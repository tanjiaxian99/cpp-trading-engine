#pragma once

#include <string>
#include <string_view>

#include "okx/auth.hpp"
#include "okx/order_types.hpp"
#include "rest/rest_client.hpp"

struct OrderResult {
    bool accepted = false;
    std::string ord_id;
    std::string cl_ord_id;
    std::string code;
    std::string s_code;
    std::string s_msg;
};

OrderResult PlaceOrder(RestClient& rest_client, const OkxAuth& auth, const OrderRequest& request);
OrderResult CancelOrder(RestClient& rest_client, const OkxAuth& auth, std::string_view inst_id,
                        std::string_view ord_id);
HttpResponse GetPendingOrders(RestClient& rest_client, const OkxAuth& auth);
