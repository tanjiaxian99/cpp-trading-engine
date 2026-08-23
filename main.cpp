#include <boost/asio.hpp>

#include <chrono>
#include <csignal>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/common/response_utils.hpp"
#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/instrument.hpp"
#include "okx/connectivity/okx_ws_client.hpp"
#include "okx/connectivity/rate_limiter.hpp"
#include "okx/connectivity/ws_response_demux.hpp"
#include "okx/marketdata/market_data.hpp"
#include "okx/marketdata/order_book.hpp"
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

constexpr std::string_view kBooksSubscribeFormat =
    R"({{"op":"subscribe","args":[{{"channel":"books","instId":"{0}"}},)"
    R"({{"channel":"trades","instId":"{0}"}}]}})";
constexpr std::string_view kBooksUnsubscribeFormat =
    R"({{"op":"unsubscribe","args":[{{"channel":"books","instId":"{}"}}]}})";
constexpr std::string_view kBooksResubscribeFormat =
    R"({{"op":"subscribe","args":[{{"channel":"books","instId":"{}"}}]}})";
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
constexpr double kQuoterBps = 20.0;
constexpr double kRequoteThresholdBps = 10.0;
constexpr RateLimit kOrderRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr RateLimit kCancelRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr RateLimit kAmendRateLimit{.capacity = 60, .window = std::chrono::seconds(2)};
constexpr auto kQuoterTimerInterval = std::chrono::seconds(2);

void LogBookUpdate(const OrderBook& book) {
    std::cout << "book: bid=" << book.BestBid().value_or(0.0)
              << " ask=" << book.BestAsk().value_or(0.0) << " seqId=" << book.LastSeqId() << "\n";
}

void LogOrderEvent(const OrderEvent& event) {
    std::cout << "order event: type=" << ToString(event.type) << " ordId=" << event.ord_id
              << " instId=" << event.inst_id << " side=" << event.side << " px=" << event.px
              << " sz=" << event.sz << " accFillSz=" << event.acc_fill_sz
              << " avgPx=" << event.avg_px << "\n";
}

void LogAccountBalance(const AccountState::Balance& balance) {
    std::cout << "account: ccy=" << balance.ccy << " cashBal=" << balance.cash_bal
              << " availBal=" << balance.avail_bal << "\n";
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
            std::cout << "private WS down — falling back to REST for order placement\n";
            const OrderResult result = PlaceOrder(rest_client_, auth_, order_request_);
            std::cout << "REST fallback place order: accepted=" << result.accepted
                      << " ordId=" << result.ord_id << " sCode=" << result.s_code
                      << " sMsg=" << result.s_msg << "\n";
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
        std::cout << "sent WS order request id=" << order.id << "\n";
        demux_.Track(order.id, [this](std::string_view response) { OnOrderResponse(response); });
    }

    void ApplyOrderEvent(const OrderEvent& event) {
        Order* order = order_store_.FindByClOrdId(event.cl_ord_id);
        if (!order) {
            return;
        }

        order->ApplyEvent(event);
        std::cout << "order state: " << ToString(order->State()) << "\n";
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
        std::cout << "WS order response: " << response << "\n";
        if (!IsRequestAccepted(response)) {
            // Rejected before ever existing on the exchange — Order has no
            // transition for this from kPendingNew. Leave it for
            // reconciliation/timeout cleanup rather than proceeding to
            // amend/cancel an order that was never placed.
            std::cout << "order placement rejected, leaving cleanup to reconciliation\n";
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
        std::cout << "sent WS amend-order request id=" << amend.id << "\n";
        demux_.Track(amend.id, [this](std::string_view response) { OnAmendResponse(response); });
    }

    void OnAmendResponse(std::string_view response) {
        std::cout << "WS amend-order response: " << response << "\n";
        if (!IsRequestAccepted(response)) {
            GetOrder().OnRequestRejected();
            std::cout << "order state: " << ToString(GetOrder().State()) << "\n";
        }

        if (!ws_client_.IsConnected()) {
            FallBackToRestCancel();
            return;
        }

        GetOrder().OnCancelRequested();
        const WsOrderRequest cancel =
            BuildWsCancelOrderMessage(order_request_.inst_id_code, ord_id_);
        ws_client_.Send(cancel.message);
        std::cout << "sent WS cancel-order request id=" << cancel.id << "\n";
        demux_.Track(cancel.id, [this](std::string_view response) { OnCancelResponse(response); });
    }

    void OnCancelResponse(std::string_view response) {
        std::cout << "WS cancel-order response: " << response << "\n";
        if (!IsRequestAccepted(response)) {
            GetOrder().OnRequestRejected();
            std::cout << "order state: " << ToString(GetOrder().State()) << "\n";
        }
    }

    void FallBackToRestCancel() {
        std::cout << "private WS down — falling back to REST for cancel\n";
        const OrderResult result =
            CancelOrder(rest_client_, auth_, order_request_.inst_id, ord_id_);
        std::cout << "REST fallback cancel order: accepted=" << result.accepted
                  << " sCode=" << result.s_code << " sMsg=" << result.s_msg << "\n";
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
    try {
        const Config config = Config::FromEnv();
        std::cout << "loaded config for key " << config.api_key << "\n";

        RestClient rest_client{std::string(kOkxRestBaseUrl)};

        // Public, unauthenticated endpoint — also used below for the clock
        // drift check A3 requires before signed requests can be trusted.
        const HttpResponse time_response = rest_client.Get("/api/v5/public/time");
        std::cout << "REST status " << time_response.status_code << ": " << time_response.body
                  << "\n";

        const long long server_time_ms = ExtractTimestampMs(time_response.body);
        const auto local_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
        std::cout << "clock drift: " << (local_time_ms - server_time_ms) << " ms\n";

        // Signed request — proves the A3 auth/signing path end-to-end.
        using enum HttpMethod;
        const OkxAuth auth(config.api_key, config.api_secret, config.passphrase);
        const std::string balance_path = "/api/v5/account/balance";
        const HttpResponse balance_response =
            rest_client.Get(balance_path, auth.SignHeaders(kGet, balance_path));
        std::cout << "balance status " << balance_response.status_code << ": "
                  << balance_response.body << "\n";

        // Public, unauthenticated endpoint — proves instrument-spec fetch
        // and parsing end-to-end.
        const InstrumentSpec spec = FetchInstrumentSpec(rest_client, kBtcUsdt);
        std::cout << "BTC-USDT: tickSz=" << spec.tick_sz << " lotSz=" << spec.lot_sz
                  << " minSz=" << spec.min_sz << "\n";

        // This demo account only has ETH-USDT enabled for trading — the
        // quoter runs on ETH-USDT below rather than BTC-USDT for that
        // reason, reusing this tick size.
        const InstrumentSpec eth_spec = FetchInstrumentSpec(rest_client, kEthUsdt);

        const long long eth_usdt_inst_id_code = FetchInstIdCode(rest_client, auth, kEthUsdt);
        std::cout << "ETH-USDT instIdCode=" << eth_usdt_inst_id_code << "\n";

        const OrderRequest order_request{
            .inst_id = std::string(kEthUsdt),
            .side = std::string(kBuy),
            .ord_type = std::string(kLimit),
            .px = std::string(kSmokeTestPx),
            .sz = std::string(kSmokeTestSz),
            .inst_id_code = eth_usdt_inst_id_code,
        };
        const OrderResult place_result = PlaceOrder(rest_client, auth, order_request);
        std::cout << "place order: accepted=" << place_result.accepted
                  << " ordId=" << place_result.ord_id << " sCode=" << place_result.s_code
                  << " sMsg=" << place_result.s_msg << "\n";

        if (place_result.accepted) {
            const OrderResult cancel_result =
                CancelOrder(rest_client, auth, order_request.inst_id, place_result.ord_id);
            std::cout << "cancel order: accepted=" << cancel_result.accepted
                      << " sCode=" << cancel_result.s_code << " sMsg=" << cancel_result.s_msg
                      << "\n";
        }

        asio::io_context io_context;
        OkxWsClient ws_client(io_context, std::string(kOkxWsHost), std::string(kOkxWsPort),
                              std::string(kPublicWsPath));
        OkxWsClient private_ws_client(io_context, std::string(kOkxWsHost), std::string(kOkxWsPort),
                                      std::string(kPrivateWsPath), auth);

        OrderBook order_book;
        OrderStore order_store;
        KillSwitch kill_switch(rest_client, auth, order_store);
        EndpointRateLimiter rate_limiter(kOrderRateLimit, kCancelRateLimit, kAmendRateLimit);
        NaiveQuoter quoter(private_ws_client, order_store, rate_limiter, std::string(kEthUsdt),
                           eth_usdt_inst_id_code, std::string(kQuoterSz), eth_spec.tick_sz,
                           kQuoterBps, kRequoteThresholdBps);
        order_store.SetOnRemove(
            [&quoter](std::string_view cl_ord_id) { quoter.OnOrderRemoved(cl_ord_id); });

        const std::string subscribe_msg = std::format(kBooksSubscribeFormat, kEthUsdt);
        const std::string books_unsubscribe_msg = std::format(kBooksUnsubscribeFormat, kEthUsdt);
        const std::string books_subscribe_msg = std::format(kBooksResubscribeFormat, kEthUsdt);

        ws_client.SetOnConnected([&ws_client, &subscribe_msg]() {
            ws_client.Send(subscribe_msg);
            std::cout << "sent books+trades subscribe request\n";
        });
        ws_client.SetOnMessage([&order_book, &ws_client, &books_unsubscribe_msg,
                                &books_subscribe_msg, &quoter, &kill_switch,
                                &private_ws_client](std::string_view message) {
            switch (ApplyBookMessage(message, order_book)) {
                case BookMessageResult::kApplied:
                    LogBookUpdate(order_book);
                    if (!kill_switch.IsTriggered() && private_ws_client.IsAuthenticated()) {
                        quoter.OnBookUpdate(order_book);
                    }
                    break;
                case BookMessageResult::kGapDetected:
                    std::cerr << "Order book sequence gap detected — resubscribing for a fresh "
                                 "snapshot\n";
                    ws_client.Send(books_unsubscribe_msg);
                    ws_client.Send(books_subscribe_msg);
                    break;
                case BookMessageResult::kIgnored:
                    std::cout << "WS message: " << message << "\n";
                    break;
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
        std::function<void()> schedule_quoter_timer = [&]() {
            quoter_timer.expires_after(kQuoterTimerInterval);
            quoter_timer.async_wait([&](const boost::system::error_code& ec) {
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

        asio::signal_set shutdown_signals(io_context, SIGINT, SIGTERM);
        shutdown_signals.async_wait(
            [&io_context, &kill_switch](const boost::system::error_code& ec, int) {
                if (ec) {
                    return;
                }
                kill_switch.Trigger("manual (signal)");
                io_context.stop();
            });

        private_ws_client.SetOnMessage([&private_ws_client, &rest_client, &auth, &account_state,
                                        &ws_demux, &order_round_trip, &order_store, &kill_switch,
                                        &position, &order_request,
                                        &quoter](std::string_view message) {
            if (json::FindString(message, kEvent) == kLoginEvent) {
                const auto code = json::FindString(message, kCode).value_or(kEmpty);
                std::cout << "private WS login: code=" << code
                          << " msg=" << json::FindString(message, kMsg).value_or(kEmpty) << "\n";
                if (code == kSuccessCode) {
                    private_ws_client.Send(std::string(kPrivateSubscribeMsg));
                    std::cout << "sent orders/account/positions subscribe request\n";

                    const HttpResponse pending = GetPendingOrders(rest_client, auth);
                    std::cout << "pending orders reconciliation: " << pending.body << "\n";
                    ReconcileOrders(order_store, pending.body);

                    if (kill_switch.IsTriggered()) {
                        std::cout << "Kill switch is triggered so we will skip order placement\n";
                    } else {
                        const RiskCheckResult risk_check = CheckPreTradeRisk(
                            kRiskLimits, order_request, order_store, std::nullopt);
                        if (!risk_check.passed) {
                            std::cout << "Pre-trade risk check failed: " << risk_check.reason
                                      << "\n";
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
                        quoter.OnFill(event);
                        position.ApplyFill(event.side, json::ParseDouble(event.fill_px),
                                           json::ParseDouble(event.fill_sz));
                        std::cout << "Position: netQty=" << position.NetQty()
                                  << " avgEntryPx=" << position.AvgEntryPx()
                                  << " realizedPnl=" << position.RealizedPnl() << "\n";
                        if (position.RealizedPnl() < -kMaxRealizedLoss) {
                            kill_switch.Trigger("max realized loss breached");
                        }
                    } else if (event.type == OrderEventType::kReject) {
                        quoter.OnReject(event);
                    }

                    order_round_trip.ApplyOrderEvent(event);
                });
            } else if (channel == kAccountChannel) {
                account_state.ApplyMessage(message);
                account_state.ForEachBalance(LogAccountBalance);
            } else {
                std::cout << "private WS message: " << message << "\n";
            }
        });

        ws_client.Start();
        private_ws_client.Start();
        io_context.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
