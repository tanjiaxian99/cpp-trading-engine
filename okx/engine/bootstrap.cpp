#include "okx/engine/bootstrap.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

#include "log/async_logger.hpp"
#include "okx/common/instruments.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/common/response_utils.hpp"
#include "okx/orders/rest_orders.hpp"
#include "util/json.hpp"

namespace {
constexpr std::string_view kSmokeTestPx = "100";
constexpr std::string_view kSmokeTestSz = "0.01";

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
}  // namespace

BootstrapResult RunStartupChecks(RestClient& rest_client, const OkxAuth& auth) {
    const HttpResponse time_response = rest_client.Get("/api/v5/public/time");
    Log.Debug("REST status {}: {}", time_response.status_code, time_response.body);

    const long long server_time_ms = ExtractTimestampMs(time_response.body);
    const auto local_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
    Log.Info("Clock drift: {} ms", local_time_ms - server_time_ms);

    using enum HttpMethod;
    const std::string balance_path = "/api/v5/account/balance";
    const HttpResponse balance_response =
        rest_client.Get(balance_path, auth.SignHeaders(kGet, balance_path));
    Log.Debug("Balance status {}: {}", balance_response.status_code, balance_response.body);

    const InstrumentSpec btc_spec = FetchInstrumentSpec(rest_client, kBtcUsdt);
    Log.Info("BTC-USDT: tickSz={} lotSz={} minSz={}", btc_spec.tick_sz, btc_spec.lot_sz,
             btc_spec.min_sz);

    BootstrapResult result;
    result.eth_spec = FetchInstrumentSpec(rest_client, kEthUsdt);
    result.eth_usdt_inst_id_code = FetchInstIdCode(rest_client, auth, kEthUsdt);
    Log.Info("ETH-USDT instIdCode={}", result.eth_usdt_inst_id_code);

    result.order_request = OrderRequest{
        .inst_id = std::string(kEthUsdt),
        .side = std::string(kBuy),
        .ord_type = std::string(kLimit),
        .px = std::string(kSmokeTestPx),
        .sz = std::string(kSmokeTestSz),
        .inst_id_code = result.eth_usdt_inst_id_code,
    };
    const OrderResult place_result = PlaceOrder(rest_client, auth, result.order_request);
    Log.Info("Place order result: accepted={} ordId={} sCode={} sMsg={}", place_result.accepted,
             place_result.ord_id, place_result.s_code, place_result.s_msg);

    if (place_result.accepted) {
        const OrderResult cancel_result =
            CancelOrder(rest_client, auth, result.order_request.inst_id, place_result.ord_id);
        Log.Info("Cancel order result: accepted={} sCode={} sMsg={}", cancel_result.accepted,
                 cancel_result.s_code, cancel_result.s_msg);
    }

    return result;
}
