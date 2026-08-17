#pragma once

#include <string>

// Parameters for placing an order — shared by the REST (rest_orders.hpp)
// and WS (ws_orders.hpp) order-entry paths.
struct OrderRequest {
    std::string inst_id;
    std::string side;
    std::string ord_type;
    std::string px;
    std::string sz;
    long long inst_id_code = 0;
};

// Monotonically increasing decimal
std::string GenerateId();
