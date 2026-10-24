#pragma once

#include <optional>
#include <string>

namespace nexus {

struct WebAction {
    std::wstring label;
    std::wstring url;
};

// Parses prefix searches (g / yt / gh / so) and direct URL / host detection.
// Returns nullopt when the query is not a web action.
std::optional<WebAction> TryParseWebAction(const std::wstring& query);

}  // namespace nexus
