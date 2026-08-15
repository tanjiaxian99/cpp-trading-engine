#include "util/json.hpp"

#include <charconv>
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>

namespace json {
namespace {
bool IsJsonWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::size_t SkipWhitespace(std::string_view text, std::size_t pos) {
    while (pos < text.size() && IsJsonWhitespace(text[pos])) {
        pos++;
    }
    return pos;
}

// Skips over the current json value, including nested objects/arrays and strings
std::size_t SkipValue(std::string_view json, std::size_t pos) {
    pos = SkipWhitespace(json, pos);
    if (pos >= json.size()) {
        return pos;
    }

    if (json[pos] == '"') {
        pos++;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\') {
                pos++;  // Also skip the character the backslash escapes
            }
            pos++;
        }
        return pos + 1;  // Skip past the closing quote
    }

    if (json[pos] == '{' || json[pos] == '[') {
        const char open = json[pos];
        const char close = (open == '{') ? '}' : ']';
        int depth = 1;
        pos++;
        while (pos < json.size() && depth > 0) {
            if (json[pos] == '"') {
                pos = SkipValue(json, pos);
                continue;
            }
            if (json[pos] == open) {
                depth++;
            } else if (json[pos] == close) {
                depth--;
            }
            pos++;
        }
        return pos;
    }

    // Skips past values until a structural delimiter is reached
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
           !IsJsonWhitespace(json[pos])) {
        pos++;
    }
    return pos;
}

std::size_t FindValueStart(std::string_view json, std::string_view key) {
    const std::string pattern = std::format("\"{}\"", key);
    std::size_t search_from = 0;
    while (true) {
        const std::size_t key_pos = json.find(pattern, search_from);
        if (key_pos == std::string_view::npos) {
            return std::string_view::npos;
        }

        const std::size_t after_key = SkipWhitespace(json, key_pos + pattern.size());
        if (after_key < json.size() && json[after_key] == ':') {
            return SkipWhitespace(json, after_key + 1);
        }
        // The matched text is found inside some other string value, so we continue looking
        search_from = key_pos + pattern.size();
    }
}

// Shared by FindArrayElement and FindElement: `array` starts at the array's
// opening '[', and this walks past `index` elements to return the next one.
std::optional<std::string_view> FindElementAt(std::string_view array, std::size_t index) {
    std::size_t pos = 1;
    for (std::size_t i = 0; i < index; i++) {
        pos = SkipWhitespace(array, pos);
        if (pos >= array.size() || array[pos] == ']') {
            return std::nullopt;
        }

        pos = SkipValue(array, pos);
        pos = SkipWhitespace(array, pos);
        if (pos < array.size() && array[pos] == ',') {
            pos++;
        }
    }

    pos = SkipWhitespace(array, pos);
    if (pos >= array.size() || array[pos] == ']') {
        return std::nullopt;
    }

    const std::size_t element_start = pos;
    const std::size_t element_end = SkipValue(array, pos);
    return array.substr(element_start, element_end - element_start);
}
}  // namespace

std::optional<std::string_view> FindString(std::string_view json, std::string_view key) {
    const std::size_t value_start = FindValueStart(json, key);
    if (value_start == std::string_view::npos || value_start >= json.size() ||
        json[value_start] != '"') {
        return std::nullopt;
    }

    const std::size_t content_start = value_start + 1;
    const std::size_t content_end = json.find('"', content_start);
    if (content_end == std::string_view::npos) {
        return std::nullopt;
    }

    return json.substr(content_start, content_end - content_start);
}

std::optional<std::string_view> FindNumber(std::string_view json, std::string_view key) {
    const std::size_t value_start = FindValueStart(json, key);
    if (value_start == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t pos = value_start;
    while (pos < json.size()) {
        const auto c = static_cast<unsigned char>(json[pos]);
        if (std::isdigit(c) == 0 && json[pos] != '-' && json[pos] != '+' && json[pos] != '.' &&
            json[pos] != 'e' && json[pos] != 'E') {
            break;
        }
        pos++;
    }

    if (pos == value_start) {
        return std::nullopt;
    }

    return json.substr(value_start, pos - value_start);
}

std::optional<std::string_view> FindArrayElement(std::string_view json, std::string_view key,
                                                 std::size_t index) {
    const std::size_t value_start = FindValueStart(json, key);
    if (value_start == std::string_view::npos || value_start >= json.size() ||
        json[value_start] != '[') {
        return std::nullopt;
    }

    return FindElementAt(json.substr(value_start), index);
}

std::optional<std::string_view> FindElement(std::string_view json_array, std::size_t index) {
    if (json_array.empty() || json_array.front() != '[') {
        return std::nullopt;
    }

    return FindElementAt(json_array, index);
}

void ForEachArrayElement(std::string_view json, std::string_view key,
                         const std::function<void(std::string_view)>& action) {
    const std::size_t value_start = FindValueStart(json, key);
    if (value_start == std::string_view::npos || value_start >= json.size() ||
        json[value_start] != '[') {
        return;
    }

    const std::string_view array = json.substr(value_start);
    std::size_t pos = 1;  // Past the opening '['.
    while (true) {
        pos = SkipWhitespace(array, pos);
        if (pos >= array.size() || array[pos] == ']') {
            return;
        }

        const std::size_t element_start = pos;
        pos = SkipValue(array, pos);
        action(array.substr(element_start, pos - element_start));

        pos = SkipWhitespace(array, pos);
        if (pos < array.size() && array[pos] == ',') {
            pos++;
        }
    }
}

double ParseDouble(std::string_view text) {
    char* end = nullptr;
    const double value =
        std::strtod(text.data(), &end);  // NOLINT(bugprone-suspicious-stringview-data-usage)
    if (end == text.data()) {
        throw std::runtime_error("Failed to parse double");
    }
    return value;
}

long long ParseLL(std::string_view text) {
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("Failed to parse long long");
    }
    return value;
}
}  // namespace json