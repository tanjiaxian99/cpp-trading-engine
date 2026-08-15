#include "okx/market_data.hpp"

#include <format>
#include <stdexcept>

#include "okx/okx_constants.hpp"
#include "okx/response_utils.hpp"
#include "util/json.hpp"

namespace {
constexpr char kDoubleQuote = '"';

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

        const double price = json::ParseDouble(StripQuotes(*price_text));
        const double size = json::ParseDouble(StripQuotes(*size_text));
        if (key == kBids) {
            book.SetBid(price, size);
        } else {
            book.SetAsk(price, size);
        }
    });
}
}  // namespace

BookMessageResult ApplyBookMessage(std::string_view message, OrderBook& book) {
    const auto channel = json::FindString(message, kChannel);
    if (channel != kBooksChannel) {
        return BookMessageResult::kIgnored;
    }

    const auto maybe_data = FindData(message);
    if (!maybe_data) {
        return BookMessageResult::kIgnored;
    }

    const std::string_view data = *maybe_data;
    const auto seq_id_text = json::FindNumber(data, kSeqId);
    const auto prev_seq_id_text = json::FindNumber(data, kPrevSeqId);
    if (!seq_id_text || !prev_seq_id_text) {
        throw std::runtime_error(std::format("Malformed books channel message: {}", data));
    }

    const long long prev_seq_id = json::ParseLL(*prev_seq_id_text);
    const bool is_snapshot = prev_seq_id == kSnapshotSeqId;

    // A non-snapshot push must chain directly onto the book's last applied
    // seqId. A mismatch means an update was missed in between — the book is
    // stale and must not be trusted until a fresh snapshot resets it.
    if (!is_snapshot && book.HasSnapshot() && prev_seq_id != book.LastSeqId()) {
        return BookMessageResult::kGapDetected;
    }

    if (is_snapshot) {
        book.BeginSnapshot();
    }
    ApplyLevels(data, kBids, book);
    ApplyLevels(data, kAsks, book);
    book.SetSeqId(json::ParseLL(*seq_id_text));

    return BookMessageResult::kApplied;
}
