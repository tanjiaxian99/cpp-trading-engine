#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "okx/order_types.hpp"

struct WsOrderRequest {
    std::string id;
    std::string message;
};

struct WsBatchOrderRequest {
    std::string id;
    std::string message;
    std::vector<std::string> cl_ord_ids;
};

WsOrderRequest BuildWsOrderMessage(const OrderRequest& request);
WsOrderRequest BuildWsCancelOrderMessage(long long inst_id_code, std::string_view ord_id);
WsOrderRequest BuildWsAmendOrderMessage(long long inst_id_code, std::string_view ord_id,
                                        std::string_view new_px, std::string_view new_sz);
WsBatchOrderRequest BuildWsBatchOrdersMessage(const std::vector<OrderRequest>& requests);
