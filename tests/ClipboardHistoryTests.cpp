#include "ClipboardHistory.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

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

void ExpectQuery(const std::wstring& query, const std::wstring& filter) {
    const auto parsed = nexus::TryParseClipboardQuery(query);
    if (!parsed) {
        ++g_failures;
        std::wcerr << L"FAIL expected clipboard query for \"" << query << L"\"\n";
        return;
    }
    if (parsed->filter != filter) {
        ++g_failures;
        std::wcerr << L"FAIL filter for \"" << query << L"\" expected \"" << filter << L"\" got \""
                   << parsed->filter << L"\"\n";
    }
}

void ExpectNoQuery(const std::wstring& query) {
    if (nexus::TryParseClipboardQuery(query).has_value()) {
        ++g_failures;
        std::wcerr << L"FAIL expected no clipboard query for \"" << query << L"\"\n";
    }
}

void TestParseAccepts() {
    ExpectQuery(L"clip", L"");
    ExpectQuery(L"cb", L"");
    ExpectQuery(L" CLIP ", L"");
    ExpectQuery(L"Cb", L"");
    ExpectQuery(L"clip foo", L"foo");
    ExpectQuery(L"CLIP Foo Bar", L"Foo Bar");
    ExpectQuery(L"cb bar", L"bar");
    ExpectQuery(L"  cb   hello  ", L"hello");
    ExpectQuery(L"clip ", L"");
    ExpectQuery(L"cb\t", L"");
}

void TestParseRejects() {
    ExpectNoQuery(L"");
    ExpectNoQuery(L"   ");
    ExpectNoQuery(L"clipboard");
    ExpectNoQuery(L"clips");
    ExpectNoQuery(L"cbx");
    ExpectNoQuery(L"c");
    ExpectNoQuery(L"cli");
    ExpectNoQuery(L"clipping");
    ExpectNoQuery(L"x clip");
    ExpectNoQuery(L"kill chrome");
}

void TestPushIgnoresEmptyAndTruncates() {
    nexus::ClipboardHistoryStore store;
    store.Push(L"");
    EXPECT_TRUE(store.Size() == 0);

    std::wstring longText(nexus::kMaxClipboardEntryChars + 50, L'a');
    store.Push(longText);
    EXPECT_TRUE(store.Size() == 1);
    const auto matches = store.Match(L"");
    EXPECT_TRUE(matches.size() == 1);
    EXPECT_TRUE(matches[0].size() == nexus::kMaxClipboardEntryChars);
}

void TestPushCapAndMru() {
    nexus::ClipboardHistoryStore store;
    for (size_t index = 0; index < nexus::kMaxClipboardEntries + 5; ++index) {
        store.Push(L"item-" + std::to_wstring(index));
    }
    EXPECT_TRUE(store.Size() == nexus::kMaxClipboardEntries);

    const auto all = store.Match(L"");
    EXPECT_TRUE(all.size() == nexus::kMaxClipboardEntries);
    EXPECT_TRUE(all.front() == L"item-" + std::to_wstring(nexus::kMaxClipboardEntries + 4));
    EXPECT_TRUE(all.back() == L"item-5");

    store.Push(L"item-10");
    EXPECT_TRUE(store.Size() == nexus::kMaxClipboardEntries);
    const auto afterMru = store.Match(L"");
    EXPECT_TRUE(afterMru.front() == L"item-10");

    size_t countItem10 = 0;
    for (const auto& entry : afterMru) {
        if (entry == L"item-10") {
            ++countItem10;
        }
    }
    EXPECT_TRUE(countItem10 == 1);
}

void TestMatchFilter() {
    nexus::ClipboardHistoryStore store;
    store.Push(L"Hello World");
    store.Push(L"goodbye");
    store.Push(L"HELLO again");

    const auto all = store.Match(L"");
    EXPECT_TRUE(all.size() == 3);
    EXPECT_TRUE(all[0] == L"HELLO again");
    EXPECT_TRUE(all[1] == L"goodbye");
    EXPECT_TRUE(all[2] == L"Hello World");

    const auto hello = store.Match(L"hello");
    EXPECT_TRUE(hello.size() == 2);
    EXPECT_TRUE(hello[0] == L"HELLO again");
    EXPECT_TRUE(hello[1] == L"Hello World");

    const auto none = store.Match(L"missing");
    EXPECT_TRUE(none.empty());

    store.Clear();
    EXPECT_TRUE(store.Size() == 0);
    EXPECT_TRUE(store.Match(L"").empty());
}

}  // namespace

int RunClipboardHistoryTests() {
    g_failures = 0;
    TestParseAccepts();
    TestParseRejects();
    TestPushIgnoresEmptyAndTruncates();
    TestPushCapAndMru();
    TestMatchFilter();
    return g_failures;
}
