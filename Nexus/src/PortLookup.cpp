#include "PortLookup.h"

#include "Constants.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iphlpapi.h>

#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Iphlpapi.lib")

namespace nexus {
namespace {

std::uint16_t NetworkToHostPort(DWORD networkPort) {
    const auto value = static_cast<std::uint16_t>(networkPort & 0xFFFFu);
    return static_cast<std::uint16_t>((value << 8) | (value >> 8));
}

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
        return L"PID " + std::to_wstring(pid);
    }

    wchar_t pathBuffer[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, pathBuffer, &size) && size > 0) {
        name = FileNameFromPath(std::wstring(pathBuffer, size));
    } else {
        name = L"PID " + std::to_wstring(pid);
    }

    CloseHandle(process);
    return name;
}

bool AlreadyListed(
    const std::vector<PortProcess>& processes,
    DWORD pid,
    const wchar_t* protocol) {
    for (const PortProcess& entry : processes) {
        if (entry.pid == pid && entry.protocolLabel == protocol) {
            return true;
        }
    }
    return false;
}

void AppendProcess(
    std::vector<PortProcess>& processes,
    DWORD pid,
    std::uint16_t port,
    const wchar_t* protocol) {
    if (AlreadyListed(processes, pid, protocol)) {
        return;
    }
    if (processes.size() >= kMaxResults) {
        return;
    }

    PortProcess entry;
    entry.pid = pid;
    entry.port = port;
    entry.protocolLabel = protocol;
    entry.processName = ProcessNameForPid(pid);
    processes.push_back(std::move(entry));
}

void CollectTcpListeners(std::uint16_t port, std::vector<PortProcess>& processes) {
    ULONG size = 0;
    DWORD result = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (result != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return;
    }

    std::vector<unsigned char> buffer(size);
    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    result = GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (result != NO_ERROR) {
        return;
    }

    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const MIB_TCPROW_OWNER_PID& row = table->table[index];
        if (row.dwState != MIB_TCP_STATE_LISTEN) {
            continue;
        }
        const std::uint16_t localPort = NetworkToHostPort(row.dwLocalPort);
        if (localPort != port) {
            continue;
        }
        AppendProcess(processes, row.dwOwningPid, port, L"TCP");
    }
}

void CollectUdpOwners(std::uint16_t port, std::vector<PortProcess>& processes) {
    ULONG size = 0;
    DWORD result = GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (result != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return;
    }

    std::vector<unsigned char> buffer(size);
    auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buffer.data());
    result = GetExtendedUdpTable(table, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (result != NO_ERROR) {
        return;
    }

    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const MIB_UDPROW_OWNER_PID& row = table->table[index];
        const std::uint16_t localPort = NetworkToHostPort(row.dwLocalPort);
        if (localPort != port) {
            continue;
        }
        AppendProcess(processes, row.dwOwningPid, port, L"UDP");
    }
}

}  // namespace

std::optional<std::uint16_t> TryParsePortQuery(const std::wstring& query) {
    const std::wstring trimmed = Trim(query);
    if (trimmed.size() < 4) {
        return std::nullopt;
    }

    // Require "-p" followed by whitespace and a port number.
    if (!StartsWithIgnoreCase(trimmed, L"-p")) {
        return std::nullopt;
    }
    if (trimmed.size() <= 2 || !iswspace(trimmed[2])) {
        return std::nullopt;
    }

    size_t index = 3;
    while (index < trimmed.size() && iswspace(trimmed[index])) {
        ++index;
    }
    if (index >= trimmed.size()) {
        return std::nullopt;
    }

    unsigned long value = 0;
    bool sawDigit = false;
    while (index < trimmed.size()) {
        const wchar_t character = trimmed[index];
        if (!iswdigit(character)) {
            return std::nullopt;
        }
        sawDigit = true;
        value = value * 10 + static_cast<unsigned long>(character - L'0');
        if (value > 65535) {
            return std::nullopt;
        }
        ++index;
    }

    if (!sawDigit || value == 0) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(value);
}

std::vector<PortProcess> FindProcessesByPort(std::uint16_t port) {
    std::vector<PortProcess> processes;
    CollectTcpListeners(port, processes);
    CollectUdpOwners(port, processes);
    return processes;
}

bool KillProcessByPid(unsigned long pid) {
    if (pid == 0 || pid == 4) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) {
        return false;
    }

    const BOOL terminated = TerminateProcess(process, 1);
    CloseHandle(process);
    return terminated != FALSE;
}

}  // namespace nexus
