#include "FileSearch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

#include <cwchar>
#include <cwctype>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef EVERYTHINGAPI
#define EVERYTHINGAPI __stdcall
#endif

#ifndef EVERYTHING_REQUEST_FILE_NAME
#define EVERYTHING_REQUEST_FILE_NAME 0x00000001
#define EVERYTHING_REQUEST_PATH 0x00000002
#define EVERYTHING_REQUEST_FULL_PATH_AND_FILE_NAME 0x00000004
#endif

#ifndef EVERYTHING_ERROR_IPC
#define EVERYTHING_ERROR_IPC 2
#endif

namespace nexus {
namespace {

bool g_forceWin32Fallback = false;

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

std::optional<FileSearchQuery> TryParsePrefix(
    const std::wstring& trimmed,
    const wchar_t* prefix,
    FileSearchScope scope) {
    const size_t prefixLength = wcslen(prefix);
    if (trimmed.size() == prefixLength && EqualsIgnoreCase(trimmed, prefix)) {
        FileSearchQuery query;
        query.scope = scope;
        return query;
    }

    if (trimmed.size() <= prefixLength) {
        return std::nullopt;
    }
    if (!StartsWithIgnoreCase(trimmed, prefix)) {
        return std::nullopt;
    }
    if (!iswspace(trimmed[prefixLength])) {
        return std::nullopt;
    }

    FileSearchQuery query;
    query.scope = scope;
    query.nameFilter = Trim(trimmed.substr(prefixLength + 1));
    return query;
}

std::wstring JoinPath(const std::wstring& folder, const std::wstring& name) {
    if (folder.empty()) {
        return name;
    }
    if (folder.back() == L'\\' || folder.back() == L'/') {
        return folder + name;
    }
    return folder + L'\\' + name;
}

std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &path))) {
        std::wstring result = path;
        CoTaskMemFree(path);
        return result;
    }
    return {};
}

void SearchFilesRecursive(
    const std::wstring& folderPath,
    const std::wstring& nameFilter,
    size_t maxResults,
    std::vector<FileSearchHit>& hits) {
    if (hits.size() >= maxResults || folderPath.empty()) {
        return;
    }

    const std::wstring pattern = JoinPath(folderPath, L"*");
    WIN32_FIND_DATAW findData = {};
    const HANDLE findHandle = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &findData,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (hits.size() >= maxResults) {
            break;
        }

        const wchar_t* name = findData.cFileName;
        if (name[0] == L'.'
            && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
            continue;
        }

        const std::wstring childPath = JoinPath(folderPath, name);
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                SearchFilesRecursive(childPath, nameFilter, maxResults, hits);
            }
            continue;
        }

        if (!ContainsIgnoreCase(name, nameFilter)) {
            continue;
        }

        FileSearchHit hit;
        hit.fileName = name;
        hit.fullPath = childPath;
        hits.push_back(std::move(hit));
    } while (FindNextFileW(findHandle, &findData));

    FindClose(findHandle);
}

struct EverythingApi {
    using SetSearchWFn = void(EVERYTHINGAPI*)(LPCWSTR);
    using SetMaxFn = void(EVERYTHINGAPI*)(DWORD);
    using SetRequestFlagsFn = void(EVERYTHINGAPI*)(DWORD);
    using QueryWFn = BOOL(EVERYTHINGAPI*)(BOOL);
    using GetLastErrorFn = DWORD(EVERYTHINGAPI*)(void);
    using GetNumResultsFn = DWORD(EVERYTHINGAPI*)(void);
    using IsFileResultFn = BOOL(EVERYTHINGAPI*)(DWORD);
    using GetResultFileNameWFn = LPCWSTR(EVERYTHINGAPI*)(DWORD);
    using GetResultFullPathNameWFn = DWORD(EVERYTHINGAPI*)(DWORD, LPWSTR, DWORD);
    using ResetFn = void(EVERYTHINGAPI*)(void);

    HMODULE module = nullptr;
    SetSearchWFn SetSearchW = nullptr;
    SetMaxFn SetMax = nullptr;
    SetRequestFlagsFn SetRequestFlags = nullptr;
    QueryWFn QueryW = nullptr;
    GetLastErrorFn GetLastError = nullptr;
    GetNumResultsFn GetNumResults = nullptr;
    IsFileResultFn IsFileResult = nullptr;
    GetResultFileNameWFn GetResultFileNameW = nullptr;
    GetResultFullPathNameWFn GetResultFullPathNameW = nullptr;
    ResetFn Reset = nullptr;
    bool ready = false;
};

EverythingApi& GetEverythingApi() {
    static std::once_flag loadFlag;
    static EverythingApi api;

    std::call_once(loadFlag, []() {
        api.module = LoadLibraryW(L"Everything64.dll");
        if (!api.module) {
            return;
        }

        auto load = [&](const char* name) -> FARPROC {
            return GetProcAddress(api.module, name);
        };

        api.SetSearchW = reinterpret_cast<EverythingApi::SetSearchWFn>(load("Everything_SetSearchW"));
        api.SetMax = reinterpret_cast<EverythingApi::SetMaxFn>(load("Everything_SetMax"));
        api.SetRequestFlags =
            reinterpret_cast<EverythingApi::SetRequestFlagsFn>(load("Everything_SetRequestFlags"));
        api.QueryW = reinterpret_cast<EverythingApi::QueryWFn>(load("Everything_QueryW"));
        api.GetLastError =
            reinterpret_cast<EverythingApi::GetLastErrorFn>(load("Everything_GetLastError"));
        api.GetNumResults =
            reinterpret_cast<EverythingApi::GetNumResultsFn>(load("Everything_GetNumResults"));
        api.IsFileResult =
            reinterpret_cast<EverythingApi::IsFileResultFn>(load("Everything_IsFileResult"));
        api.GetResultFileNameW = reinterpret_cast<EverythingApi::GetResultFileNameWFn>(
            load("Everything_GetResultFileNameW"));
        api.GetResultFullPathNameW = reinterpret_cast<EverythingApi::GetResultFullPathNameWFn>(
            load("Everything_GetResultFullPathNameW"));
        api.Reset = reinterpret_cast<EverythingApi::ResetFn>(load("Everything_Reset"));

        api.ready = api.SetSearchW && api.SetMax && api.SetRequestFlags && api.QueryW
            && api.GetLastError && api.GetNumResults && api.IsFileResult
            && api.GetResultFileNameW && api.GetResultFullPathNameW && api.Reset;
    });

    return api;
}

std::wstring BuildEverythingQuery(const std::wstring& rootPath, const std::wstring& nameFilter) {
    std::wstring rooted = rootPath;
    if (!rooted.empty() && rooted.back() != L'\\' && rooted.back() != L'/') {
        rooted.push_back(L'\\');
    }

    std::wstring search = L"\"";
    search += rooted;
    search += L"\"";
    if (!nameFilter.empty()) {
        search.push_back(L' ');
        search += nameFilter;
    }
    return search;
}

bool TrySearchWithEverything(
    const std::wstring& rootPath,
    const std::wstring& nameFilter,
    size_t maxResults,
    std::vector<FileSearchHit>& hits) {
    EverythingApi& api = GetEverythingApi();
    if (!api.ready || maxResults == 0) {
        return false;
    }

    api.Reset();
    api.SetMax(static_cast<DWORD>(maxResults * 4));
    api.SetRequestFlags(
        EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_PATH
        | EVERYTHING_REQUEST_FULL_PATH_AND_FILE_NAME);
    api.SetSearchW(BuildEverythingQuery(rootPath, nameFilter).c_str());

    if (!api.QueryW(TRUE)) {
        (void)api.GetLastError();
        return false;
    }

    const DWORD count = api.GetNumResults();
    wchar_t fullPathBuffer[MAX_PATH * 4] = {};

    for (DWORD index = 0; index < count && hits.size() < maxResults; ++index) {
        if (!api.IsFileResult(index)) {
            continue;
        }

        const wchar_t* fileName = api.GetResultFileNameW(index);
        if (!fileName || fileName[0] == L'\0') {
            continue;
        }
        if (!ContainsIgnoreCase(fileName, nameFilter)) {
            continue;
        }

        const DWORD written = api.GetResultFullPathNameW(
            index,
            fullPathBuffer,
            static_cast<DWORD>(sizeof(fullPathBuffer) / sizeof(fullPathBuffer[0])));
        if (written == 0
            || written >= static_cast<DWORD>(sizeof(fullPathBuffer) / sizeof(fullPathBuffer[0]))) {
            continue;
        }

        FileSearchHit hit;
        hit.fileName = fileName;
        hit.fullPath.assign(fullPathBuffer, written);
        hits.push_back(std::move(hit));
    }

    return true;
}

std::wstring ResolveScopeRoot(FileSearchScope scope) {
    switch (scope) {
    case FileSearchScope::Documents:
        return GetKnownFolderPath(FOLDERID_Documents);
    case FileSearchScope::Downloads:
        return GetKnownFolderPath(FOLDERID_Downloads);
    }
    return {};
}

}  // namespace

std::optional<FileSearchQuery> TryParseFileSearchQuery(const std::wstring& query) {
    const std::wstring trimmed = Trim(query);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (auto documents = TryParsePrefix(trimmed, L"doc", FileSearchScope::Documents)) {
        return documents;
    }
    if (auto downloads = TryParsePrefix(trimmed, L"dl", FileSearchScope::Downloads)) {
        return downloads;
    }
    return std::nullopt;
}

std::vector<FileSearchHit> SearchFilesInFolder(
    const std::wstring& rootPath,
    const std::wstring& nameFilter,
    size_t maxResults) {
    std::vector<FileSearchHit> hits;
    if (rootPath.empty() || maxResults == 0) {
        return hits;
    }
    hits.reserve(maxResults);
    SearchFilesRecursive(rootPath, nameFilter, maxResults, hits);
    return hits;
}

std::vector<FileSearchHit> SearchFiles(const FileSearchQuery& query, size_t maxResults) {
    const std::wstring rootPath = ResolveScopeRoot(query.scope);
    if (rootPath.empty() || maxResults == 0) {
        return {};
    }

    if (!g_forceWin32Fallback) {
        std::vector<FileSearchHit> everythingHits;
        everythingHits.reserve(maxResults);
        if (TrySearchWithEverything(rootPath, query.nameFilter, maxResults, everythingHits)) {
            return everythingHits;
        }
    }

    return SearchFilesInFolder(rootPath, query.nameFilter, maxResults);
}

void SetFileSearchForceWin32Fallback(bool force) {
    g_forceWin32Fallback = force;
}

}  // namespace nexus
