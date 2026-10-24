#include "Startup.h"

#include <windows.h>

namespace nexus {
namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"Nexus";

std::wstring BuildStartupCommandLine() {
    const std::wstring executablePath = GetExecutablePath();
    if (executablePath.empty()) {
        return L"";
    }
    return L"\"" + executablePath + L"\" --autostart";
}

}  // namespace

std::wstring GetExecutablePath() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }
    return buffer;
}

bool IsStartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH * 2] = {};
    DWORD valueSize = sizeof(value);
    const LONG result = RegQueryValueExW(
        key,
        kRunValueName,
        nullptr,
        nullptr,
        reinterpret_cast<LPBYTE>(value),
        &valueSize);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && value[0] != L'\0';
}

bool EnableStartup() {
    const std::wstring commandLine = BuildStartupCommandLine();
    if (commandLine.empty()) {
        return false;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG result = RegSetValueExW(
        key,
        kRunValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(commandLine.c_str()),
        static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool DisableStartup() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG result = RegDeleteValueW(key, kRunValueName);
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

}  // namespace nexus
