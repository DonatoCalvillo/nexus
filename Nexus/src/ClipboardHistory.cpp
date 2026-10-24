#include "ClipboardHistory.h"

#include <cwchar>
#include <cwctype>
#include <utility>

namespace nexus {
namespace {

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

bool ContainsIgnoreCase(std::wstring_view haystack, std::wstring_view needle) {
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

std::optional<ClipboardQuery> TryParsePrefix(
    const std::wstring& trimmed,
    const wchar_t* prefix) {
    const size_t prefixLength = wcslen(prefix);
    if (trimmed.size() == prefixLength && EqualsIgnoreCase(trimmed, prefix)) {
        return ClipboardQuery{};
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

    ClipboardQuery query;
    query.filter = Trim(trimmed.substr(prefixLength + 1));
    return query;
}

}  // namespace

std::optional<ClipboardQuery> TryParseClipboardQuery(const std::wstring& query) {
    const std::wstring trimmed = Trim(query);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (auto clip = TryParsePrefix(trimmed, L"clip")) {
        return clip;
    }
    return TryParsePrefix(trimmed, L"cb");
}

void ClipboardHistoryStore::Push(std::wstring text) {
    if (text.empty()) {
        return;
    }

    if (text.size() > kMaxClipboardEntryChars) {
        text.resize(kMaxClipboardEntryChars);
    }

    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (*it == text) {
            entries_.erase(it);
            break;
        }
    }

    entries_.push_front(std::move(text));
    while (entries_.size() > kMaxClipboardEntries) {
        entries_.pop_back();
    }
}

std::vector<std::wstring> ClipboardHistoryStore::Match(std::wstring_view filter) const {
    std::vector<std::wstring> matches;
    matches.reserve(entries_.size());
    for (const std::wstring& entry : entries_) {
        if (ContainsIgnoreCase(entry, filter)) {
            matches.push_back(entry);
        }
    }
    return matches;
}

void ClipboardHistoryStore::Clear() {
    entries_.clear();
}

size_t ClipboardHistoryStore::Size() const {
    return entries_.size();
}

}  // namespace nexus
