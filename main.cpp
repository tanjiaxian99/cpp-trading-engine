#include <boost/asio.hpp>

#include <chrono>
#include <exception>
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
#include "okx/ws_client.hpp"
#include "okx/ws_orders.hpp"
#include "rest/rest_client.hpp"
#include "util/json.hpp"

namespace {
// Extracts the server timestamp from /public/time's response, e.g.
// {"code":"0","data":[{"ts":"1786204129995"}],"msg":""}, using the
// general-purpose scanner (json::FindArrayElement + json::FindString)
// rather than a one-off ad-hoc lookup.
long long ExtractTimestampMs(const std::string& public_time_body) {
    const auto data = json::FindArrayElement(public_time_body, kData, 0);
    if (!data) {
        throw std::runtime_error("Could not find data[0] in /public/time response");
    }
    const auto ts = json::FindString(*data, kTs);
    if (!ts) {
        throw std::runtime_error("Could not find ts field in /public/time response");
    }
    return std::stoll(std::string(*ts));
}
}  // namespace

int main() {
    try {
        const Config config = Config::FromEnv();
        std::cout << "loaded config for key " << config.api_key << "\n";

        RestClient rest_client("https://www.okx.com");

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
        const InstrumentSpec spec = FetchInstrumentSpec(rest_client, "BTC-USDT");
        std::cout << "BTC-USDT: tickSz=" << spec.tick_sz << " lotSz=" << spec.lot_sz
                  << " minSz=" << spec.min_sz << "\n";

        const long long eth_usdt_inst_id_code = FetchInstIdCode(rest_client, auth, "ETH-USDT");
        std::cout << "ETH-USDT instIdCode=" << eth_usdt_inst_id_code << "\n";

        const OrderRequest order_request{
            .inst_id = "ETH-USDT",
            .side = std::string(kBuy),
            .ord_type = std::string(kLimit),
            .px = "100",
            .sz = "0.01",
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
        OkxWsClient ws_client(io_context, "wspap.okx.com", "8443", "/ws/v5/public");

        OrderBook order_book;

        const std::string subscribe_msg =
            R"({"op":"subscribe","args":)"
            R"([{"channel":"books","instId":"BTC-USDT"},{"channel":"trades","instId":"BTC-USDT"}]})";
        const std::string books_unsubscribe_msg =
            R"({"op":"unsubscribe","args":[{"channel":"books","instId":"BTC-USDT"}]})";
        const std::string books_subscribe_msg =
            R"({"op":"subscribe","args":[{"channel":"books","instId":"BTC-USDT"}]})";

        ws_client.SetOnConnected([&ws_client, &subscribe_msg]() {
            ws_client.Send(subscribe_msg);
            std::cout << "sent books+trades subscribe request\n";
        });
        ws_client.SetOnMessage([&order_book, &ws_client, &books_unsubscribe_msg,
                                &books_subscribe_msg](std::string_view message) {
            switch (ApplyBookMessage(message, order_book)) {
                case BookMessageResult::kApplied:
                    std::cout << "book: bid=" << order_book.BestBid().value_or(0.0)
                              << " ask=" << order_book.BestAsk().value_or(0.0)
                              << " seqId=" << order_book.LastSeqId() << "\n";
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

        OkxWsClient private_ws_client(io_context, "wspap.okx.com", "8443", "/ws/v5/private");
        AccountState account_state;
        // Tracks the ordId across the WS order -> amend -> cancel smoke-test
        // chain below, ahead of B4's proper response-demux item.
        std::string ws_ord_id;

        const std::string private_subscribe_msg =
            R"({"op":"subscribe","args":)"
            R"([{"channel":"orders","instType":"SPOT"},{"channel":"account"},)"
            R"({"channel":"positions","instType":"ANY"}]})";

        private_ws_client.SetOnConnected([&private_ws_client, &auth]() {
            private_ws_client.Send(auth.BuildWsLoginMessage());
            std::cout << "sent private WS login request\n";
        });

        private_ws_client.SetOnMessage(
            [&private_ws_client, &private_subscribe_msg, &rest_client, &auth, &account_state,
             &order_request, &eth_usdt_inst_id_code, &ws_ord_id](std::string_view message) {
                if (json::FindString(message, kEvent) == kLoginEvent) {
                    const auto code = json::FindString(message, kCode).value_or(kEmpty);
                    std::cout << "private WS login: code=" << code
                              << " msg=" << json::FindString(message, kMsg).value_or(kEmpty)
                              << "\n";
                    if (code == kSuccessCode) {
                        private_ws_client.Send(private_subscribe_msg);
                        std::cout << "sent orders/account/positions subscribe request\n";

                        const HttpResponse pending = GetPendingOrders(rest_client, auth);
                        std::cout << "pending orders reconciliation: " << pending.body << "\n";

                        const WsOrderRequest ws_order = BuildWsOrderMessage(order_request);
                        private_ws_client.Send(ws_order.message);
                        std::cout << "sent WS order request id=" << ws_order.id << "\n";
                    }
                    return;
                }

                const auto op = json::FindString(message, kOp);
                if (op == kOrderOp || op == kAmendOrderOp || op == kCancelOrderOp) {
                    std::cout << "WS " << *op << " response: " << message << "\n";

                    if (op == kOrderOp) {
                        const auto data = json::FindArrayElement(message, kData, 0);
                        const auto ord_id = data ? json::FindString(*data, kOrdId) : std::nullopt;
                        if (ord_id) {
                            ws_ord_id = std::string(*ord_id);
                            const WsOrderRequest amend = BuildWsAmendOrderMessage(
                                eth_usdt_inst_id_code, ws_ord_id, "101", order_request.sz);
                            private_ws_client.Send(amend.message);
                            std::cout << "sent WS amend-order request id=" << amend.id << "\n";
                        }
                    } else if (op == kAmendOrderOp) {
                        const WsOrderRequest cancel =
                            BuildWsCancelOrderMessage(eth_usdt_inst_id_code, ws_ord_id);
                        private_ws_client.Send(cancel.message);
                        std::cout << "sent WS cancel-order request id=" << cancel.id << "\n";
                    }
                    return;
                }

                const auto channel = json::FindString(message, kChannel);
                if (channel == kOrdersChannel) {
                    ForEachOrderEvent(message, [](const OrderEvent& event) {
                        std::cout << "order event: type=" << ToString(event.type)
                                  << " ordId=" << event.ord_id << " instId=" << event.inst_id
                                  << " side=" << event.side << " px=" << event.px
                                  << " sz=" << event.sz << " accFillSz=" << event.acc_fill_sz
                                  << " avgPx=" << event.avg_px << "\n";
                    });
                } else if (channel == kAccountChannel) {
                    account_state.ApplyMessage(message);
                    account_state.ForEachBalance([](const AccountState::Balance& balance) {
                        std::cout << "account: ccy=" << balance.ccy
                                  << " cashBal=" << balance.cash_bal
                                  << " availBal=" << balance.avail_bal << "\n";
                    });
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
