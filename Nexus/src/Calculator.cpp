#include "Calculator.h"

#include <cmath>
#include <cwctype>
#include <string>

namespace nexus {
namespace {

bool IsAllowedChar(wchar_t character) {
    return iswdigit(character) || character == L'.' || character == L'+' || character == L'-'
        || character == L'*' || character == L'/' || character == L'(' || character == L')'
        || iswspace(character);
}

bool LooksLikeMathQuery(const std::wstring& query) {
    if (query.empty()) {
        return false;
    }

    for (wchar_t character : query) {
        if (!IsAllowedChar(character)) {
            return false;
        }
    }

    return true;
}

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

std::wstring FormatValue(double value) {
    if (!std::isfinite(value)) {
        return {};
    }

    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9 && std::fabs(rounded) < 1e15) {
        return std::to_wstring(static_cast<long long>(rounded));
    }

    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%.10g", value);
    return buffer;
}

bool EnsureFinite(double value, bool& ok) {
    if (!ok || !std::isfinite(value)) {
        ok = false;
        return false;
    }
    return true;
}

class ExpressionParser {
public:
    explicit ExpressionParser(const std::wstring& source) : source_(source) {}

    std::optional<double> Parse() {
        const double value = ParseExpression();
        SkipSpaces();
        if (!ok_ || index_ != source_.size() || !sawBinaryOperator_) {
            return std::nullopt;
        }
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

private:
    const std::wstring& source_;
    size_t index_ = 0;
    bool ok_ = true;
    bool sawBinaryOperator_ = false;

    void SkipSpaces() {
        while (index_ < source_.size() && iswspace(source_[index_])) {
            ++index_;
        }
    }

    bool Match(wchar_t character) {
        SkipSpaces();
        if (index_ < source_.size() && source_[index_] == character) {
            ++index_;
            return true;
        }
        return false;
    }

    double ParseExpression() {
        double left = ParseTerm();
        while (ok_) {
            if (Match(L'+')) {
                sawBinaryOperator_ = true;
                left += ParseTerm();
                if (!EnsureFinite(left, ok_)) {
                    return 0.0;
                }
            } else if (Match(L'-')) {
                sawBinaryOperator_ = true;
                left -= ParseTerm();
                if (!EnsureFinite(left, ok_)) {
                    return 0.0;
                }
            } else {
                break;
            }
        }
        return left;
    }

    double ParseTerm() {
        double left = ParseFactor();
        while (ok_) {
            if (Match(L'*')) {
                sawBinaryOperator_ = true;
                left *= ParseFactor();
                if (!EnsureFinite(left, ok_)) {
                    return 0.0;
                }
            } else if (Match(L'/')) {
                sawBinaryOperator_ = true;
                const double right = ParseFactor();
                if (!ok_ || right == 0.0) {
                    ok_ = false;
                    return 0.0;
                }
                left /= right;
                if (!EnsureFinite(left, ok_)) {
                    return 0.0;
                }
            } else {
                break;
            }
        }
        return left;
    }

    double ParseFactor() {
        SkipSpaces();
        if (Match(L'+')) {
            return ParseFactor();
        }
        if (Match(L'-')) {
            const double value = -ParseFactor();
            if (!EnsureFinite(value, ok_)) {
                return 0.0;
            }
            return value;
        }
        if (Match(L'(')) {
            const double value = ParseExpression();
            if (!Match(L')')) {
                ok_ = false;
                return 0.0;
            }
            return value;
        }
        return ParseNumber();
    }

    double ParseNumber() {
        SkipSpaces();
        if (index_ >= source_.size()) {
            ok_ = false;
            return 0.0;
        }

        const size_t start = index_;
        bool sawDigit = false;
        bool sawDot = false;
        size_t digitCount = 0;

        while (index_ < source_.size()) {
            const wchar_t character = source_[index_];
            if (iswdigit(character)) {
                ++digitCount;
                if (digitCount > kMaxNumberDigits) {
                    ok_ = false;
                    return 0.0;
                }
                sawDigit = true;
                ++index_;
            } else if (character == L'.' && !sawDot) {
                sawDot = true;
                ++index_;
            } else {
                break;
            }
        }

        if (!sawDigit || start == index_) {
            ok_ = false;
            return 0.0;
        }

        try {
            const double value = std::stod(source_.substr(start, index_ - start));
            if (!EnsureFinite(value, ok_)) {
                return 0.0;
            }
            return value;
        } catch (...) {
            ok_ = false;
            return 0.0;
        }
    }
};

}  // namespace

std::optional<CalcResult> TryEvaluate(const std::wstring& query) {
    const std::wstring trimmed = Trim(query);
    if (trimmed.size() > kMaxExpressionLength) {
        return std::nullopt;
    }
    if (!LooksLikeMathQuery(trimmed)) {
        return std::nullopt;
    }

    ExpressionParser parser(trimmed);
    const std::optional<double> value = parser.Parse();
    if (!value) {
        return std::nullopt;
    }

    const std::wstring formatted = FormatValue(*value);
    if (formatted.empty()) {
        return std::nullopt;
    }

    CalcResult result;
    result.expression = trimmed;
    result.value = *value;
    result.formattedValue = formatted;
    return result;
}

}  // namespace nexus
