#include "StringUtils.h"

#include <algorithm>
#include <cwctype>

namespace nexus {

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

}  // namespace nexus
