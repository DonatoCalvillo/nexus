#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nexus {

struct PortProcess {
    unsigned long pid = 0;
    std::wstring processName;
    std::wstring protocolLabel;
    std::uint16_t port = 0;
};

// Parses "-p <port>" (case-insensitive). Returns nullopt when the query is not a port lookup.
std::optional<std::uint16_t> TryParsePortQuery(const std::wstring& query);

// Finds TCP LISTEN and UDP owners for the given local port (unique by pid+protocol).
std::vector<PortProcess> FindProcessesByPort(std::uint16_t port);

// Terminates the process. Returns false on access denied or other failure.
bool KillProcessByPid(unsigned long pid);

}  // namespace nexus
