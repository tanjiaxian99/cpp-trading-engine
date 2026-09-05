#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/connectivity/rate_limiter.hpp"
#include "okx/orders/order_store.hpp"
#include "okx/strategy/strategy.hpp"
#include "perf/tick_to_trade_trace.hpp"

class NaiveQuoter : public Strategy {
public:
    NaiveQuoter(OkxWsClient& ws_client, OrderStore& order_store, EndpointRateLimiter& rate_limiter,
                std::string inst_id, long long inst_id_code, std::string sz,
                std::string_view tick_sz, double quote_bps, double requote_threshold_bps);

    void OnBookUpdate(const OrderBook& book, TickToTradeTrace trace) override;
    void OnFill(const OrderEvent& event) override;
    void OnCancel(const OrderEvent& event) override;
    void OnTimer() override;
    void OnOrderRemoved(std::string_view cl_ord_id);

private:
    void Requote(double mid, TickToTradeTrace trace);
    void ReplaceSide(std::optional<std::string>& cl_ord_id, std::string_view side, double px,
                     std::optional<TickToTradeTrace> trace);
    std::optional<std::string> PlaceSide(std::string_view side, double px,
                                         std::optional<TickToTradeTrace> trace);
    void TraceMessageBuilt(std::optional<TickToTradeTrace>& trace);
    [[nodiscard]] std::string FormatPrice(double px) const;
    [[nodiscard]] std::pair<double, double> ComputeQuotePrices(double mid) const;
    [[nodiscard]] bool OwnsOrder(std::string_view cl_ord_id) const;

    OkxWsClient& ws_client_;
    OrderStore& order_store_;
    EndpointRateLimiter& rate_limiter_;
    std::string inst_id_;
    long long inst_id_code_;
    std::string sz_;
    double tick_sz_;
    int tick_decimal_places_;
    double quote_bps_;
    double requote_threshold_bps_;

    std::optional<double> last_quoted_mid_;
    std::optional<std::string> bid_cl_ord_id_;
    std::optional<std::string> ask_cl_ord_id_;
};
