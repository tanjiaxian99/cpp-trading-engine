#include "okx/marketdata/json_book_source.hpp"

#include <format>
#include <utility>

#include "log/async_logger.hpp"
#include "okx/marketdata/market_data.hpp"
#include "perf/clock.hpp"

namespace {
constexpr std::string_view kSubscribeFormat =
    R"({{"op":"subscribe","args":[{{"channel":"books","instId":"{0}"}},)"
    R"({{"channel":"trades","instId":"{0}"}}]}})";
constexpr std::string_view kUnsubscribeFormat =
    R"({{"op":"unsubscribe","args":[{{"channel":"books","instId":"{}"}}]}})";
constexpr std::string_view kResubscribeFormat =
    R"({{"op":"subscribe","args":[{{"channel":"books","instId":"{}"}}]}})";
}  // namespace

JsonBookSource::JsonBookSource(asio::io_context& io_context, std::string host, std::string port,
                               std::string path, std::string inst_id)
    : ws_client_(io_context, std::move(host), std::move(port), std::move(path)),
      subscribe_msg_(std::format(kSubscribeFormat, inst_id)),
      unsubscribe_msg_(std::format(kUnsubscribeFormat, inst_id)),
      resubscribe_msg_(std::format(kResubscribeFormat, inst_id)) {}

void JsonBookSource::SetOnBookUpdate(BookHandler handler) {
    on_book_update_ = std::move(handler);
}

std::string_view JsonBookSource::Name() const {
    return "json-books";
}

void JsonBookSource::Start() {
    ws_client_.SetOnConnected([this]() {
        ws_client_.Send(subscribe_msg_);
        Log.Info("Sent books and trades WS subscribe request");
    });
    ws_client_.SetOnMessage([this](std::string_view message) { OnMessage(message); });
    ws_client_.Start();
}

void JsonBookSource::OnMessage(std::string_view message) {
    const std::uint64_t wire_arrival_ticks = ws_client_.LastMessageArrivalTicks();
    const std::uint64_t message_decoded_ticks = ws_client_.LastMessageDecodedTicks();

    switch (ApplyBookMessage(message, book_)) {
        case BookMessageResult::kApplied: {
            const std::uint64_t book_consistent_ticks = perf::ReadCounter();
            Log.Debug("Update to orderbook: bid={} ask={} seqId={}", book_.BestBid().value_or(0.0),
                      book_.BestAsk().value_or(0.0), book_.LastSeqId());
            if (on_book_update_) {
                on_book_update_(book_, TickToTradeTrace{
                                           .wire_arrival_ticks = wire_arrival_ticks,
                                           .message_decoded_ticks = message_decoded_ticks,
                                           .book_consistent_ticks = book_consistent_ticks,
                                       });
            }
            break;
        }
        case BookMessageResult::kGapDetected:
            Log.Warn("Order book sequence gap detected — resubscribing for a fresh snapshot");
            ws_client_.Send(unsubscribe_msg_);
            ws_client_.Send(resubscribe_msg_);
            break;
        case BookMessageResult::kIgnored:
            Log.Debug("WS message is ignored: {}", message);
            break;
    }
}
