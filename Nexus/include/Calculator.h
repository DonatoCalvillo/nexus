#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace nexus {

constexpr size_t kMaxExpressionLength = 64;
constexpr size_t kMaxNumberDigits = 15;

struct CalcResult {
    std::wstring expression;
    double value = 0.0;
    std::wstring formattedValue;
};

// Returns a result when query is a complete arithmetic expression with at least
// one binary operator (+ - * /). Supports parentheses, unary minus, and decimals.
// Rejects expressions longer than kMaxExpressionLength, literals with more than
// kMaxNumberDigits digits, and non-finite results.
std::optional<CalcResult> TryEvaluate(const std::wstring& query);

}  // namespace nexus
