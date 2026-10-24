#include "Calculator.h"

#include <cmath>
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

void ExpectNear(double actual, double expected, double epsilon, const char* file, int line) {
    if (std::fabs(actual - expected) <= epsilon) {
        return;
    }
    ++g_failures;
    std::wcerr << L"FAIL " << file << L":" << line << L"  expected " << expected << L" got "
               << actual << L"\n";
}

#define EXPECT_TRUE(condition) ExpectTrue((condition), #condition, __FILE__, __LINE__)
#define EXPECT_EQ_WSTR(actual, expected) ExpectEqWString((actual), (expected), __FILE__, __LINE__)
#define EXPECT_NEAR(actual, expected, epsilon) \
    ExpectNear((actual), (expected), (epsilon), __FILE__, __LINE__)

void ExpectResult(const std::wstring& query, const std::wstring& formatted, double value) {
    const auto result = nexus::TryEvaluate(query);
    EXPECT_TRUE(result.has_value());
    if (!result) {
        return;
    }
    EXPECT_EQ_WSTR(result->formattedValue, formatted);
    EXPECT_NEAR(result->value, value, 1e-9);
}

void ExpectNoResult(const std::wstring& query) {
    if (nexus::TryEvaluate(query).has_value()) {
        ++g_failures;
        std::wcerr << L"FAIL expected no result for \"" << query << L"\"\n";
    }
}

void TestSuccessfulEvaluations() {
    ExpectResult(L"1+1", L"2", 2.0);
    ExpectResult(L"10*1", L"10", 10.0);
    ExpectResult(L"100 / 2 * .5 + 19", L"44", 44.0);
    ExpectResult(L"(3+2)*4", L"20", 20.0);
    ExpectResult(L"-3+5", L"2", 2.0);
}

void TestRejections() {
    ExpectNoResult(L"");
    ExpectNoResult(L"   ");
    ExpectNoResult(L"10");
    ExpectNoResult(L"chrome");
    ExpectNoResult(L"C++");
    ExpectNoResult(L"1+*");
    ExpectNoResult(L"1++");
    ExpectNoResult(L"1/0");
    ExpectNoResult(L"(1+2");
    ExpectNoResult(L"1+2)");
}

void TestLimits() {
    ExpectNoResult(std::wstring(nexus::kMaxExpressionLength + 1, L'1') + L"+1");

    std::wstring tooManyDigits(nexus::kMaxNumberDigits + 1, L'9');
    tooManyDigits += L"+1";
    ExpectNoResult(tooManyDigits);

    // Scientific notation is not supported (letters are rejected).
    ExpectNoResult(L"1e10+1");
    ExpectNoResult(L"1e308*10");

    // Within length/digit caps; still evaluates.
    ExpectResult(L"1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1", L"16", 16.0);

    std::wstring maxDigits(nexus::kMaxNumberDigits, L'1');
    ExpectResult(maxDigits + L"+0", maxDigits, 111111111111111.0);
}

}  // namespace

int RunWebSearchTests();

int wmain() {
    TestSuccessfulEvaluations();
    TestRejections();
    TestLimits();

    const int calculatorFailures = g_failures;
    const int webSearchFailures = RunWebSearchTests();
    const int totalFailures = calculatorFailures + webSearchFailures;

    if (totalFailures == 0) {
        std::wcout << L"All tests passed.\n";
        return 0;
    }

    std::wcerr << totalFailures << L" test failure(s).\n";
    return 1;
}
