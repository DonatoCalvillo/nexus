#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus {

constexpr size_t kMaxClipboardEntries = 20;
constexpr size_t kMaxClipboardEntryChars = 4096;

struct ClipboardQuery {
    std::wstring filter;
};

// Parses clip / cb, optionally followed by a case-insensitive substring filter.
// Returns nullopt when the query is not a clipboard command.
std::optional<ClipboardQuery> TryParseClipboardQuery(const std::wstring& query);

// In-memory MRU store of recent clipboard texts (newest at front).
class ClipboardHistoryStore {
public:
    void Push(std::wstring text);
    std::vector<std::wstring> Match(std::wstring_view filter) const;
    void Clear();
    size_t Size() const;

private:
    std::deque<std::wstring> entries_;
};

}  // namespace nexus
