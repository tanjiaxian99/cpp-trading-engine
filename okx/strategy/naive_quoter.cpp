#include "okx/strategy/naive_quoter.hpp"

#include <cmath>
#include <format>
#include <utility>

#include "log/async_logger.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/orders/order_types.hpp"
#include "okx/orders/ws_orders.hpp"
#include "perf/clock.hpp"
#include "util/json.hpp"

namespace {
constexpr double kBpsPerUnit = 10000.0;

int DecimalPlaces(std::string_view tick_sz) {
    const auto dot = tick_sz.find('.');
    if (dot == std::string_view::npos) {
        return 0;
    }
    return static_cast<int>(tick_sz.size() - dot - 1);
}
}  // namespace

NaiveQuoter::NaiveQuoter(OkxWsClient& ws_client, OrderStore& order_store,
                         EndpointRateLimiter& rate_limiter, std::string inst_id,
                         long long inst_id_code, std::string sz, std::string_view tick_sz,
                         double quote_bps, double requote_threshold_bps)
    : ws_client_(ws_client),
      order_store_(order_store),
      rate_limiter_(rate_limiter),
      inst_id_(std::move(inst_id)),
      inst_id_code_(inst_id_code),
      sz_(std::move(sz)),
      tick_sz_(json::ParseDouble(tick_sz)),
      tick_decimal_places_(DecimalPlaces(tick_sz)),
      quote_bps_(quote_bps),
      requote_threshold_bps_(requote_threshold_bps) {}

void NaiveQuoter::OnBookUpdate(const OrderBook& book, TickToTradeTrace trace) {
    const auto best_bid = book.BestBid();
    const auto best_ask = book.BestAsk();
    if (!best_bid || !best_ask) {
        return;
    }

    const double mid = (*best_bid + *best_ask) / 2.0;
    if (!last_quoted_mid_) {
        Requote(mid, trace);
        return;
    }

    const double deviation_bps =
        std::abs(mid - *last_quoted_mid_) / *last_quoted_mid_ * kBpsPerUnit;
    if (deviation_bps > requote_threshold_bps_) {
        Requote(mid, trace);
    }
}

void NaiveQuoter::OnFill(const OrderEvent& event) {
    if (!OwnsOrder(event.cl_ord_id)) {
        return;
    }
    Log.Info("Fill on {} side={} px={} sz={}", event.cl_ord_id, event.side, event.fill->px,
             event.fill->sz);
}

void NaiveQuoter::OnCancel(const OrderEvent& event) {
    if (!OwnsOrder(event.cl_ord_id)) {
        return;
    }
    Log.Info("Cancel on {}", event.cl_ord_id);
}

void NaiveQuoter::OnOrderRemoved(std::string_view cl_ord_id) {
    if (bid_cl_ord_id_ && *bid_cl_ord_id_ == cl_ord_id) {
        Log.Debug("Bid {} removed from order_store_", cl_ord_id);
        bid_cl_ord_id_.reset();
    } else if (ask_cl_ord_id_ && *ask_cl_ord_id_ == cl_ord_id) {
        Log.Debug("Ask {} removed from order_store_", cl_ord_id);
        ask_cl_ord_id_.reset();
    }
}

void NaiveQuoter::OnTimer() {
    if (!last_quoted_mid_) {
        return;
    }

    const auto [bid_px, ask_px] = ComputeQuotePrices(*last_quoted_mid_);
    if (!bid_cl_ord_id_) {
        bid_cl_ord_id_ = PlaceSide(kBuy, bid_px, std::nullopt);
    }
    if (!ask_cl_ord_id_) {
        ask_cl_ord_id_ = PlaceSide(kSell, ask_px, std::nullopt);
    }
}

void NaiveQuoter::Requote(double mid, TickToTradeTrace trace) {
    const auto [bid_px, ask_px] = ComputeQuotePrices(mid);

    ReplaceSide(bid_cl_ord_id_, kBuy, bid_px, trace);
    ReplaceSide(ask_cl_ord_id_, kSell, ask_px, trace);
    last_quoted_mid_ = mid;
}

std::pair<double, double> NaiveQuoter::ComputeQuotePrices(double mid) const {
    return {mid * (1.0 - quote_bps_ / kBpsPerUnit), mid * (1.0 + quote_bps_ / kBpsPerUnit)};
}

void NaiveQuoter::ReplaceSide(std::optional<std::string>& cl_ord_id, std::string_view side,
                              double px, std::optional<TickToTradeTrace> trace) {
    if (!cl_ord_id) {
        cl_ord_id = PlaceSide(side, px, trace);
        return;
    }

    Order* order = order_store_.FindByClOrdId(*cl_ord_id);
    if (!order || order->OrdId().empty()) {
        Log.Warn("Cannot amend {} (no ordId yet)", *cl_ord_id);
        cl_ord_id.reset();
        cl_ord_id = PlaceSide(side, px, trace);
        return;
    }

    if (!rate_limiter_.TryAcquire(kAmendOrderOp)) {
        Log.Warn("Amend order rate-limited, skipping this cycle for {}", *cl_ord_id);
        return;
    }

    const std::string formatted_px = FormatPrice(px);
    order->OnAmendRequested();
    const WsOrderRequest amend =
        BuildWsAmendOrderMessage(inst_id_code_, order->OrdId(), formatted_px, sz_);
    TraceMessageBuilt(trace);

    ws_client_.Send(amend.message, trace);
    Log.Debug("Sent amend for {} new px={}", *cl_ord_id, formatted_px);
}

std::optional<std::string> NaiveQuoter::PlaceSide(std::string_view side, double px,
                                                  std::optional<TickToTradeTrace> trace) {
    if (!rate_limiter_.TryAcquire(kOrderOp)) {
        Log.Warn("Place order rate-limited, skipping this cycle for {}", side);
        return std::nullopt;
    }

    const OrderRequest request{
        .inst_id = inst_id_,
        .side = std::string(side),
        .ord_type = std::string(kLimit),
        .px = FormatPrice(px),
        .sz = sz_,
        .inst_id_code = inst_id_code_,
    };
    const WsOrderRequest order = BuildWsOrderMessage(request);
    order_store_.Add(order.id, inst_id_, std::string(side), request.px, sz_);
    TraceMessageBuilt(trace);

    ws_client_.Send(order.message, trace);
    Log.Info("Placed order {} id={} px={}", side, order.id, request.px);
    return order.id;
}

void NaiveQuoter::TraceMessageBuilt(std::optional<TickToTradeTrace>& trace) {
    if (trace) {
        trace->message_built_ticks = perf::ReadCounter();
    }
}

std::string NaiveQuoter::FormatPrice(double px) const {
    const double rounded = std::round(px / tick_sz_) * tick_sz_;
    return std::format("{:.{}f}", rounded, tick_decimal_places_);
}

bool NaiveQuoter::OwnsOrder(std::string_view cl_ord_id) const {
    return (bid_cl_ord_id_ && *bid_cl_ord_id_ == cl_ord_id) ||
           (ask_cl_ord_id_ && *ask_cl_ord_id_ == cl_ord_id);
}
