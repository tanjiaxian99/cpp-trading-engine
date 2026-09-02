#pragma once

#include <cstdint>

#include "okx/marketdata/order_book.hpp"
#include "okx/orders/order_events.hpp"

class Strategy {
public:
    Strategy() = default;
    virtual ~Strategy() = default;
    Strategy(const Strategy&) = delete;
    Strategy& operator=(const Strategy&) = delete;
    Strategy(Strategy&&) = delete;
    Strategy& operator=(Strategy&&) = delete;

    virtual void OnBookUpdate(const OrderBook& book, std::uint64_t wire_arrival_ticks) = 0;
    virtual void OnFill(const OrderEvent& event) = 0;
    virtual void OnCancel(const OrderEvent& event) = 0;
    virtual void OnTimer() = 0;
};
