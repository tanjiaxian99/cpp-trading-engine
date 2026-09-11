#include <boost/asio.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

#include "config.hpp"
#include "log/async_logger.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/common/response_utils.hpp"
#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/instrument.hpp"
#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/connectivity/rate_limiter.hpp"
#include "okx/connectivity/ws_response_demux.hpp"
#include "okx/marketdata/json_book_source.hpp"
#include "okx/marketdata/market_data_source.hpp"
#include "okx/marketdata/order_book.hpp"
#include "okx/marketdata/sbe_bbo_source.hpp"
#include "okx/orders/order_events.hpp"
#include "okx/orders/order_lifecycle.hpp"
#include "okx/orders/order_store.hpp"
#include "okx/orders/order_timeout_monitor.hpp"
#include "okx/orders/order_types.hpp"
#include "okx/orders/reconciliation.hpp"
#include "okx/orders/rest_orders.hpp"
#include "okx/orders/ws_orders.hpp"
#include "okx/risk/account_state.hpp"
#include "okx/risk/kill_switch.hpp"
#include "okx/risk/position.hpp"
#include "okx/risk/risk.hpp"
#include "okx/strategy/naive_quoter.hpp"
#include "perf/clock.hpp"
#include "perf/tick_to_trade_stats.hpp"
#include "perf/tick_to_trade_trace.hpp"
#include "rest/rest_client.hpp"
#include "util/json.hpp"

namespace {
constexpr std::string_view kOkxRestBaseUrl = "https://www.okx.com";
constexpr std::string_view kOkxWsHost = "wspap.okx.com";
constexpr std::string_view kOkxWsPort = "8443";
constexpr std::string_view kPublicWsPath = "/ws/v5/public";
constexpr std::string_view kPrivateWsPath = "/ws/v5/private";

constexpr std::string_view kBtcUsdt = "BTC-USDT";
constexpr std::string_view kEthUsdt = "ETH-USDT";

// Order params for the B4 WS order/amend/cancel smoke test — a limit buy
// far enough below market that it never fills.
constexpr std::string_view kSmokeTestPx = "100";
constexpr std::string_view kSmokeTestAmendPx = "101";
constexpr std::string_view kSmokeTestSz = "0.01";

constexpr std::string_view kSbeWsPath = "/ws/v5/public-sbe";

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

constexpr std::string_view kQuoterSz = kSmokeTestSz;
constexpr double kQuoterBps = 1.0;
constexpr double kRequoteThresholdBps = 1.0;
constexpr RateLimit kOrderRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr RateLimit kCancelRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr RateLimit kAmendRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr auto kQuoterTimerInterval = std::chrono::milliseconds(500);
constexpr auto kStatsLogInterval = std::chrono::minutes(5);

std::unique_ptr<MarketDataSource> MakeMarketDataSource(MarketDataMode mode,
                                                       asio::io_context& io_context,
                                                       const OkxAuth& auth,
                                                       long long inst_id_code) {
    switch (mode) {
        case MarketDataMode::kJsonBook:
            return std::make_unique<JsonBookSource>(
                io_context, std::string(kOkxWsHost), std::string(kOkxWsPort),
                std::string(kPublicWsPath), std::string(kEthUsdt));
        case MarketDataMode::kSbeBbo:
            return std::make_unique<SbeBboSource>(io_context, std::string(kOkxWsHost),
                                                  std::string(kOkxWsPort), std::string(kSbeWsPath),
                                                  inst_id_code, auth);
    }
    throw std::runtime_error(std::format("Unrecognized MarketDataMode"));
}

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

bool IsRequestAccepted(std::string_view response) {
    const auto data = FindData(response);
    return data && json::FindString(*data, kSCode) == kSuccessCode;
}

// Extracts the server timestamp from /public/time's response, e.g.
// {"code":"0","data":[{"ts":"1786204129995"}],"msg":""}, using the
// general-purpose scanner (json::FindArrayElement + json::FindString)
// rather than a one-off ad-hoc lookup.
long long ExtractTimestampMs(const std::string& public_time_body) {
    const auto data = FindData(public_time_body);
    if (!data) {
        throw std::runtime_error("Could not find data[0] in /public/time response");
    }
    const auto ts = json::FindString(*data, kTs);
    if (!ts) {
        throw std::runtime_error("Could not find ts field in /public/time response");
    }
    return std::stoll(std::string(*ts));
}

class WsOrderRoundTrip {
public:
    WsOrderRoundTrip(OkxWsClient& ws_client, WsResponseDemux& demux, RestClient& rest_client,
                     const OkxAuth& auth, const OrderRequest& order_request,
                     OrderStore& order_store)
        : ws_client_(ws_client),
          demux_(demux),
          rest_client_(rest_client),
          auth_(auth),
          order_request_(order_request),
          order_store_(order_store) {}

    void Start() {
        if (!ws_client_.IsConnected()) {
            Log.Warn("Private WS is down, falling back to REST for order placement");
            const OrderResult result = PlaceOrder(rest_client_, auth_, order_request_);
            Log.Info("REST fallback order placement: accepted={} ordId={} sCode={} sMsg={}",
                     result.accepted, result.ord_id, result.s_code, result.s_msg);
            if (result.accepted) {
                ord_id_ = result.ord_id;
                FallBackToRestCancel();
            }
            return;
        }

        const WsOrderRequest order = BuildWsOrderMessage(order_request_);
        cl_ord_id_ = order.id;
        order_store_.Add(order.id, order_request_.inst_id, order_request_.side, order_request_.px,
                         order_request_.sz);
        ws_client_.Send(order.message);
        Log.Debug("Sent WS order request id={}", order.id);
        demux_.Track(order.id, [this](std::string_view response) { OnOrderResponse(response); });
    }

    void ApplyOrderEvent(const OrderEvent& event) {
        Order* order = order_store_.FindByClOrdId(event.cl_ord_id);
        if (!order) {
            return;
        }

        order->ApplyEvent(event);
        Log.Debug("New order state={} after applying event", ToString(order->State()));
        if (IsTerminal(order->State())) {
            order_store_.Remove(order->ClOrdId());
        }
    }

private:
    Order& GetOrder() {
        Order* order = order_store_.FindByClOrdId(cl_ord_id_);
        if (!order) {
            throw std::runtime_error("WsOrderRoundTrip: order not found in order_store_");
        }
        return *order;
    }

    void OnOrderResponse(std::string_view response) {
        Log.Debug("WS order response: {}", response);
        if (!IsRequestAccepted(response)) {
            // Rejected before ever existing on the exchange — Order has no
            // transition for this from kPendingNew. Leave it for
            // reconciliation/timeout cleanup rather than proceeding to
            // amend/cancel an order that was never placed.
            Log.Warn("Order placement rejected, cleanup will be done by reconciliation");
            return;
        }
        const auto data = FindData(response);
        const auto ord_id = data ? json::FindString(*data, kOrdId) : std::nullopt;
        if (!ord_id) {
            return;
        }
        ord_id_ = std::string(*ord_id);

        if (!ws_client_.IsConnected()) {
            FallBackToRestCancel();
            return;
        }

        GetOrder().OnAmendRequested();
        const WsOrderRequest amend = BuildWsAmendOrderMessage(order_request_.inst_id_code, ord_id_,
                                                              kSmokeTestAmendPx, order_request_.sz);
        ws_client_.Send(amend.message);
        Log.Debug("Sent WS amend-order request id={}", amend.id);
        demux_.Track(amend.id, [this](std::string_view response) { OnAmendResponse(response); });
    }

    void OnAmendResponse(std::string_view response) {
        Log.Debug("WS amend-order response: {}", response);
        if (!IsRequestAccepted(response)) {
            GetOrder().OnRequestRejected();
            Log.Warn("New order state={} upon amend request rejection",
                     ToString(GetOrder().State()));
        }

        if (!ws_client_.IsConnected()) {
            FallBackToRestCancel();
            return;
        }

        GetOrder().OnCancelRequested();
        const WsOrderRequest cancel =
            BuildWsCancelOrderMessage(order_request_.inst_id_code, ord_id_);
        ws_client_.Send(cancel.message);
        Log.Debug("Sent WS cancel-order request id={}", cancel.id);
        demux_.Track(cancel.id, [this](std::string_view response) { OnCancelResponse(response); });
    }

    void OnCancelResponse(std::string_view response) {
        Log.Debug("WS cancel-order response: {}", response);
        if (!IsRequestAccepted(response)) {
            GetOrder().OnRequestRejected();
            Log.Warn("New order state={} upon cancel request rejection",
                     ToString(GetOrder().State()));
        }
    }

    void FallBackToRestCancel() {
        Log.Warn("Private WS is down, falling back to REST for order cancellation");
        const OrderResult result =
            CancelOrder(rest_client_, auth_, order_request_.inst_id, ord_id_);
        Log.Info("REST fallback order cancellation: accepted={} sCode={} sMsg={}", result.accepted,
                 result.s_code, result.s_msg);
    }

    OkxWsClient& ws_client_;
    WsResponseDemux& demux_;
    RestClient& rest_client_;
    const OkxAuth& auth_;
    const OrderRequest& order_request_;
    std::string ord_id_;
    std::string cl_ord_id_;
    OrderStore& order_store_;
};
}  // namespace

int main() {
#ifdef __APPLE__
    if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) != 0) {
        Log.Warn("Failed to set thread QoS class: {}", std::strerror(errno));
    }
#endif

    TickToTradeStats tick_to_trade_stats;
    try {
        const Config config = Config::FromEnv();
        Log.Info("Loaded config for key {}", config.api_key);

        RestClient rest_client{std::string(kOkxRestBaseUrl)};

        // Public, unauthenticated endpoint — also used below for the clock
        // drift check A3 requires before signed requests can be trusted.
        const HttpResponse time_response = rest_client.Get("/api/v5/public/time");
        Log.Debug("REST status {}: {}", time_response.status_code, time_response.body);

        const long long server_time_ms = ExtractTimestampMs(time_response.body);
        const auto local_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
        Log.Info("Clock drift: {} ms", local_time_ms - server_time_ms);

        // Signed request — proves the A3 auth/signing path end-to-end.
        using enum HttpMethod;
        const OkxAuth auth(config.api_key, config.api_secret, config.passphrase);
        const std::string balance_path = "/api/v5/account/balance";
        const HttpResponse balance_response =
            rest_client.Get(balance_path, auth.SignHeaders(kGet, balance_path));
        Log.Debug("Balance status {}: {}", balance_response.status_code, balance_response.body);

        // Public, unauthenticated endpoint — proves instrument-spec fetch
        // and parsing end-to-end.
        const InstrumentSpec spec = FetchInstrumentSpec(rest_client, kBtcUsdt);
        Log.Info("BTC-USDT: tickSz={} lotSz={} minSz={}", spec.tick_sz, spec.lot_sz, spec.min_sz);

        // This demo account only has ETH-USDT enabled for trading — the
        // quoter runs on ETH-USDT below rather than BTC-USDT for that
        // reason, reusing this tick size.
        const InstrumentSpec eth_spec = FetchInstrumentSpec(rest_client, kEthUsdt);

        const long long eth_usdt_inst_id_code = FetchInstIdCode(rest_client, auth, kEthUsdt);
        Log.Info("ETH-USDT instIdCode={}", eth_usdt_inst_id_code);

        const OrderRequest order_request{
            .inst_id = std::string(kEthUsdt),
            .side = std::string(kBuy),
            .ord_type = std::string(kLimit),
            .px = std::string(kSmokeTestPx),
            .sz = std::string(kSmokeTestSz),
            .inst_id_code = eth_usdt_inst_id_code,
        };
        const OrderResult place_result = PlaceOrder(rest_client, auth, order_request);
        Log.Info("Place order result: accepted={} ordId={} sCode={} sMsg={}", place_result.accepted,
                 place_result.ord_id, place_result.s_code, place_result.s_msg);

        if (place_result.accepted) {
            const OrderResult cancel_result =
                CancelOrder(rest_client, auth, order_request.inst_id, place_result.ord_id);
            Log.Info("Cancel order result: accepted={} sCode={} sMsg={}", cancel_result.accepted,
                     cancel_result.s_code, cancel_result.s_msg);
        }

        asio::io_context io_context;
        OkxWsClient private_ws_client(io_context, std::string(kOkxWsHost), std::string(kOkxWsPort),
                                      std::string(kPrivateWsPath), auth);

        const std::unique_ptr<MarketDataSource> market_data =
            MakeMarketDataSource(config.market_data_mode, io_context, auth, eth_usdt_inst_id_code);
        Log.Info("Market data mode: {}", market_data->Name());

        OrderStore order_store;
        KillSwitch kill_switch(rest_client, auth, order_store);
        EndpointRateLimiter rate_limiter(kOrderRateLimit, kCancelRateLimit, kAmendRateLimit);
        NaiveQuoter quoter(private_ws_client, order_store, rate_limiter, std::string(kEthUsdt),
                           eth_usdt_inst_id_code, std::string(kQuoterSz), eth_spec.tick_sz,
                           kQuoterBps, kRequoteThresholdBps);
        order_store.SetOnRemove(
            [&quoter](std::string_view cl_ord_id) { quoter.OnOrderRemoved(cl_ord_id); });

        private_ws_client.SetOnTraceResolved([&tick_to_trade_stats](const TickToTradeTrace& trace) {
            tick_to_trade_stats.Record(trace);
        });

        market_data->SetOnBookUpdate([&quoter, &kill_switch, &private_ws_client](
                                         const OrderBook& book, TickToTradeTrace trace) {
            if (!kill_switch.IsTriggered() && private_ws_client.IsAuthenticated()) {
                quoter.OnBookUpdate(book, trace);
            }
        });

        AccountState account_state;
        WsResponseDemux ws_demux;
        WsOrderRoundTrip order_round_trip(private_ws_client, ws_demux, rest_client, auth,
                                          order_request, order_store);
        OrderTimeoutMonitor order_timeout_monitor(io_context, order_store, rest_client, auth);
        order_timeout_monitor.Start();
        Position position;

        asio::steady_timer quoter_timer(io_context);
        std::function<void()> schedule_quoter_timer = [&quoter_timer, &schedule_quoter_timer,
                                                       &kill_switch, &private_ws_client,
                                                       &quoter]() {
            quoter_timer.expires_after(kQuoterTimerInterval);
            quoter_timer.async_wait([&kill_switch, &private_ws_client, &quoter,
                                     &schedule_quoter_timer](const boost::system::error_code& ec) {
                if (ec) {
                    return;
                }
                if (!kill_switch.IsTriggered() && private_ws_client.IsAuthenticated()) {
                    quoter.OnTimer();
                }
                schedule_quoter_timer();
            });
        };
        schedule_quoter_timer();

        asio::steady_timer stats_log_timer(io_context);
        std::function<void()> schedule_stats_log_timer =
            [&stats_log_timer, &schedule_stats_log_timer, &tick_to_trade_stats]() {
                stats_log_timer.expires_after(kStatsLogInterval);
                stats_log_timer.async_wait([&tick_to_trade_stats, &schedule_stats_log_timer](
                                               const boost::system::error_code& ec) {
                    if (ec) {
                        return;
                    }
                    tick_to_trade_stats.LogSummary();
                    schedule_stats_log_timer();
                });
            };
        schedule_stats_log_timer();

        asio::signal_set shutdown_signals(io_context, SIGINT, SIGTERM);
        shutdown_signals.async_wait([&io_context, &kill_switch, &tick_to_trade_stats](
                                        const boost::system::error_code& ec, int) {
            if (ec) {
                return;
            }

            tick_to_trade_stats.LogSummary();
            kill_switch.Trigger("manual (signal)");
            io_context.stop();
        });

        private_ws_client.SetOnMessage([&private_ws_client, &rest_client, &auth, &account_state,
                                        &ws_demux, &order_round_trip, &order_store, &kill_switch,
                                        &position, &order_request,
                                        &quoter](std::string_view message) {
            if (json::FindString(message, kEvent) == kLoginEvent) {
                const auto code = json::FindString(message, kCode).value_or(kEmpty);
                Log.Info("Private WS login: code={} msg={}", code,
                         json::FindString(message, kMsg).value_or(kEmpty));
                if (code == kSuccessCode) {
                    private_ws_client.Send(std::string(kPrivateSubscribeMsg));
                    Log.Info("Sent orders, account and positions WS subscribe request");

                    const HttpResponse pending = GetPendingOrders(rest_client, auth);
                    Log.Debug("Pending orders reconciliation: {}", pending.body);
                    ReconcileOrders(order_store, pending.body);

                    if (kill_switch.IsTriggered()) {
                        Log.Warn("Kill switch is triggered so we will skip order placement");
                    } else {
                        const RiskCheckResult risk_check = CheckPreTradeRisk(
                            kRiskLimits, order_request, order_store, std::nullopt);
                        if (!risk_check.passed) {
                            Log.Warn("Pre-trade risk check failed: {}", risk_check.reason);
                        } else {
                            order_round_trip.Start();
                        }
                    }
                }
                return;
            }

            if (ws_demux.Dispatch(message)) {
                return;
            }

            const auto channel = json::FindString(message, kChannel);
            if (channel == kOrdersChannel) {
                ForEachOrderEvent(message, [&order_round_trip, &position, &kill_switch,
                                            &quoter](const OrderEvent& event) {
                    LogOrderEvent(event);
                    if (event.type == OrderEventType::kFill ||
                        event.type == OrderEventType::kPartialFill) {
                        if (event.fill) {
                            quoter.OnFill(event);
                            position.ApplyFill(event.side, event.fill->px, event.fill->sz);
                            Log.Info("Current position: netQty={} avgEntryPx={} realizedPnl={}",
                                     position.NetQty(), position.AvgEntryPx(),
                                     position.RealizedPnl());
                            if (position.RealizedPnl() < -kMaxRealizedLoss) {
                                kill_switch.Trigger("Max realized loss breached");
                            }
                        } else {
                            Log.Debug(
                                "Fill-state order event with no new fill data (state resend), "
                                "ignoring");
                        }
                    } else if (event.type == OrderEventType::kCancel) {
                        quoter.OnCancel(event);
                    }

                    order_round_trip.ApplyOrderEvent(event);
                });
            } else if (channel == kAccountChannel) {
                account_state.ApplyMessage(message);
                account_state.ForEachBalance(
                    [](const AccountState::Balance& balance) { LogAccountBalance(balance); });
            } else {
                Log.Debug("Private WS message from neither orders nor account channel: {}",
                          message);
            }
        });

        market_data->Start();
        private_ws_client.Start();
        while (!io_context.stopped()) {
            io_context.poll();
        }
        return 0;
    } catch (const std::exception& e) {
        Log.Error("Fatal exception causing main to crash: {}", e.what());
        tick_to_trade_stats.LogSummary();
        return 1;
    }
}
