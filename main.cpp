#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "net/transport.hpp"
#include "net/websocket_handshake.hpp"
#include "okx/auth.hpp"
#include "okx/instrument.hpp"
#include "okx/okx_constants.hpp"
#include "okx/orders.hpp"
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
        throw std::runtime_error("could not find data[0] in /public/time response");
    }
    const auto ts = json::FindString(*data, kTs);
    if (!ts) {
        throw std::runtime_error("could not find ts field in /public/time response");
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

        const std::string ws_host = "wspap.okx.com";
        Transport transport(ws_host, "8443");
        transport.Connect();
        std::cout << "TLS connected to " << ws_host << ":8443\n";

        PerformWebSocketHandshake(transport, ws_host, "/ws/v5/public");
        std::cout << "WebSocket handshake complete\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
