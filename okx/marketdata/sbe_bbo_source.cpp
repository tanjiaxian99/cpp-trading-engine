#include "okx/marketdata/sbe_bbo_source.hpp"

#include <format>
#include <utility>

#include "log/async_logger.hpp"
#include "okx/marketdata/sbe_bbo.hpp"
#include "perf/clock.hpp"

namespace {
// SBE subscriptions identify the instrument by instIdCode, not instId.
constexpr std::string_view kSubscribeFormat =
    R"({{"op":"subscribe","args":[{{"channel":"bbo-tbt","instIdCode":{}}}]}})";
}  // namespace

SbeBboSource::SbeBboSource(asio::io_context& io_context, std::string host, std::string port,
                           std::string path, long long inst_id_code, std::optional<OkxAuth> auth)
    : ws_client_(io_context, std::move(host), std::move(port), std::move(path), std::move(auth),
                 WsEndpointKind::kSbe),
      subscribe_msg_(std::format(kSubscribeFormat, inst_id_code)) {}

void SbeBboSource::SetOnBookUpdate(BookHandler handler) {
    on_book_update_ = std::move(handler);
}

std::string_view SbeBboSource::Name() const {
    return "sbe-bbo-tbt";
}

void SbeBboSource::Start() {
    ws_client_.SetOnConnected([this]() {
        ws_client_.Send(subscribe_msg_);
        Log.Info("Sent SBE bbo-tbt WS subscribe request");
    });
    ws_client_.SetOnMessage([this](std::string_view message) { OnMessage(message); });
    ws_client_.Start();
}

void SbeBboSource::OnMessage(std::string_view message) {
    const std::uint64_t wire_arrival_ticks = ws_client_.LastMessageArrivalTicks();
    const std::uint64_t message_decoded_ticks = ws_client_.LastMessageDecodedTicks();

    const auto bbo = sbe::DecodeBboTbt(message);
    if (!bbo) {
        Log.Warn("Non-BBO message on the SBE socket: {}", message);
        return;
    }

    if (bbo->seq_id <= last_seq_id_) {
        Log.Debug("Stale SBE bbo-tbt message: seqId={} last={}", bbo->seq_id, last_seq_id_);
        return;
    }
    last_seq_id_ = bbo->seq_id;

    book_.BeginSnapshot();
    book_.SetBid(bbo->bid_px, bbo->bid_sz);
    book_.SetAsk(bbo->ask_px, bbo->ask_sz);
    book_.SetSeqId(bbo->seq_id);
    const std::uint64_t book_consistent_ticks = perf::ReadCounter();

    Log.Debug("SBE bbo-tbt: bid={} ask={} seqId={}", bbo->bid_px, bbo->ask_px, bbo->seq_id);
    if (on_book_update_) {
        on_book_update_(book_, TickToTradeTrace{
                                   .wire_arrival_ticks = wire_arrival_ticks,
                                   .message_decoded_ticks = message_decoded_ticks,
                                   .book_consistent_ticks = book_consistent_ticks,
                               });
    }
}
