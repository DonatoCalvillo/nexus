#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nexus {

enum class FileSearchScope {
    Documents,
    Downloads,
};

struct FileSearchQuery {
    FileSearchScope scope = FileSearchScope::Documents;
    std::wstring nameFilter;
};

struct FileSearchHit {
    std::wstring fileName;
    std::wstring fullPath;
};

// Parses doc / dl, optionally followed by a case-insensitive name filter.
// Returns nullopt when the query is not a file-search command.
std::optional<FileSearchQuery> TryParseFileSearchQuery(const std::wstring& query);

// Recursive Win32 search under rootPath (files only, substring match on name).
std::vector<FileSearchHit> SearchFilesInFolder(
    const std::wstring& rootPath,
    const std::wstring& nameFilter,
    size_t maxResults);

// Resolves Documents/Downloads, tries Everything IPC, then falls back to Win32.
std::vector<FileSearchHit> SearchFiles(const FileSearchQuery& query, size_t maxResults);

// When true, SearchFiles skips Everything and uses Win32 only (for unit tests).
void SetFileSearchForceWin32Fallback(bool force);

}  // namespace nexus
