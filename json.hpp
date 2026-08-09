#pragma once

#include <optional>
#include <string_view>

namespace json {
// Extracts a string-valued field, i.e. "key":"value" -> "value".
std::optional<std::string_view> FindString(std::string_view json, std::string_view key);

// Extracts a numeric field, i.e. "key":123.45 -> "123.45"
std::optional<std::string_view> FindNumber(std::string_view json, std::string_view key);

// Extracts the Nth element of an array, i.e. 0th element of {"data":[{"a":1}]} -> {"a":1}
std::optional<std::string_view> FindArrayElement(std::string_view json, std::string_view key,
                                                 std::size_t index);
}
