#include "WebSearch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwctype>
#include <string>

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

bool EndsWithIgnoreCase(const std::wstring& value, const wchar_t* suffix) {
    const size_t suffixLength = wcslen(suffix);
    if (value.size() < suffixLength) {
        return false;
    }
    const size_t start = value.size() - suffixLength;
    for (size_t index = 0; index < suffixLength; ++index) {
        if (towlower(value[start + index]) != towlower(suffix[index])) {
            return false;
        }
    }
    return true;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        utf8.data(),
        size,
        nullptr,
        nullptr);
    return utf8;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }

    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        wide.data(),
        size);
    return wide;
}

std::wstring UrlEncode(const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    std::string encoded;
    encoded.reserve(utf8.size() * 3);

    for (unsigned char byte : utf8) {
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.'
            || byte == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(byte));
        } else if (byte == ' ') {
            encoded.push_back('+');
        } else {
            static constexpr char kHex[] = "0123456789ABCDEF";
            encoded.push_back('%');
            encoded.push_back(kHex[(byte >> 4) & 0x0F]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
    }

    return Utf8ToWide(encoded);
}

struct PrefixRule {
    const wchar_t* prefix;
    const wchar_t* label;
    const wchar_t* urlPrefix;
};

constexpr PrefixRule kPrefixRules[] = {
    {L"g", L"Buscar en Google", L"https://www.google.com/search?q="},
    {L"yt", L"Buscar en YouTube", L"https://www.youtube.com/results?search_query="},
    {L"gh", L"Buscar en GitHub", L"https://github.com/search?q="},
    {L"so", L"Buscar en Stack Overflow", L"https://stackoverflow.com/search?q="},
};

std::optional<WebAction> TryParsePrefixSearch(const std::wstring& trimmed) {
    for (const PrefixRule& rule : kPrefixRules) {
        const size_t prefixLength = wcslen(rule.prefix);
        if (trimmed.size() <= prefixLength) {
            continue;
        }
        if (!StartsWithIgnoreCase(trimmed, rule.prefix)) {
            continue;
        }
        if (!iswspace(trimmed[prefixLength])) {
            continue;
        }

        const std::wstring remainder = Trim(trimmed.substr(prefixLength + 1));
        if (remainder.empty()) {
            return std::nullopt;
        }

        WebAction action;
        action.label = rule.label;
        action.url = std::wstring(rule.urlPrefix) + UrlEncode(remainder);
        return action;
    }

    return std::nullopt;
}

bool LooksLikeLocalhost(const std::wstring& host) {
    if (EqualsIgnoreCase(host, L"localhost")) {
        return true;
    }
    if (!StartsWithIgnoreCase(host, L"localhost:")) {
        return false;
    }
    if (host.size() <= 10) {
        return false;
    }
    for (size_t index = 10; index < host.size(); ++index) {
        if (!iswdigit(host[index])) {
            return false;
        }
    }
    return true;
}

bool HasKnownTld(const std::wstring& host) {
    static constexpr const wchar_t* kTlds[] = {
        L".com",
        L".dev",
        L".org",
        L".net",
        L".io",
        L".app",
    };
    for (const wchar_t* tld : kTlds) {
        if (EndsWithIgnoreCase(host, tld)) {
            return true;
        }
    }
    return false;
}

bool IsValidHostChar(wchar_t character) {
    return iswalnum(character) || character == L'.' || character == L'-' || character == L':'
        || character == L'_';
}

bool LooksLikeHost(const std::wstring& value) {
    if (value.empty() || value.find(L' ') != std::wstring::npos) {
        return false;
    }
    for (wchar_t character : value) {
        if (!IsValidHostChar(character)) {
            return false;
        }
    }
    return LooksLikeLocalhost(value) || HasKnownTld(value);
}

std::optional<WebAction> TryParseDirectUrl(const std::wstring& trimmed) {
    if (StartsWithIgnoreCase(trimmed, L"http://") || StartsWithIgnoreCase(trimmed, L"https://")) {
        if (trimmed.find(L' ') != std::wstring::npos) {
            return std::nullopt;
        }
        WebAction action;
        action.label = L"Abrir URL";
        action.url = trimmed;
        return action;
    }

    if (!LooksLikeHost(trimmed)) {
        return std::nullopt;
    }

    WebAction action;
    action.label = L"Abrir URL";
    action.url = L"https://" + trimmed;
    return action;
}

}  // namespace

std::optional<WebAction> TryParseWebAction(const std::wstring& query) {
    const std::wstring trimmed = Trim(query);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (auto prefixAction = TryParsePrefixSearch(trimmed)) {
        return prefixAction;
    }

    return TryParseDirectUrl(trimmed);
}

}  // namespace nexus
