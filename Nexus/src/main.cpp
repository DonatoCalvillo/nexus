#define _CRT_SECURE_NO_WARNINGS

#include "AppIndexer.h"
#include "Constants.h"
#include "Overlay.h"
#include "Startup.h"

#include <commctrl.h>
#include <cwctype>
#include <shellapi.h>
#include <string>
#include <windows.h>

namespace {

struct LaunchOptions {
    bool startHidden = false;
    bool enableStartup = false;
    bool disableStartup = false;
};

std::wstring ToLowerCopy(std::wstring value) {
    for (wchar_t& character : value) {
        character = static_cast<wchar_t>(towlower(character));
    }
    return value;
}

LaunchOptions ParseCommandLine() {
    LaunchOptions options;

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) {
        return options;
    }

    for (int index = 0; index < argumentCount; ++index) {
        const std::wstring argument = ToLowerCopy(arguments[index]);
        if (argument == L"--autostart" || argument == L"/autostart") {
            options.startHidden = true;
        } else if (argument == L"--install-startup" || argument == L"/install-startup") {
            options.enableStartup = true;
        } else if (argument == L"--uninstall-startup" || argument == L"/uninstall-startup") {
            options.disableStartup = true;
        }
    }

    LocalFree(arguments);
    return options;
}

void InitializeRuntime() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX commonControls = {
        sizeof(INITCOMMONCONTROLSEX),
        ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);
}

void ShutdownRuntime() {
    CoUninitialize();
}

bool TryActivateExistingInstance() {
    HWND existingWindow = nexus::FindExistingOverlayWindow();
    if (!existingWindow) {
        return false;
    }
    PostMessageW(existingWindow, nexus::kShowOverlayMessage, 0, 0);
    return true;
}

}  // namespace

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE previousInstance,
    _In_ PWSTR commandLine,
    _In_ int showCommand) {
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    const LaunchOptions options = ParseCommandLine();

    if (options.disableStartup) {
        nexus::DisableStartup();
        if (!options.startHidden && !options.enableStartup) {
            return 0;
        }
    }
    if (options.enableStartup) {
        nexus::EnableStartup();
    }

    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, nexus::kSingleInstanceMutexName);
    if (singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!options.startHidden) {
            TryActivateExistingInstance();
        }
        if (singleInstanceMutex) {
            CloseHandle(singleInstanceMutex);
        }
        return 0;
    }

    InitializeRuntime();
    nexus::IndexApplications();

    if (!nexus::CreateOverlayWindow(instance)) {
        ShutdownRuntime();
        if (singleInstanceMutex) {
            CloseHandle(singleInstanceMutex);
        }
        return 0;
    }

    // Register login startup on first run so Ctrl+Space is always available.
    if (!options.disableStartup) {
        nexus::EnableStartup();
    }

    if (!options.startHidden) {
        nexus::ShowOverlayWindow();
    }

    const int exitCode = nexus::RunMessageLoop();

    ShutdownRuntime();
    if (singleInstanceMutex) {
        ReleaseMutex(singleInstanceMutex);
        CloseHandle(singleInstanceMutex);
    }
    return exitCode;
}
