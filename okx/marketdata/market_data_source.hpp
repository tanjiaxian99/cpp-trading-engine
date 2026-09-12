#pragma once

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <string_view>

#include "config.hpp"
#include "okx/connectivity/auth.hpp"
#include "okx/marketdata/order_book.hpp"
#include "perf/tick_to_trade_trace.hpp"

namespace asio = boost::asio;

class MarketDataSource {
public:
    using BookHandler = std::function<void(const OrderBook& book, TickToTradeTrace trace)>;

    MarketDataSource() = default;
    virtual ~MarketDataSource() = default;
    MarketDataSource(const MarketDataSource&) = delete;
    MarketDataSource& operator=(const MarketDataSource&) = delete;
    MarketDataSource(MarketDataSource&&) = delete;
    MarketDataSource& operator=(MarketDataSource&&) = delete;

    virtual void SetOnBookUpdate(BookHandler handler) = 0;
    virtual void Start() = 0;
    [[nodiscard]] virtual std::string_view Name() const = 0;
};

std::unique_ptr<MarketDataSource> MakeMarketDataSource(MarketDataMode mode,
                                                       asio::io_context& io_context,
                                                       const OkxAuth& auth, long long inst_id_code);
