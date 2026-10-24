#include "Overlay.h"

#include "AppIndexer.h"
#include "Constants.h"
#include "IconManager.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <string>

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
    if (g_filteredApps.empty()) {
        return;
    }
    if (g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(g_filteredApps.size())) {
        return;
    }

    const AppItem* app = g_filteredApps[g_selectedIndex];
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
            if (!g_filteredApps.empty()) {
                g_selectedIndex = (g_selectedIndex + 1) % static_cast<int>(g_filteredApps.size());
                InvalidateRect(g_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == VK_UP) {
            if (!g_filteredApps.empty()) {
                g_selectedIndex =
                    (g_selectedIndex - 1 + static_cast<int>(g_filteredApps.size()))
                    % static_cast<int>(g_filteredApps.size());
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
    }
}

static void PaintResults(HDC deviceContext) {
    SelectObject(deviceContext, g_hFontList);
    SetBkMode(deviceContext, TRANSPARENT);

    for (int index = 0; index < static_cast<int>(g_filteredApps.size()); ++index) {
        int itemTop = kHeaderHeight + (index * kItemHeight);
        RECT itemRect = {12, itemTop, kWindowWidth - 12, itemTop + kItemHeight - 4};

        if (index == g_selectedIndex) {
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

        if (index < static_cast<int>(g_visibleIcons.size()) && g_visibleIcons[index].icon) {
            DrawIconEx(
                deviceContext,
                itemRect.left + 12,
                itemTop + 7,
                g_visibleIcons[index].icon,
                20,
                20,
                0,
                nullptr,
                DI_NORMAL);
        }

        SetTextColor(
            deviceContext,
            (index == g_selectedIndex) ? RGB(255, 255, 255) : RGB(197, 197, 204));
        RECT textRect = {
            itemRect.left + 44,
            itemTop + 6,
            itemRect.right - 14,
            itemRect.bottom};
        DrawTextW(
            deviceContext,
            g_filteredApps[index]->name.c_str(),
            -1,
            &textRect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
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

    if (!g_filteredApps.empty()) {
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
