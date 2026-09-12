#pragma once

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <string_view>

#include "config.hpp"
#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/connectivity/rate_limiter.hpp"
#include "okx/connectivity/ws_response_demux.hpp"
#include "okx/engine/bootstrap.hpp"
#include "okx/marketdata/market_data_source.hpp"
#include "okx/marketdata/order_book.hpp"
#include "okx/orders/order_events.hpp"
#include "okx/orders/order_store.hpp"
#include "okx/orders/order_timeout_monitor.hpp"
#include "okx/orders/ws_order_round_trip.hpp"
#include "okx/risk/account_state.hpp"
#include "okx/risk/kill_switch.hpp"
#include "okx/risk/position.hpp"
#include "okx/strategy/naive_quoter.hpp"
#include "perf/tick_to_trade_stats.hpp"
#include "perf/tick_to_trade_trace.hpp"
#include "rest/rest_client.hpp"

namespace asio = boost::asio;

class Engine {
public:
    Engine(MarketDataMode market_data_mode, RestClient& rest_client, const OkxAuth& auth,
           BootstrapResult bootstrap, TickToTradeStats& tick_to_trade_stats);

    void Run();

private:
    void OnPrivateWsMessage(std::string_view message);
    void HandleLoginEvent(std::string_view message);
    void HandleOrdersChannelMessage(std::string_view message);
    void HandleOrderEvent(const OrderEvent& event);
    void HandleAccountChannelMessage(std::string_view message);

    void ScheduleQuoterTimer();
    void ScheduleStatsLogTimer();
    void SetUpShutdownSignals();

    RestClient& rest_client_;
    const OkxAuth& auth_;
    BootstrapResult bootstrap_;
    TickToTradeStats& tick_to_trade_stats_;

    asio::io_context io_context_;
    OkxWsClient private_ws_client_;
    std::unique_ptr<MarketDataSource> market_data_;
    OrderStore order_store_;
    KillSwitch kill_switch_;
    EndpointRateLimiter rate_limiter_;
    NaiveQuoter quoter_;
    AccountState account_state_;
    WsResponseDemux ws_demux_;
    WsOrderRoundTrip order_round_trip_;
    OrderTimeoutMonitor order_timeout_monitor_;
    Position position_;

    asio::steady_timer quoter_timer_;
    asio::steady_timer stats_log_timer_;
    asio::signal_set shutdown_signals_;
};
