#include "FileSearch.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

int g_failures = 0;

void ExpectTrue(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }
    ++g_failures;
    std::wcerr << L"FAIL " << file << L":" << line << L"  EXPECT_TRUE(" << expression << L")\n";
}

#define EXPECT_TRUE(condition) ExpectTrue((condition), #condition, __FILE__, __LINE__)

void ExpectQuery(
    const std::wstring& query,
    nexus::FileSearchScope scope,
    const std::wstring& filter) {
    const auto parsed = nexus::TryParseFileSearchQuery(query);
    if (!parsed) {
        ++g_failures;
        std::wcerr << L"FAIL expected file search query for \"" << query << L"\"\n";
        return;
    }
    if (parsed->scope != scope) {
        ++g_failures;
        std::wcerr << L"FAIL scope mismatch for \"" << query << L"\"\n";
    }
    if (parsed->nameFilter != filter) {
        ++g_failures;
        std::wcerr << L"FAIL filter for \"" << query << L"\" expected \"" << filter << L"\" got \""
                   << parsed->nameFilter << L"\"\n";
    }
}

void ExpectNoQuery(const std::wstring& query) {
    if (nexus::TryParseFileSearchQuery(query).has_value()) {
        ++g_failures;
        std::wcerr << L"FAIL expected no file search query for \"" << query << L"\"\n";
    }
}

void TestParseAccepts() {
    ExpectQuery(L"doc", nexus::FileSearchScope::Documents, L"");
    ExpectQuery(L"dl", nexus::FileSearchScope::Downloads, L"");
    ExpectQuery(L" DOC ", nexus::FileSearchScope::Documents, L"");
    ExpectQuery(L"Dl", nexus::FileSearchScope::Downloads, L"");
    ExpectQuery(L"doc report", nexus::FileSearchScope::Documents, L"report");
    ExpectQuery(L"DOC Foo Bar", nexus::FileSearchScope::Documents, L"Foo Bar");
    ExpectQuery(L"dl invoice.pdf", nexus::FileSearchScope::Downloads, L"invoice.pdf");
    ExpectQuery(L"  dl   hello  ", nexus::FileSearchScope::Downloads, L"hello");
    ExpectQuery(L"doc ", nexus::FileSearchScope::Documents, L"");
    ExpectQuery(L"dl\t", nexus::FileSearchScope::Downloads, L"");
}

void TestParseRejects() {
    ExpectNoQuery(L"");
    ExpectNoQuery(L"   ");
    ExpectNoQuery(L"document");
    ExpectNoQuery(L"docs");
    ExpectNoQuery(L"docx");
    ExpectNoQuery(L"download");
    ExpectNoQuery(L"downloads");
    ExpectNoQuery(L"d");
    ExpectNoQuery(L"do");
    ExpectNoQuery(L"x doc");
    ExpectNoQuery(L"clip");
    ExpectNoQuery(L"g query");
}

std::wstring MakeTempDirectory() {
    wchar_t tempRoot[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, tempRoot);
    EXPECT_TRUE(length > 0 && length < MAX_PATH);

    wchar_t uniquePath[MAX_PATH] = {};
    EXPECT_TRUE(GetTempFileNameW(tempRoot, L"nxf", 0, uniquePath) != 0);
    DeleteFileW(uniquePath);
    EXPECT_TRUE(CreateDirectoryW(uniquePath, nullptr) != 0);
    return uniquePath;
}

bool WriteTextFile(const std::wstring& path, const char* contents) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream << contents;
    return static_cast<bool>(stream);
}

void RemoveTree(const std::wstring& path) {
    const std::wstring pattern = path + L"\\*";
    WIN32_FIND_DATAW findData = {};
    const HANDLE findHandle = FindFirstFileW(pattern.c_str(), &findData);
    if (findHandle != INVALID_HANDLE_VALUE) {
        do {
            const wchar_t* name = findData.cFileName;
            if (name[0] == L'.'
                && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
                continue;
            }
            const std::wstring child = path + L'\\' + name;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(child);
            } else {
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(findHandle, &findData));
        FindClose(findHandle);
    }
    RemoveDirectoryW(path.c_str());
}

void TestWin32FolderSearch() {
    const std::wstring root = MakeTempDirectory();
    const std::wstring nested = root + L"\\nested";
    EXPECT_TRUE(CreateDirectoryW(nested.c_str(), nullptr) != 0);
    EXPECT_TRUE(WriteTextFile(root + L"\\ReportFinal.txt", "a"));
    EXPECT_TRUE(WriteTextFile(nested + L"\\report-draft.md", "b"));
    EXPECT_TRUE(WriteTextFile(root + L"\\notes.txt", "c"));
    EXPECT_TRUE(WriteTextFile(root + L"\\other.bin", "d"));

    const auto reportHits = nexus::SearchFilesInFolder(root, L"report", 5);
    EXPECT_TRUE(reportHits.size() == 2);

    bool foundFinal = false;
    bool foundDraft = false;
    for (const auto& hit : reportHits) {
        if (hit.fileName == L"ReportFinal.txt") {
            foundFinal = true;
            EXPECT_TRUE(hit.fullPath == root + L"\\ReportFinal.txt");
        }
        if (hit.fileName == L"report-draft.md") {
            foundDraft = true;
            EXPECT_TRUE(hit.fullPath == nested + L"\\report-draft.md");
        }
    }
    EXPECT_TRUE(foundFinal);
    EXPECT_TRUE(foundDraft);

    const auto limited = nexus::SearchFilesInFolder(root, L"", 1);
    EXPECT_TRUE(limited.size() == 1);

    const auto none = nexus::SearchFilesInFolder(root, L"missing-xyz", 5);
    EXPECT_TRUE(none.empty());

    RemoveTree(root);
}

void TestSearchFilesEarlyExit() {
    nexus::SetFileSearchForceWin32Fallback(true);

    nexus::FileSearchQuery query;
    query.scope = nexus::FileSearchScope::Documents;
    query.nameFilter = L"x";
    // maxResults == 0 must not walk known folders.
    const auto hits = nexus::SearchFiles(query, 0);
    EXPECT_TRUE(hits.empty());

    nexus::SetFileSearchForceWin32Fallback(false);
}

}  // namespace

int RunFileSearchTests() {
    g_failures = 0;
    TestParseAccepts();
    TestParseRejects();
    TestWin32FolderSearch();
    TestSearchFilesEarlyExit();
    return g_failures;
}
