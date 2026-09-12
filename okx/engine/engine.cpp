#include "okx/engine/engine.hpp"

#include <string>
#include <utility>

#include "log/async_logger.hpp"
#include "okx/common/instruments.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/connectivity/okx_endpoints.hpp"
#include "okx/orders/order_lifecycle.hpp"
#include "okx/orders/reconciliation.hpp"
#include "okx/orders/rest_orders.hpp"
#include "okx/risk/risk.hpp"
#include "util/json.hpp"

namespace {
constexpr std::string_view kPrivateSubscribeMsg =
    R"({"op":"subscribe","args":)"
    R"([{"channel":"orders","instType":"SPOT"},{"channel":"account"},)"
    R"({"channel":"positions","instType":"ANY"}]})";

constexpr RiskLimits kRiskLimits{
    .max_order_sz = 1.0,
    .max_notional = 1000.0,
    .price_collar_pct = 0.05,
    .max_open_orders = 10,
};
constexpr double kMaxRealizedLoss = 50.0;

constexpr std::string_view kQuoterSz = "0.01";
constexpr double kQuoterBps = 0.01;
constexpr double kRequoteThresholdBps = 0.01;
constexpr RateLimit kOrderRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr RateLimit kCancelRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr RateLimit kAmendRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr auto kQuoterTimerInterval = std::chrono::milliseconds(500);
constexpr auto kStatsLogInterval = std::chrono::minutes(5);

void LogOrderEvent(const OrderEvent& event) {
    Log.Info(
        "Order event received: type={} ordId={} instId={} side={} px={} sz={} accFillSz={} "
        "avgPx={}",
        ToString(event.type), event.ord_id, event.inst_id, event.side, event.px, event.sz,
        event.acc_fill_sz, event.avg_px);
}

void LogAccountBalance(const AccountState::Balance& balance) {
    Log.Debug("Current account balance: ccy={} cashBal={} availBal={}", balance.ccy,
              balance.cash_bal, balance.avail_bal);
}
}  // namespace

Engine::Engine(MarketDataMode market_data_mode, RestClient& rest_client, const OkxAuth& auth,
               BootstrapResult bootstrap, TickToTradeStats& tick_to_trade_stats)
    : rest_client_(rest_client),
      auth_(auth),
      bootstrap_(std::move(bootstrap)),
      tick_to_trade_stats_(tick_to_trade_stats),
      private_ws_client_(io_context_, std::string(kOkxWsHost), std::string(kOkxWsPort),
                         std::string(kPrivateWsPath), auth_),
      market_data_(MakeMarketDataSource(market_data_mode, io_context_, auth_,
                                        bootstrap_.eth_usdt_inst_id_code)),
      kill_switch_(rest_client_, auth_, order_store_),
      rate_limiter_(kOrderRateLimit, kCancelRateLimit, kAmendRateLimit),
      quoter_(private_ws_client_, order_store_, rate_limiter_, std::string(kEthUsdt),
              bootstrap_.eth_usdt_inst_id_code, std::string(kQuoterSz), bootstrap_.eth_spec.tick_sz,
              kQuoterBps, kRequoteThresholdBps),
      order_round_trip_(private_ws_client_, ws_demux_, rest_client_, auth_,
                        bootstrap_.order_request, order_store_),
      order_timeout_monitor_(io_context_, order_store_, rest_client_, auth_),
      quoter_timer_(io_context_),
      stats_log_timer_(io_context_),
      shutdown_signals_(io_context_, SIGINT, SIGTERM) {
    Log.Info("Market data mode: {}", market_data_->Name());

    order_store_.SetOnRemove(
        [this](std::string_view cl_ord_id) { quoter_.OnOrderRemoved(cl_ord_id); });

    private_ws_client_.SetOnTraceResolved(
        [this](const TickToTradeTrace& trace) { tick_to_trade_stats_.Record(trace); });

    market_data_->SetOnBookUpdate([this](const OrderBook& book, TickToTradeTrace trace) {
        if (!kill_switch_.IsTriggered() && private_ws_client_.IsAuthenticated()) {
            quoter_.OnBookUpdate(book, trace);
        }
    });

    private_ws_client_.SetOnMessage(
        [this](std::string_view message) { OnPrivateWsMessage(message); });

    order_timeout_monitor_.Start();
    ScheduleQuoterTimer();
    ScheduleStatsLogTimer();
    SetUpShutdownSignals();
}

void Engine::Run() {
    market_data_->Start();
    private_ws_client_.Start();
    while (!io_context_.stopped()) {
        io_context_.poll();
    }
}

void Engine::ScheduleQuoterTimer() {
    quoter_timer_.expires_after(kQuoterTimerInterval);
    quoter_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        if (!kill_switch_.IsTriggered() && private_ws_client_.IsAuthenticated()) {
            quoter_.OnTimer();
        }
        ScheduleQuoterTimer();
    });
}

void Engine::ScheduleStatsLogTimer() {
    stats_log_timer_.expires_after(kStatsLogInterval);
    stats_log_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        tick_to_trade_stats_.LogSummary();
        ScheduleStatsLogTimer();
    });
}

void Engine::SetUpShutdownSignals() {
    shutdown_signals_.async_wait([this](const boost::system::error_code& ec, int) {
        if (ec) {
            return;
        }
        tick_to_trade_stats_.LogSummary();
        kill_switch_.Trigger("manual (signal)");
        io_context_.stop();
    });
}

void Engine::OnPrivateWsMessage(std::string_view message) {
    if (json::FindString(message, kEvent) == kLoginEvent) {
        HandleLoginEvent(message);
        return;
    }

    if (ws_demux_.Dispatch(message)) {
        return;
    }

    const auto channel = json::FindString(message, kChannel);
    if (channel == kOrdersChannel) {
        HandleOrdersChannelMessage(message);
    } else if (channel == kAccountChannel) {
        HandleAccountChannelMessage(message);
    } else {
        Log.Debug("Private WS message from neither orders nor account channel: {}", message);
    }
}

void Engine::HandleLoginEvent(std::string_view message) {
    const auto code = json::FindString(message, kCode).value_or(kEmpty);
    Log.Info("Private WS login: code={} msg={}", code,
             json::FindString(message, kMsg).value_or(kEmpty));
    if (code != kSuccessCode) {
        return;
    }

    private_ws_client_.Send(std::string(kPrivateSubscribeMsg));
    Log.Info("Sent orders, account and positions WS subscribe request");

    const HttpResponse pending = GetPendingOrders(rest_client_, auth_);
    Log.Debug("Pending orders reconciliation: {}", pending.body);
    ReconcileOrders(order_store_, pending.body);

    if (kill_switch_.IsTriggered()) {
        Log.Warn("Kill switch is triggered so we will skip order placement");
        return;
    }

    const RiskCheckResult risk_check =
        CheckPreTradeRisk(kRiskLimits, bootstrap_.order_request, order_store_, std::nullopt);
    if (!risk_check.passed) {
        Log.Warn("Pre-trade risk check failed: {}", risk_check.reason);
        return;
    }

    order_round_trip_.Start();
}

void Engine::HandleOrdersChannelMessage(std::string_view message) {
    ForEachOrderEvent(message, [this](const OrderEvent& event) { HandleOrderEvent(event); });
}

void Engine::HandleOrderEvent(const OrderEvent& event) {
    LogOrderEvent(event);
    if (event.type == OrderEventType::kFill || event.type == OrderEventType::kPartialFill) {
        if (event.fill) {
            quoter_.OnFill(event);
            position_.ApplyFill(event.side, event.fill->px, event.fill->sz);
            Log.Info("Current position: netQty={} avgEntryPx={} realizedPnl={}", position_.NetQty(),
                     position_.AvgEntryPx(), position_.RealizedPnl());
            if (position_.RealizedPnl() < -kMaxRealizedLoss) {
                kill_switch_.Trigger("Max realized loss breached");
            }
        } else {
            Log.Debug("Fill-state order event with no new fill data (state resend), ignoring");
        }
    } else if (event.type == OrderEventType::kCancel) {
        quoter_.OnCancel(event);
    }

    order_round_trip_.ApplyOrderEvent(event);
}

void Engine::HandleAccountChannelMessage(std::string_view message) {
    account_state_.ApplyMessage(message);
    account_state_.ForEachBalance(
        [](const AccountState::Balance& balance) { LogAccountBalance(balance); });
}
