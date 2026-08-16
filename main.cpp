#include <boost/asio.hpp>

#include <chrono>
#include <exception>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "okx/account_state.hpp"
#include "okx/auth.hpp"
#include "okx/instrument.hpp"
#include "okx/market_data.hpp"
#include "okx/okx_constants.hpp"
#include "okx/order_book.hpp"
#include "okx/order_events.hpp"
#include "okx/orders.hpp"
#include "okx/response_utils.hpp"
#include "okx/ws_client.hpp"
#include "okx/ws_orders.hpp"
#include "okx/ws_response_demux.hpp"
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
    WsOrderRoundTrip(OkxWsClient& ws_client, WsResponseDemux& demux,
                     const OrderRequest& order_request)
        : ws_client_(ws_client), demux_(demux), order_request_(order_request) {}

    void Start() {
        const WsOrderRequest order = BuildWsOrderMessage(order_request_);
        ws_client_.Send(order.message);
        std::cout << "sent WS order request id=" << order.id << "\n";
        demux_.Track(order.id, [this](std::string_view response) { OnOrderResponse(response); });
    }

private:
    void OnOrderResponse(std::string_view response) {
        std::cout << "WS order response: " << response << "\n";
        const auto data = FindData(response);
        const auto ord_id = data ? json::FindString(*data, kOrdId) : std::nullopt;
        if (!ord_id) {
            return;
        }
        ord_id_ = std::string(*ord_id);

        const WsOrderRequest amend = BuildWsAmendOrderMessage(order_request_.inst_id_code, ord_id_,
                                                              kSmokeTestAmendPx, order_request_.sz);
        ws_client_.Send(amend.message);
        std::cout << "sent WS amend-order request id=" << amend.id << "\n";
        demux_.Track(amend.id, [this](std::string_view response) { OnAmendResponse(response); });
    }

    void OnAmendResponse(std::string_view response) {
        std::cout << "WS amend-order response: " << response << "\n";
        const WsOrderRequest cancel =
            BuildWsCancelOrderMessage(order_request_.inst_id_code, ord_id_);
        ws_client_.Send(cancel.message);
        std::cout << "sent WS cancel-order request id=" << cancel.id << "\n";
        demux_.Track(cancel.id, [this](std::string_view response) { OnCancelResponse(response); });
    }

    void OnCancelResponse(std::string_view response) {
        std::cout << "WS cancel-order response: " << response << "\n";
    }

    OkxWsClient& ws_client_;
    WsResponseDemux& demux_;
    const OrderRequest& order_request_;
    std::string ord_id_;
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

        OrderBook order_book;

        const std::string subscribe_msg = std::format(kBooksSubscribeFormat, kBtcUsdt);
        const std::string books_unsubscribe_msg = std::format(kBooksUnsubscribeFormat, kBtcUsdt);
        const std::string books_subscribe_msg = std::format(kBooksResubscribeFormat, kBtcUsdt);

        ws_client.SetOnConnected([&ws_client, &subscribe_msg]() {
            ws_client.Send(subscribe_msg);
            std::cout << "sent books+trades subscribe request\n";
        });
        ws_client.SetOnMessage([&order_book, &ws_client, &books_unsubscribe_msg,
                                &books_subscribe_msg](std::string_view message) {
            switch (ApplyBookMessage(message, order_book)) {
                case BookMessageResult::kApplied:
                    LogBookUpdate(order_book);
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

        OkxWsClient private_ws_client(io_context, std::string(kOkxWsHost), std::string(kOkxWsPort),
                                      std::string(kPrivateWsPath));
        AccountState account_state;
        WsResponseDemux ws_demux;
        WsOrderRoundTrip order_round_trip(private_ws_client, ws_demux, order_request);

        private_ws_client.SetOnConnected([&private_ws_client, &auth]() {
            private_ws_client.Send(auth.BuildWsLoginMessage());
            std::cout << "sent private WS login request\n";
        });

        private_ws_client.SetOnMessage([&private_ws_client, &rest_client, &auth, &account_state,
                                        &ws_demux, &order_round_trip](std::string_view message) {
            if (json::FindString(message, kEvent) == kLoginEvent) {
                const auto code = json::FindString(message, kCode).value_or(kEmpty);
                std::cout << "private WS login: code=" << code
                          << " msg=" << json::FindString(message, kMsg).value_or(kEmpty) << "\n";
                if (code == kSuccessCode) {
                    private_ws_client.Send(std::string(kPrivateSubscribeMsg));
                    std::cout << "sent orders/account/positions subscribe request\n";

                    const HttpResponse pending = GetPendingOrders(rest_client, auth);
                    std::cout << "pending orders reconciliation: " << pending.body << "\n";

                    order_round_trip.Start();
                }
                return;
            }

            if (ws_demux.Dispatch(message)) {
                return;
            }

            const auto channel = json::FindString(message, kChannel);
            if (channel == kOrdersChannel) {
                ForEachOrderEvent(message, LogOrderEvent);
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
