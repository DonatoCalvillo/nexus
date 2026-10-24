#include "WebSearch.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void ExpectTrue(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }
    ++g_failures;
    std::wcerr << L"FAIL " << file << L":" << line << L"  EXPECT_TRUE(" << expression << L")\n";
}

void ExpectEqWString(
    const std::wstring& actual,
    const std::wstring& expected,
    const char* file,
    int line) {
    if (actual == expected) {
        return;
    }
    ++g_failures;
    std::wcerr << L"FAIL " << file << L":" << line << L"  expected \"" << expected
               << L"\" got \"" << actual << L"\"\n";
}

#define EXPECT_TRUE(condition) ExpectTrue((condition), #condition, __FILE__, __LINE__)
#define EXPECT_EQ_WSTR(actual, expected) ExpectEqWString((actual), (expected), __FILE__, __LINE__)

void ExpectAction(
    const std::wstring& query,
    const std::wstring& label,
    const std::wstring& url) {
    const auto action = nexus::TryParseWebAction(query);
    EXPECT_TRUE(action.has_value());
    if (!action) {
        return;
    }
    EXPECT_EQ_WSTR(action->label, label);
    EXPECT_EQ_WSTR(action->url, url);
}

void ExpectNoAction(const std::wstring& query) {
    if (nexus::TryParseWebAction(query).has_value()) {
        ++g_failures;
        std::wcerr << L"FAIL expected no web action for \"" << query << L"\"\n";
    }
}

void TestPrefixSearches() {
    ExpectAction(
        L"g rust",
        L"Buscar en Google",
        L"https://www.google.com/search?q=rust");
    ExpectAction(
        L"G hello world",
        L"Buscar en Google",
        L"https://www.google.com/search?q=hello+world");
    ExpectAction(
        L"yt lo-fi",
        L"Buscar en YouTube",
        L"https://www.youtube.com/results?search_query=lo-fi");
    ExpectAction(
        L"gh nexus",
        L"Buscar en GitHub",
        L"https://github.com/search?q=nexus");
    ExpectAction(
        L"so segfault",
        L"Buscar en Stack Overflow",
        L"https://stackoverflow.com/search?q=segfault");
}

void TestPrefixRejections() {
    ExpectNoAction(L"");
    ExpectNoAction(L"   ");
    ExpectNoAction(L"g");
    ExpectNoAction(L"g ");
    ExpectNoAction(L"yt");
    ExpectNoAction(L"chrome");
    ExpectNoAction(L"google rust");
}

void TestDirectUrls() {
    ExpectAction(
        L"https://example.com",
        L"Abrir URL",
        L"https://example.com");
    ExpectAction(
        L"http://example.com/path",
        L"Abrir URL",
        L"http://example.com/path");
    ExpectAction(
        L"example.com",
        L"Abrir URL",
        L"https://example.com");
    ExpectAction(
        L"docs.example.dev",
        L"Abrir URL",
        L"https://docs.example.dev");
    ExpectAction(
        L"localhost",
        L"Abrir URL",
        L"https://localhost");
    ExpectAction(
        L"localhost:3000",
        L"Abrir URL",
        L"https://localhost:3000");
}

void TestUrlRejections() {
    ExpectNoAction(L"not a url");
    ExpectNoAction(L"example.xyz");
    ExpectNoAction(L"https://example.com/with space");
}

void TestEncoding() {
    ExpectAction(
        L"g c++",
        L"Buscar en Google",
        L"https://www.google.com/search?q=c%2B%2B");
}

}  // namespace

int RunWebSearchTests() {
    g_failures = 0;
    TestPrefixSearches();
    TestPrefixRejections();
    TestDirectUrls();
    TestUrlRejections();
    TestEncoding();
    return g_failures;
}
