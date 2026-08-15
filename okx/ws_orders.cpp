#include "okx/ws_orders.hpp"

#include <format>
#include <stdexcept>

#include "okx/okx_constants.hpp"

namespace {
constexpr std::string_view kOrderArgFormat =
    R"({{"instIdCode":{},"tdMode":"{}","side":"{}","ordType":"{}","px":"{}","sz":"{}",)"
    R"("clOrdId":"{}"}})";
constexpr std::string_view kOrderMessageFormat = R"({{"id":"{}","op":"order","args":[{}]}})";
constexpr std::string_view kCancelOrderMessageFormat =
    R"({{"id":"{}","op":"cancel-order","args":[{{"instIdCode":{},"ordId":"{}"}}]}})";
constexpr std::string_view kAmendOrderMessageFormat =
    R"({{"id":"{}","op":"amend-order","args":[{{"instIdCode":{},"ordId":"{}","newPx":"{}",)"
    R"("newSz":"{}"}}]}})";
constexpr std::string_view kBatchOrdersMessageFormat =
    R"({{"id":"{}","op":"batch-orders","args":[{}]}})";

std::string FormatOrderArg(const OrderRequest& request, std::string_view cl_ord_id) {
    if (request.ord_type == kMarket) {
        throw std::invalid_argument("Market orders are not yet supported");
    }
    return std::format(kOrderArgFormat, request.inst_id_code, kCash, request.side, request.ord_type,
                       request.px, request.sz, cl_ord_id);
}
}  // namespace

WsOrderRequest BuildWsOrderMessage(const OrderRequest& request) {
    const std::string id = GenerateId();
    std::string message = std::format(kOrderMessageFormat, id, FormatOrderArg(request, id));
    return WsOrderRequest{.id = id, .message = std::move(message)};
}

WsOrderRequest BuildWsCancelOrderMessage(long long inst_id_code, std::string_view ord_id) {
    const std::string id = GenerateId();
    std::string message = std::format(kCancelOrderMessageFormat, id, inst_id_code, ord_id);
    return WsOrderRequest{.id = id, .message = std::move(message)};
}

WsOrderRequest BuildWsAmendOrderMessage(long long inst_id_code, std::string_view ord_id,
                                        std::string_view new_px, std::string_view new_sz) {
    const std::string id = GenerateId();
    std::string message =
        std::format(kAmendOrderMessageFormat, id, inst_id_code, ord_id, new_px, new_sz);
    return WsOrderRequest{.id = id, .message = std::move(message)};
}

WsBatchOrderRequest BuildWsBatchOrdersMessage(const std::vector<OrderRequest>& requests) {
    if (requests.empty()) {
        throw std::invalid_argument("Batch order request must include at least one order");
    }

    std::vector<std::string> cl_ord_ids;
    cl_ord_ids.reserve(requests.size());

    std::string args_json;
    for (const auto& request : requests) {
        const std::string cl_ord_id = GenerateId();
        args_json += FormatOrderArg(request, cl_ord_id);
        args_json += ",";
        cl_ord_ids.push_back(cl_ord_id);
    }
    args_json.pop_back();  // Drop the trailing comma left by the loop above.

    const std::string id = GenerateId();
    std::string message = std::format(kBatchOrdersMessageFormat, id, args_json);

    return WsBatchOrderRequest{
        .id = id,
        .message = std::move(message),
        .cl_ord_ids = std::move(cl_ord_ids),
    };
}
