/// Parses unsigned decimal text shared by every strict text format.

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace core_engine {

/// Returns the value of one or more ASCII digits that fit in uint64, otherwise nullopt.
[[nodiscard]] inline std::optional<std::uint64_t> parse_decimal(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0U;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
      return std::nullopt;
    }
    value = value * 10U + digit;
  }
  return value;
}

}
