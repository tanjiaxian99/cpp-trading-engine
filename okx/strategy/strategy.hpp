#pragma once

#include "okx/marketdata/order_book.hpp"
#include "okx/orders/order_events.hpp"
#include "perf/tick_to_trade_trace.hpp"

class Strategy {
public:
    Strategy() = default;
    virtual ~Strategy() = default;
    Strategy(const Strategy&) = delete;
    Strategy& operator=(const Strategy&) = delete;
    Strategy(Strategy&&) = delete;
    Strategy& operator=(Strategy&&) = delete;

    virtual void OnBookUpdate(const OrderBook& book, TickToTradeTrace trace) = 0;
    virtual void OnFill(const OrderEvent& event) = 0;
    virtual void OnCancel(const OrderEvent& event) = 0;
    virtual void OnTimer() = 0;
};
