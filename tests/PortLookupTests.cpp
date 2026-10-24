#include "PortLookup.h"

#include <cstdint>
#include <iostream>
#include <optional>
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

void ExpectEqPort(
    std::optional<std::uint16_t> actual,
    std::uint16_t expected,
    const char* file,
    int line) {
    if (actual && *actual == expected) {
        return;
    }
    ++g_failures;
    std::wcerr << L"FAIL " << file << L":" << line << L"  expected port " << expected;
    if (!actual) {
        std::wcerr << L" got nullopt\n";
    } else {
        std::wcerr << L" got " << *actual << L"\n";
    }
}

#define EXPECT_TRUE(condition) ExpectTrue((condition), #condition, __FILE__, __LINE__)
#define EXPECT_EQ_PORT(actual, expected) ExpectEqPort((actual), (expected), __FILE__, __LINE__)

void ExpectPort(const std::wstring& query, std::uint16_t port) {
    EXPECT_EQ_PORT(nexus::TryParsePortQuery(query), port);
}

void ExpectNoPort(const std::wstring& query) {
    if (nexus::TryParsePortQuery(query).has_value()) {
        ++g_failures;
        std::wcerr << L"FAIL expected no port query for \"" << query << L"\"\n";
    }
}

void TestAcceptedQueries() {
    ExpectPort(L"-p 3000", 3000);
    ExpectPort(L" -P 80 ", 80);
    ExpectPort(L"-p\t443", 443);
    ExpectPort(L"-p 1", 1);
    ExpectPort(L"-p 65535", 65535);
}

void TestRejectedQueries() {
    ExpectNoPort(L"");
    ExpectNoPort(L"   ");
    ExpectNoPort(L"-p");
    ExpectNoPort(L"-p ");
    ExpectNoPort(L"-p abc");
    ExpectNoPort(L"-p 0");
    ExpectNoPort(L"-p 65536");
    ExpectNoPort(L"-p 3000 extra");
    ExpectNoPort(L"p 3000");
    ExpectNoPort(L"--p 3000");
    ExpectNoPort(L"-port 3000");
    ExpectNoPort(L"g 3000");
}

}  // namespace

int RunPortLookupTests() {
    g_failures = 0;
    TestAcceptedQueries();
    TestRejectedQueries();
    return g_failures;
}
