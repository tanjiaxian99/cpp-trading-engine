#include "okx/strategy/naive_quoter.hpp"

#include <cmath>
#include <format>
#include <iostream>
#include <utility>

#include "okx/common/okx_constants.hpp"
#include "okx/orders/order_types.hpp"
#include "okx/orders/ws_orders.hpp"
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

NaiveQuoter::NaiveQuoter(OkxWsClient& ws_client, OrderStore& order_store, std::string inst_id,
                         long long inst_id_code, std::string sz, std::string_view tick_sz,
                         double quote_bps, double requote_threshold_bps)
    : ws_client_(ws_client),
      order_store_(order_store),
      inst_id_(std::move(inst_id)),
      inst_id_code_(inst_id_code),
      sz_(std::move(sz)),
      tick_sz_(json::ParseDouble(tick_sz)),
      tick_decimal_places_(DecimalPlaces(tick_sz)),
      quote_bps_(quote_bps),
      requote_threshold_bps_(requote_threshold_bps) {}

void NaiveQuoter::OnBookUpdate(const OrderBook& book) {
    const auto best_bid = book.BestBid();
    const auto best_ask = book.BestAsk();
    if (!best_bid || !best_ask) {
        return;
    }

    const double mid = (*best_bid + *best_ask) / 2.0;
    if (!last_quoted_mid_) {
        Requote(mid);
        return;
    }

    const double deviation_bps =
        std::abs(mid - *last_quoted_mid_) / *last_quoted_mid_ * kBpsPerUnit;
    if (deviation_bps > requote_threshold_bps_) {
        Requote(mid);
    }
}

void NaiveQuoter::OnFill(const OrderEvent& event) {
    if (!OwnsOrder(event.cl_ord_id)) {
        return;
    }
    std::cout << "Quoter: fill on " << event.cl_ord_id << " side=" << event.side
              << " px=" << event.fill_px << " sz=" << event.fill_sz << "\n";
}

void NaiveQuoter::OnReject(const OrderEvent& event) {
    if (!OwnsOrder(event.cl_ord_id)) {
        return;
    }
    std::cout << "Quoter: reject on " << event.cl_ord_id << "\n";
}

void NaiveQuoter::OnOrderRemoved(std::string_view cl_ord_id) {
    if (bid_cl_ord_id_ && *bid_cl_ord_id_ == cl_ord_id) {
        std::cout << "Quoter: bid " << cl_ord_id << " removed from order_store_\n";
        bid_cl_ord_id_.reset();
    } else if (ask_cl_ord_id_ && *ask_cl_ord_id_ == cl_ord_id) {
        std::cout << "Quoter: ask " << cl_ord_id << " removed from order_store_\n";
        ask_cl_ord_id_.reset();
    }
}

void NaiveQuoter::OnTimer() {
    if (!last_quoted_mid_) {
        return;
    }
    EnsureQuoted(*last_quoted_mid_);
}

void NaiveQuoter::Requote(double mid) {
    CancelSide(bid_cl_ord_id_);
    CancelSide(ask_cl_ord_id_);
    EnsureQuoted(mid);
    last_quoted_mid_ = mid;
}

void NaiveQuoter::EnsureQuoted(double mid) {
    const double bid_px = mid * (1.0 - quote_bps_ / kBpsPerUnit);
    const double ask_px = mid * (1.0 + quote_bps_ / kBpsPerUnit);

    if (!bid_cl_ord_id_) {
        bid_cl_ord_id_ = PlaceSide(kBuy, bid_px);
    }
    if (!ask_cl_ord_id_) {
        ask_cl_ord_id_ = PlaceSide(kSell, ask_px);
    }
}

void NaiveQuoter::CancelSide(std::optional<std::string>& cl_ord_id) {
    if (!cl_ord_id) {
        return;
    }

    Order* order = order_store_.FindByClOrdId(*cl_ord_id);
    if (order && !order->OrdId().empty()) {
        const WsOrderRequest cancel = BuildWsCancelOrderMessage(inst_id_code_, order->OrdId());
        ws_client_.Send(cancel.message);
        std::cout << "Quoter: sent cancel for " << *cl_ord_id << "\n";
    } else {
        std::cout << "Quoter: cannot cancel " << *cl_ord_id << " (no ordId yet)\n";
    }
    cl_ord_id.reset();
}

std::string NaiveQuoter::PlaceSide(std::string_view side, double px) {
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
    ws_client_.Send(order.message);
    std::cout << "Quoter: placed " << side << " id=" << order.id << " px=" << request.px << "\n";
    return order.id;
}

std::string NaiveQuoter::FormatPrice(double px) const {
    const double rounded = std::round(px / tick_sz_) * tick_sz_;
    return std::format("{:.{}f}", rounded, tick_decimal_places_);
}

bool NaiveQuoter::OwnsOrder(std::string_view cl_ord_id) const {
    return (bid_cl_ord_id_ && *bid_cl_ord_id_ == cl_ord_id) ||
           (ask_cl_ord_id_ && *ask_cl_ord_id_ == cl_ord_id);
}
