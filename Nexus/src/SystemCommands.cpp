#include "SystemCommands.h"

#include "Constants.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <powrprof.h>
#include <psapi.h>
#include <shellapi.h>

#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace nexus {
namespace {

std::wstring Trim(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && iswspace(value[start])) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && iswspace(value[end - 1])) {
        --end;
    }

    return value.substr(start, end - start);
}

std::wstring CollapseWhitespace(const std::wstring& value) {
    std::wstring collapsed;
    collapsed.reserve(value.size());
    bool pendingSpace = false;
    for (const wchar_t character : value) {
        if (iswspace(character)) {
            pendingSpace = !collapsed.empty();
            continue;
        }
        if (pendingSpace) {
            collapsed.push_back(L' ');
            pendingSpace = false;
        }
        collapsed.push_back(character);
    }
    return collapsed;
}

bool EqualsIgnoreCase(const std::wstring& left, const wchar_t* right) {
    size_t index = 0;
    while (right[index] != L'\0') {
        if (index >= left.size()) {
            return false;
        }
        if (towlower(left[index]) != towlower(right[index])) {
            return false;
        }
        ++index;
    }
    return index == left.size();
}

bool StartsWithIgnoreCase(const std::wstring& value, const wchar_t* prefix) {
    size_t index = 0;
    while (prefix[index] != L'\0') {
        if (index >= value.size()) {
            return false;
        }
        if (towlower(value[index]) != towlower(prefix[index])) {
            return false;
        }
        ++index;
    }
    return true;
}

bool ContainsIgnoreCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }

    for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool match = true;
        for (size_t index = 0; index < needle.size(); ++index) {
            if (towlower(haystack[start + index]) != towlower(needle[index])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

std::wstring FileNameFromPath(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring ProcessNameForPid(DWORD pid) {
    if (pid == 0) {
        return L"System Idle Process";
    }
    if (pid == 4) {
        return L"System";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }

    wchar_t pathBuffer[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, pathBuffer, &size) && size > 0) {
        name = FileNameFromPath(std::wstring(pathBuffer, size));
    }

    CloseHandle(process);
    return name;
}

bool AlreadyListed(const std::vector<NamedProcess>& processes, DWORD pid) {
    for (const NamedProcess& entry : processes) {
        if (entry.pid == pid) {
            return true;
        }
    }
    return false;
}

std::optional<int> TryParseVolumePercent(const std::wstring& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    unsigned long value = 0;
    for (const wchar_t character : text) {
        if (!iswdigit(character)) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<unsigned long>(character - L'0');
        if (value > 100) {
            return std::nullopt;
        }
    }

    return static_cast<int>(value);
}

bool EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES privileges = {};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }

    const BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(token);
    return adjusted != FALSE && error == ERROR_SUCCESS;
}

HRESULT GetDefaultEndpointVolume(IAudioEndpointVolume** volume) {
    if (!volume) {
        return E_POINTER;
    }
    *volume = nullptr;

    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(result) || !enumerator) {
        return result;
    }

    IMMDevice* device = nullptr;
    result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    enumerator->Release();
    if (FAILED(result) || !device) {
        return result;
    }

    result = device->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(volume));
    device->Release();
    return result;
}

bool SetMasterMute(bool muted) {
    IAudioEndpointVolume* volume = nullptr;
    const HRESULT result = GetDefaultEndpointVolume(&volume);
    if (FAILED(result) || !volume) {
        return false;
    }

    const HRESULT muteResult = volume->SetMute(muted ? TRUE : FALSE, nullptr);
    volume->Release();
    return SUCCEEDED(muteResult);
}

bool SetMasterVolumePercent(int percent) {
    if (percent < 0 || percent > 100) {
        return false;
    }

    IAudioEndpointVolume* volume = nullptr;
    const HRESULT result = GetDefaultEndpointVolume(&volume);
    if (FAILED(result) || !volume) {
        return false;
    }

    const float scalar = static_cast<float>(percent) / 100.0f;
    HRESULT setResult = volume->SetMasterVolumeLevelScalar(scalar, nullptr);
    if (SUCCEEDED(setResult)) {
        setResult = volume->SetMute(FALSE, nullptr);
    }
    volume->Release();
    return SUCCEEDED(setResult);
}

bool LockSession() {
    return LockWorkStation() != FALSE;
}

bool SuspendComputer() {
    return SetSuspendState(FALSE, FALSE, FALSE) != FALSE;
}

bool RestartComputer() {
    if (!EnableShutdownPrivilege()) {
        return false;
    }
    return ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER) != FALSE;
}

bool ShutdownComputer() {
    if (!EnableShutdownPrivilege()) {
        return false;
    }
    return ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER) != FALSE;
}

bool EmptyRecycleBin() {
    const HRESULT result = SHEmptyRecycleBinW(
        nullptr,
        nullptr,
        SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    return SUCCEEDED(result) || result == S_FALSE;
}

SystemCommand MakeCommand(SystemCommandKind kind, std::wstring label) {
    SystemCommand command;
    command.kind = kind;
    command.label = std::move(label);
    return command;
}

}  // namespace

std::optional<SystemCommand> TryParseSystemCommand(const std::wstring& query) {
    const std::wstring trimmed = CollapseWhitespace(Trim(query));
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (EqualsIgnoreCase(trimmed, L"lock")) {
        return MakeCommand(SystemCommandKind::Lock, L"Bloquear sesi\u00F3n");
    }
    if (EqualsIgnoreCase(trimmed, L"sleep")) {
        return MakeCommand(SystemCommandKind::Sleep, L"Suspender equipo");
    }
    if (EqualsIgnoreCase(trimmed, L"restart")) {
        return MakeCommand(SystemCommandKind::Restart, L"Reiniciar equipo");
    }
    if (EqualsIgnoreCase(trimmed, L"shutdown")) {
        return MakeCommand(SystemCommandKind::Shutdown, L"Apagar equipo");
    }
    if (EqualsIgnoreCase(trimmed, L"mute")) {
        return MakeCommand(SystemCommandKind::Mute, L"Silenciar volumen");
    }
    if (EqualsIgnoreCase(trimmed, L"empty trash")) {
        return MakeCommand(
            SystemCommandKind::EmptyTrash,
            L"Vaciar papelera de reciclaje");
    }

    if (StartsWithIgnoreCase(trimmed, L"kill")) {
        if (trimmed.size() <= 4 || !iswspace(trimmed[4])) {
            return std::nullopt;
        }

        const std::wstring processName = Trim(trimmed.substr(5));
        if (processName.empty()) {
            return std::nullopt;
        }

        SystemCommand command = MakeCommand(
            SystemCommandKind::KillProcess,
            L"Terminar proceso");
        command.processName = processName;
        return command;
    }

    if (StartsWithIgnoreCase(trimmed, L"vol")) {
        if (trimmed.size() <= 3 || !iswspace(trimmed[3])) {
            return std::nullopt;
        }

        const std::wstring levelText = Trim(trimmed.substr(4));
        const std::optional<int> percent = TryParseVolumePercent(levelText);
        if (!percent) {
            return std::nullopt;
        }

        SystemCommand command = MakeCommand(
            SystemCommandKind::SetVolume,
            L"Volumen al " + std::to_wstring(*percent) + L"%");
        command.volumePercent = *percent;
        return command;
    }

    return std::nullopt;
}

std::vector<NamedProcess> FindProcessesByName(const std::wstring& nameFilter) {
    std::vector<NamedProcess> matches;
    if (nameFilter.empty()) {
        return matches;
    }

    DWORD processIds[1024] = {};
    DWORD bytesNeeded = 0;
    if (!EnumProcesses(processIds, sizeof(processIds), &bytesNeeded)) {
        return matches;
    }

    const DWORD processCount = bytesNeeded / sizeof(DWORD);
    const DWORD currentPid = GetCurrentProcessId();

    for (DWORD index = 0; index < processCount; ++index) {
        const DWORD pid = processIds[index];
        if (pid == 0 || pid == 4 || pid == currentPid) {
            continue;
        }
        if (AlreadyListed(matches, pid)) {
            continue;
        }

        const std::wstring processName = ProcessNameForPid(pid);
        if (processName.empty() || !ContainsIgnoreCase(processName, nameFilter)) {
            continue;
        }

        NamedProcess entry;
        entry.pid = pid;
        entry.processName = processName;
        matches.push_back(std::move(entry));

        if (matches.size() >= kMaxResults) {
            break;
        }
    }

    return matches;
}

bool ExecuteSystemCommand(const SystemCommand& command) {
    switch (command.kind) {
    case SystemCommandKind::Lock:
        return LockSession();
    case SystemCommandKind::Sleep:
        return SuspendComputer();
    case SystemCommandKind::Restart:
        return RestartComputer();
    case SystemCommandKind::Shutdown:
        return ShutdownComputer();
    case SystemCommandKind::EmptyTrash:
        return EmptyRecycleBin();
    case SystemCommandKind::Mute:
        return SetMasterMute(true);
    case SystemCommandKind::SetVolume:
        return SetMasterVolumePercent(command.volumePercent);
    case SystemCommandKind::KillProcess:
        return false;
    }

    return false;
}

}  // namespace nexus
