#include <boost/asio.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "okx/auth.hpp"
#include "okx/instrument.hpp"
#include "okx/market_data.hpp"
#include "okx/okx_constants.hpp"
#include "okx/order_book.hpp"
#include "okx/orders.hpp"
#include "okx/ws_client.hpp"
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

        const OrderRequest order_request{
            .inst_id = "ETH-USDT",
            .side = std::string(kBuy),
            .ord_type = std::string(kLimit),
            .px = "100",
            .sz = "0.01",
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

        ws_client.Start();
        io_context.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
