#include "okx/market_data.hpp"

#include <charconv>
#include <cstdlib>
#include <format>
#include <stdexcept>

#include "okx/okx_constants.hpp"
#include "util/json.hpp"

namespace {
constexpr char kDoubleQuote = '"';

double ParseDouble(std::string_view text) {
    char* end = nullptr;
    const double value =
        std::strtod(text.data(), &end);  // NOLINT(bugprone-suspicious-stringview-data-usage)
    if (end == text.data()) {
        throw std::runtime_error("Failed to parse double order book field");
    }
    return value;
}

long long ParseLL(std::string_view text) {
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("Failed to parse long long order book field");
    }
    return value;
}

std::string_view StripQuotes(std::string_view text) {
    if (text.size() >= 2 && text.front() == kDoubleQuote && text.back() == kDoubleQuote) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

void ApplyLevels(std::string_view data, std::string_view key, OrderBook& book) {
    json::ForEachArrayElement(data, key, [&](std::string_view level) {
        const auto price_text = json::FindElement(level, kBooksLevelPriceIdx);
        const auto size_text = json::FindElement(level, kBooksLevelSizeIdx);
        if (!price_text || !size_text) {
            throw std::runtime_error("Malformed order book price level");
        }

        const double price = ParseDouble(StripQuotes(*price_text));
        const double size = ParseDouble(StripQuotes(*size_text));
        if (key == kBids) {
            book.SetBid(price, size);
        } else {
            book.SetAsk(price, size);
        }
    });
}
}  // namespace

bool ApplyBookMessage(std::string_view message, OrderBook& book) {
    const auto channel = json::FindString(message, kChannel);
    if (channel != kBooksChannel) {
        return false;
    }

    const auto maybe_data = json::FindArrayElement(message, kData, 0);
    if (!maybe_data) {
        return false;
    }

    const std::string_view data = *maybe_data;
    const auto seq_id_text = json::FindNumber(data, kSeqId);
    const auto prev_seq_id_text = json::FindNumber(data, kPrevSeqId);
    if (!seq_id_text || !prev_seq_id_text) {
        throw std::runtime_error(std::format("Malformed books channel message: {}", data));
    }

    if (ParseLL(*prev_seq_id_text) == kSnapshotSeqId) {
        book.BeginSnapshot();
    }
    ApplyLevels(data, kBids, book);
    ApplyLevels(data, kAsks, book);
    book.SetSeqId(ParseLL(*seq_id_text));

    return true;
}
