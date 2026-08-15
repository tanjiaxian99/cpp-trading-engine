#pragma once

#include <functional>
#include <optional>
#include <string_view>

namespace json {
// Extracts a string-valued field, i.e. "key":"value" -> "value".
std::optional<std::string_view> FindString(std::string_view json, std::string_view key);

// Extracts a numeric field, i.e. "key":123.45 -> "123.45"
std::optional<std::string_view> FindNumber(std::string_view json, std::string_view key);

// Extracts the Nth element of an array at a key,
// i.e. 0th element of key "data" of {"data":[{"a":1}]} -> {"a":1}
std::optional<std::string_view> FindArrayElement(std::string_view json, std::string_view key,
                                                 std::size_t index);

// Extracts the Nth element from an array without key lookup
// i.e. index 1 of ["a","b","c"] -> "b"
std::optional<std::string_view> FindElement(std::string_view json_array, std::size_t index);

// Loop through every array element at the key
void ForEachArrayElement(std::string_view json, std::string_view key,
                         const std::function<void(std::string_view)>& action);

// Parses a JSON decimal number, e.g. "123.45" -> 123.45
double ParseDouble(std::string_view text);

// Parses a JSON numeric, e.g. "12345" -> 12345
long long ParseLL(std::string_view text);
}  // namespace json
