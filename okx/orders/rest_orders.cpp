#include "okx/orders/rest_orders.hpp"

#include <format>
#include <optional>
#include <stdexcept>

#include "okx/common/okx_constants.hpp"
#include "okx/common/response_utils.hpp"
#include "util/json.hpp"

using enum HttpMethod;

namespace {
constexpr std::string_view kOrderPath = "/api/v5/trade/order";
constexpr std::string_view kCancelOrderPath = "/api/v5/trade/cancel-order";
constexpr std::string_view kPendingOrdersPath = "/api/v5/trade/orders-pending";

constexpr std::string_view kOrderBodyFormat =
    R"({{"instId":"{}","tdMode":"{}","side":"{}","ordType":"{}","px":"{}","sz":"{}","clOrdId":"{}"}})";
constexpr std::string_view kCancelOrderBodyFormat = R"({{"instId":"{}","ordId":"{}"}})";

// json::Find* returns std::nullopt when a field is absent — this
// collapses that into an owned std::string, defaulting to kEmpty rather
// than leaving OrderResult's fields with no value to report at all.
std::string ValueOrEmpty(std::optional<std::string_view> value) {
    return std::string(value ? *value : kEmpty);
}

// Parses OKX's two-level response envelope shared by order placement and
// cancellation: request-level code/msg, plus this order's own sCode/sMsg
// inside data[0]. Never throws — a missing/malformed field just leaves
// the corresponding OrderResult field empty and accepted=false.
OrderResult ParseOrderResult(const HttpResponse& response) {
    OrderResult result;

    result.code = ValueOrEmpty(json::FindString(response.body, kCode));

    const auto maybe_data = FindData(response.body);
    if (!maybe_data) {
        return result;  // request-level failure — nothing order-specific to report
    }
    const std::string_view data = *maybe_data;

    result.ord_id = ValueOrEmpty(json::FindString(data, kOrdId));
    result.cl_ord_id = ValueOrEmpty(json::FindString(data, kClOrdId));
    result.s_code = ValueOrEmpty(json::FindString(data, kSCode));
    result.s_msg = ValueOrEmpty(json::FindString(data, kSMsg));
    result.accepted = result.code == kSuccessCode && result.s_code == kSuccessCode;
    return result;
}
}  // namespace

OrderResult PlaceOrder(RestClient& rest_client, const OkxAuth& auth, const OrderRequest& request) {
    if (request.ord_type == kMarket) {
        throw std::invalid_argument("Market orders are not yet supported");
    }

    const std::string cl_ord_id = GenerateId();
    const std::string body = std::format(kOrderBodyFormat, request.inst_id, kCash, request.side,
                                         request.ord_type, request.px, request.sz, cl_ord_id);
    const HttpResponse response =
        rest_client.Post(kOrderPath, body, auth.SignHeaders(kPost, kOrderPath, body));
    return ParseOrderResult(response);
}

OrderResult CancelOrder(RestClient& rest_client, const OkxAuth& auth, std::string_view inst_id,
                        std::string_view ord_id) {
    const std::string body = std::format(kCancelOrderBodyFormat, inst_id, ord_id);
    const HttpResponse response =
        rest_client.Post(kCancelOrderPath, body, auth.SignHeaders(kPost, kCancelOrderPath, body));
    return ParseOrderResult(response);
}

HttpResponse GetPendingOrders(RestClient& rest_client, const OkxAuth& auth) {
    return rest_client.Get(kPendingOrdersPath, auth.SignHeaders(kGet, kPendingOrdersPath));
}
