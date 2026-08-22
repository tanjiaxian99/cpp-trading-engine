#pragma once

#include "okx/order_book.hpp"
#include "okx/order_events.hpp"

class Strategy {
public:
    Strategy() = default;
    virtual ~Strategy() = default;
    Strategy(const Strategy&) = delete;
    Strategy& operator=(const Strategy&) = delete;
    Strategy(Strategy&&) = delete;
    Strategy& operator=(Strategy&&) = delete;

    virtual void OnBookUpdate(const OrderBook& book) = 0;
    virtual void OnFill(const OrderEvent& event) = 0;
    virtual void OnReject(const OrderEvent& event) = 0;
    virtual void OnTimer() = 0;
};
