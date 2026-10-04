#pragma once

#include <charconv>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

namespace chess {

// Parses a whole string as a base-10 integer; nullopt if any character is left over.
[[nodiscard]] inline std::optional<int> parse_int(std::string_view text) {
    int value = 0;
    const char* const last = std::to_address(text.end());
    const auto [ptr, error] = std::from_chars(std::to_address(text.begin()), last, value);
    if (error != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

}  // namespace chess
