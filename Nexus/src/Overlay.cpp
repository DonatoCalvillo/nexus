#include "Overlay.h"

#include "AppIndexer.h"
#include "Calculator.h"
#include "Constants.h"
#include "IconManager.h"
#include "PortLookup.h"
#include "SystemCommands.h"
#include "WebSearch.h"

#include <commctrl.h>
#include <cstring>
#include <dwmapi.h>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")

namespace nexus {

HWND g_hwnd = nullptr;
HWND g_hwndEdit = nullptr;
HFONT g_hFont = nullptr;
HFONT g_hFontList = nullptr;
HBRUSH g_bgBrush = nullptr;
int g_selectedIndex = 0;
static std::optional<WebAction> g_webAction;
static std::optional<CalcResult> g_calcResult;
static std::optional<std::uint16_t> g_portQuery;
static std::vector<PortProcess> g_portProcesses;
static std::optional<SystemCommand> g_systemCommand;
static std::vector<NamedProcess> g_killProcesses;

static int PortRowCount() {
    if (!g_portQuery) {
        return 0;
    }
    if (g_portProcesses.empty()) {
        return 1;
    }
    return static_cast<int>(g_portProcesses.size());
}

static int SystemCommandRowCount() {
    if (!g_systemCommand) {
        return 0;
    }
    if (g_systemCommand->kind == SystemCommandKind::KillProcess) {
        if (g_killProcesses.empty()) {
            return 1;
        }
        return static_cast<int>(g_killProcesses.size());
    }
    return 1;
}

static int SpecialRowCount() {
    return (g_webAction ? 1 : 0) + (g_calcResult ? 1 : 0) + PortRowCount()
        + SystemCommandRowCount();
}

static std::wstring FormatPortProcessLabel(const PortProcess& process) {
    return process.processName + L" - PID " + std::to_wstring(process.pid) + L" | "
        + process.protocolLabel + L" | puerto " + std::to_wstring(process.port);
}

static std::wstring FormatNamedProcessLabel(const NamedProcess& process) {
    return process.processName + L" - PID " + std::to_wstring(process.pid);
}

static bool ConfirmAndKillProcess(const std::wstring& processName, unsigned long pid) {
    const std::wstring message =
        L"\u00BFTerminar " + processName + L" (PID " + std::to_wstring(pid) + L")?";
    const int answer = MessageBoxW(
        nullptr,
        message.c_str(),
        L"Nexus",
        MB_YESNO | MB_ICONWARNING | MB_TASKMODAL);
    if (answer != IDYES) {
        return true;
    }

    if (KillProcessByPid(pid)) {
        return true;
    }

    MessageBoxW(
        nullptr,
        L"No se pudo terminar el proceso. Puede requerir permisos elevados.",
        L"Nexus",
        MB_OK | MB_ICONERROR | MB_TASKMODAL);
    return false;
}

static void ShowCommandFailure(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"Nexus", MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

static int VisibleRowCount() {
    return SpecialRowCount() + static_cast<int>(g_filteredApps.size());
}

static void ResizeOverlayToResults() {
    const int newHeight = kHeaderHeight + VisibleRowCount() * kItemHeight;
    SetWindowPos(
        g_hwnd,
        nullptr,
        0,
        0,
        kWindowWidth,
        newHeight,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static bool CopyTextToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_hwnd)) {
        return false;
    }

    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }

    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(memory);
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return true;
}

void ApplyWindowEffects(HWND hwnd) {
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(
        hwnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference,
        sizeof(cornerPreference));

    DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(
        hwnd,
        DWMWA_SYSTEMBACKDROP_TYPE,
        &backdropType,
        sizeof(backdropType));
}

static bool RegisterOverlayClass(HINSTANCE instance) {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    return RegisterClassW(&windowClass) != 0;
}

bool CreateOverlayWindow(HINSTANCE instance) {
    if (!RegisterOverlayClass(instance)) {
        return false;
    }

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int positionX = (screenWidth - kWindowWidth) / 2;
    const int positionY = screenHeight / 4;

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"Nexus",
        WS_POPUP,
        positionX,
        positionY,
        kWindowWidth,
        kHeaderHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!g_hwnd) {
        return false;
    }

    ApplyWindowEffects(g_hwnd);
    RegisterHotKey(g_hwnd, kHotkeyId, MOD_CONTROL, VK_SPACE);
    return true;
}

void ShowOverlayWindow() {
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwndEdit);
}

void RequestShowOverlay() {
    if (g_hwnd) {
        PostMessageW(g_hwnd, kShowOverlayMessage, 0, 0);
    }
}

HWND FindExistingOverlayWindow() {
    return FindWindowW(kWindowClassName, L"Nexus");
}

int RunMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void CloseOverlay() {
    if (!g_hwnd) {
        return;
    }

    bool isVisible = IsWindowVisible(g_hwnd);
    bool hasText = g_hwndEdit && GetWindowTextLengthW(g_hwndEdit) > 0;
    if (!isVisible && !hasText) {
        return;
    }

    ShowWindow(g_hwnd, SW_HIDE);
    if (hasText) {
        SetWindowTextW(g_hwndEdit, L"");
    }
}

void LaunchSelected() {
    const int rowCount = VisibleRowCount();
    if (rowCount == 0) {
        return;
    }
    if (g_selectedIndex < 0 || g_selectedIndex >= rowCount) {
        return;
    }

    int rowOffset = 0;
    if (g_webAction) {
        if (g_selectedIndex == rowOffset) {
            const std::wstring url = g_webAction->url;
            CloseOverlay();

            SHELLEXECUTEINFOW executeInfo = {};
            executeInfo.cbSize = sizeof(executeInfo);
            executeInfo.nShow = SW_SHOWNORMAL;
            executeInfo.lpVerb = L"open";
            executeInfo.lpFile = url.c_str();
            executeInfo.fMask = SEE_MASK_FLAG_NO_UI;
            ShellExecuteExW(&executeInfo);
            return;
        }
        ++rowOffset;
    }

    if (g_calcResult) {
        if (g_selectedIndex == rowOffset) {
            const std::wstring resultText = g_calcResult->formattedValue;
            CloseOverlay();
            CopyTextToClipboard(resultText);
            return;
        }
        ++rowOffset;
    }

    if (g_portQuery) {
        if (g_portProcesses.empty()) {
            if (g_selectedIndex == rowOffset) {
                return;
            }
            ++rowOffset;
        } else {
            const int portIndex = g_selectedIndex - rowOffset;
            if (portIndex >= 0 && portIndex < static_cast<int>(g_portProcesses.size())) {
                const PortProcess process = g_portProcesses[static_cast<size_t>(portIndex)];
                CloseOverlay();

                const std::wstring message = L"\u00BFTerminar " + process.processName + L" (PID "
                    + std::to_wstring(process.pid) + L") en el puerto "
                    + std::to_wstring(process.port) + L"?";
                const int answer = MessageBoxW(
                    nullptr,
                    message.c_str(),
                    L"Nexus",
                    MB_YESNO | MB_ICONWARNING | MB_TASKMODAL);
                if (answer == IDYES) {
                    if (!KillProcessByPid(process.pid)) {
                        ShowCommandFailure(
                            L"No se pudo terminar el proceso. Puede requerir permisos elevados.");
                    }
                }
                return;
            }
            rowOffset += static_cast<int>(g_portProcesses.size());
        }
    }

    if (g_systemCommand) {
        if (g_systemCommand->kind == SystemCommandKind::KillProcess) {
            if (g_killProcesses.empty()) {
                if (g_selectedIndex == rowOffset) {
                    return;
                }
                ++rowOffset;
            } else {
                const int killIndex = g_selectedIndex - rowOffset;
                if (killIndex >= 0 && killIndex < static_cast<int>(g_killProcesses.size())) {
                    const NamedProcess process = g_killProcesses[static_cast<size_t>(killIndex)];
                    CloseOverlay();
                    ConfirmAndKillProcess(process.processName, process.pid);
                    return;
                }
                rowOffset += static_cast<int>(g_killProcesses.size());
            }
        } else if (g_selectedIndex == rowOffset) {
            const SystemCommand command = *g_systemCommand;
            CloseOverlay();

            if (command.kind == SystemCommandKind::Restart
                || command.kind == SystemCommandKind::Shutdown
                || command.kind == SystemCommandKind::EmptyTrash) {
                const std::wstring prompt = L"\u00BF" + command.label + L"?";
                const int answer = MessageBoxW(
                    nullptr,
                    prompt.c_str(),
                    L"Nexus",
                    MB_YESNO | MB_ICONWARNING | MB_TASKMODAL);
                if (answer != IDYES) {
                    return;
                }
            }

            if (!ExecuteSystemCommand(command)) {
                ShowCommandFailure(L"No se pudo ejecutar el comando.");
            }
            return;
        } else {
            ++rowOffset;
        }
    }

    const int appIndex = g_selectedIndex - SpecialRowCount();
    if (appIndex < 0 || appIndex >= static_cast<int>(g_filteredApps.size())) {
        return;
    }

    const AppItem* app = g_filteredApps[appIndex];
    const std::wstring launchPath = app->path;
    PIDLIST_ABSOLUTE launchPidl = app->pidl;

    // Hide first so focus can move cleanly to the launched app.
    CloseOverlay();

    SHELLEXECUTEINFOW executeInfo = {};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.nShow = SW_SHOWNORMAL;
    executeInfo.lpVerb = L"open";
    executeInfo.fMask = SEE_MASK_FLAG_NO_UI;

    if (launchPidl) {
        executeInfo.fMask |= SEE_MASK_IDLIST;
        executeInfo.lpIDList = launchPidl;
    } else {
        executeInfo.lpFile = launchPath.c_str();
    }

    ShellExecuteExW(&executeInfo);
}

LRESULT CALLBACK EditSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData) {
    (void)subclassId;
    (void)refData;

    switch (message) {
    case WM_GETDLGCODE:
        return DefSubclassProc(hwnd, message, wParam, lParam) | DLGC_WANTALLKEYS;

    case WM_KEYDOWN: {
        if (wParam == VK_DOWN) {
            const int rowCount = VisibleRowCount();
            if (rowCount > 0) {
                g_selectedIndex = (g_selectedIndex + 1) % rowCount;
                InvalidateRect(g_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == VK_UP) {
            const int rowCount = VisibleRowCount();
            if (rowCount > 0) {
                g_selectedIndex = (g_selectedIndex - 1 + rowCount) % rowCount;
                InvalidateRect(g_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == VK_RETURN) {
            // Defer launch: ShellExecute from inside an EDIT subclass is unreliable.
            PostMessageW(g_hwnd, kLaunchSelectedMessage, 0, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            CloseOverlay();
            return 0;
        }
        break;
    }

    case WM_CHAR:
        // Single-line EDIT beeps on Enter unless WM_CHAR is consumed.
        if (wParam == '\r' || wParam == '\n') {
            return 0;
        }
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static void HandleCreate(HWND hwnd, LPCREATESTRUCT createStruct) {
    g_hwnd = hwnd;
    g_bgBrush = CreateSolidBrush(RGB(28, 28, 28));

    g_hFont = CreateFontW(
        -20,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    g_hFontList = CreateFontW(
        -16,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    g_hwndEdit = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        20,
        20,
        kWindowWidth - 40,
        28,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditControlId)),
        createStruct->hInstance,
        nullptr);

    SendMessageW(g_hwndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    SendMessageW(
        g_hwndEdit,
        EM_SETCUEBANNER,
        TRUE,
        reinterpret_cast<LPARAM>(L"Buscar programas o archivos..."));

    SetWindowSubclass(g_hwndEdit, EditSubclassProc, 0, 0);
    UpdateFilter(L"");
}

static void HandleCommand(WPARAM wParam) {
    if (LOWORD(wParam) == kEditControlId && HIWORD(wParam) == EN_CHANGE) {
        int length = GetWindowTextLengthW(g_hwndEdit);
        std::wstring buffer(length + 1, L'\0');
        GetWindowTextW(g_hwndEdit, &buffer[0], length + 1);
        buffer.resize(length);
        UpdateFilter(buffer);
        g_webAction = TryParseWebAction(buffer);
        g_calcResult = TryEvaluate(buffer);
        g_portQuery = TryParsePortQuery(buffer);
        if (g_portQuery) {
            g_portProcesses = FindProcessesByPort(*g_portQuery);
        } else {
            g_portProcesses.clear();
        }
        g_systemCommand = TryParseSystemCommand(buffer);
        if (g_systemCommand && g_systemCommand->kind == SystemCommandKind::KillProcess) {
            g_killProcesses = FindProcessesByName(g_systemCommand->processName);
        } else {
            g_killProcesses.clear();
        }
        g_selectedIndex = 0;
        ResizeOverlayToResults();
    }
}

static void PaintResultRow(
    HDC deviceContext,
    int index,
    int itemTop,
    const RECT& itemRect,
    bool selected) {
    if (selected) {
        HBRUSH selectionBrush = CreateSolidBrush(RGB(0, 114, 239));
        HPEN selectionPen = CreatePen(PS_SOLID, 1, RGB(0, 114, 239));
        HGDIOBJ previousBrush = SelectObject(deviceContext, selectionBrush);
        HGDIOBJ previousPen = SelectObject(deviceContext, selectionPen);

        RoundRect(
            deviceContext,
            itemRect.left,
            itemRect.top,
            itemRect.right,
            itemRect.bottom,
            10,
            10);

        SelectObject(deviceContext, previousBrush);
        SelectObject(deviceContext, previousPen);
        DeleteObject(selectionBrush);
        DeleteObject(selectionPen);
    }

    SetTextColor(deviceContext, selected ? RGB(255, 255, 255) : RGB(197, 197, 204));

    int rowOffset = 0;
    if (g_webAction) {
        if (index == rowOffset) {
            RECT textRect = {
                itemRect.left + 14,
                itemTop + 6,
                itemRect.right - 14,
                itemRect.bottom};
            DrawTextW(
                deviceContext,
                g_webAction->label.c_str(),
                -1,
                &textRect,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            return;
        }
        ++rowOffset;
    }

    if (g_calcResult) {
        if (index == rowOffset) {
            RECT equalsRect = {
                itemRect.left + 12,
                itemTop + 6,
                itemRect.left + 36,
                itemRect.bottom};
            DrawTextW(deviceContext, L"=", -1, &equalsRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

            RECT textRect = {
                itemRect.left + 44,
                itemTop + 6,
                itemRect.right - 14,
                itemRect.bottom};
            DrawTextW(
                deviceContext,
                g_calcResult->formattedValue.c_str(),
                -1,
                &textRect,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            return;
        }
        ++rowOffset;
    }

    if (g_portQuery) {
        if (g_portProcesses.empty()) {
            if (index == rowOffset) {
                const std::wstring emptyLabel =
                    L"Ning\u00FAn proceso en el puerto " + std::to_wstring(*g_portQuery);
                RECT textRect = {
                    itemRect.left + 14,
                    itemTop + 6,
                    itemRect.right - 14,
                    itemRect.bottom};
                DrawTextW(
                    deviceContext,
                    emptyLabel.c_str(),
                    -1,
                    &textRect,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                return;
            }
            ++rowOffset;
        } else {
            const int portIndex = index - rowOffset;
            if (portIndex >= 0 && portIndex < static_cast<int>(g_portProcesses.size())) {
                const std::wstring label =
                    FormatPortProcessLabel(g_portProcesses[static_cast<size_t>(portIndex)]);
                RECT textRect = {
                    itemRect.left + 14,
                    itemTop + 6,
                    itemRect.right - 14,
                    itemRect.bottom};
                DrawTextW(
                    deviceContext,
                    label.c_str(),
                    -1,
                    &textRect,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                return;
            }
            rowOffset += static_cast<int>(g_portProcesses.size());
        }
    }

    if (g_systemCommand) {
        if (g_systemCommand->kind == SystemCommandKind::KillProcess) {
            if (g_killProcesses.empty()) {
                if (index == rowOffset) {
                    const std::wstring emptyLabel =
                        L"Ning\u00FAn proceso coincide con \"" + g_systemCommand->processName
                        + L"\"";
                    RECT textRect = {
                        itemRect.left + 14,
                        itemTop + 6,
                        itemRect.right - 14,
                        itemRect.bottom};
                    DrawTextW(
                        deviceContext,
                        emptyLabel.c_str(),
                        -1,
                        &textRect,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    return;
                }
                ++rowOffset;
            } else {
                const int killIndex = index - rowOffset;
                if (killIndex >= 0 && killIndex < static_cast<int>(g_killProcesses.size())) {
                    const std::wstring label =
                        FormatNamedProcessLabel(g_killProcesses[static_cast<size_t>(killIndex)]);
                    RECT textRect = {
                        itemRect.left + 14,
                        itemTop + 6,
                        itemRect.right - 14,
                        itemRect.bottom};
                    DrawTextW(
                        deviceContext,
                        label.c_str(),
                        -1,
                        &textRect,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    return;
                }
                rowOffset += static_cast<int>(g_killProcesses.size());
            }
        } else if (index == rowOffset) {
            RECT textRect = {
                itemRect.left + 14,
                itemTop + 6,
                itemRect.right - 14,
                itemRect.bottom};
            DrawTextW(
                deviceContext,
                g_systemCommand->label.c_str(),
                -1,
                &textRect,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            return;
        } else {
            ++rowOffset;
        }
    }

    const int appIndex = index - SpecialRowCount();
    if (appIndex >= 0 && appIndex < static_cast<int>(g_visibleIcons.size())
        && g_visibleIcons[appIndex].icon) {
        DrawIconEx(
            deviceContext,
            itemRect.left + 12,
            itemTop + 7,
            g_visibleIcons[appIndex].icon,
            20,
            20,
            0,
            nullptr,
            DI_NORMAL);
    }

    if (appIndex < 0 || appIndex >= static_cast<int>(g_filteredApps.size())) {
        return;
    }

    RECT textRect = {
        itemRect.left + 44,
        itemTop + 6,
        itemRect.right - 14,
        itemRect.bottom};
    DrawTextW(
        deviceContext,
        g_filteredApps[appIndex]->name.c_str(),
        -1,
        &textRect,
        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void PaintResults(HDC deviceContext) {
    SelectObject(deviceContext, g_hFontList);
    SetBkMode(deviceContext, TRANSPARENT);

    const int rowCount = VisibleRowCount();
    for (int index = 0; index < rowCount; ++index) {
        const int itemTop = kHeaderHeight + (index * kItemHeight);
        const RECT itemRect = {12, itemTop, kWindowWidth - 12, itemTop + kItemHeight - 4};
        PaintResultRow(deviceContext, index, itemTop, itemRect, index == g_selectedIndex);
    }
}

static void HandlePaint(HWND hwnd) {
    PAINTSTRUCT paintStruct;
    HDC deviceContext = BeginPaint(hwnd, &paintStruct);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    HBRUSH backgroundBrush = CreateSolidBrush(RGB(24, 24, 28));
    FillRect(deviceContext, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (VisibleRowCount() > 0) {
        HPEN linePen = CreatePen(PS_SOLID, 1, RGB(48, 48, 54));
        HGDIOBJ previousPen = SelectObject(deviceContext, linePen);
        MoveToEx(deviceContext, 18, kHeaderHeight - 6, nullptr);
        LineTo(deviceContext, kWindowWidth - 18, kHeaderHeight - 6);
        SelectObject(deviceContext, previousPen);
        DeleteObject(linePen);
    }

    PaintResults(deviceContext);
    EndPaint(hwnd, &paintStruct);
}

static void HandleDestroy(HWND hwnd) {
    KillTimer(hwnd, kIconTimerId);
    UnregisterHotKey(hwnd, kHotkeyId);
    ReleaseIndexedApps();
    if (g_hFont) {
        DeleteObject(g_hFont);
    }
    if (g_hFontList) {
        DeleteObject(g_hFontList);
    }
    if (g_bgBrush) {
        DeleteObject(g_bgBrush);
    }
    PostQuitMessage(0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        HandleCreate(hwnd, reinterpret_cast<LPCREATESTRUCT>(lParam));
        return 0;

    case WM_COMMAND:
        HandleCommand(wParam);
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, RGB(245, 245, 245));
        SetBkColor(deviceContext, RGB(28, 28, 28));
        return reinterpret_cast<LRESULT>(g_bgBrush);
    }

    case WM_PAINT:
        HandlePaint(hwnd);
        return 0;

    case kLaunchSelectedMessage:
        LaunchSelected();
        return 0;

    case kShowOverlayMessage:
        ShowOverlayWindow();
        return 0;

    case WM_HOTKEY:
        if (wParam == kHotkeyId) {
            if (IsWindowVisible(hwnd)) {
                CloseOverlay();
            } else {
                ShowOverlayWindow();
            }
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            CloseOverlay();
        }
        return 0;

    case WM_TIMER:
        if (wParam == kIconTimerId) {
            KillTimer(hwnd, kIconTimerId);
            ++g_iconRetry;
            SyncVisibleIcons();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_DESTROY:
        HandleDestroy(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace nexus
