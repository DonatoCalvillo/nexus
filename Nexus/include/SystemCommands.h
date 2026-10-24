#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus {

enum class SystemCommandKind {
    Lock,
    Sleep,
    Restart,
    Shutdown,
    EmptyTrash,
    KillProcess,
    Mute,
    SetVolume,
};

struct SystemCommand {
    SystemCommandKind kind = SystemCommandKind::Lock;
    std::wstring label;
    std::wstring processName;
    int volumePercent = 0;
};

struct NamedProcess {
    unsigned long pid = 0;
    std::wstring processName;
};

// Parses lock / sleep / restart / shutdown / empty trash / kill / mute / vol.
// Returns nullopt when the query is not a system command.
std::optional<SystemCommand> TryParseSystemCommand(const std::wstring& query);

// Lists running processes whose image name contains nameFilter (case-insensitive).
std::vector<NamedProcess> FindProcessesByName(const std::wstring& nameFilter);

// Runs a non-kill system command. Returns false on API failure.
bool ExecuteSystemCommand(const SystemCommand& command);

}  // namespace nexus
