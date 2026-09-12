#pragma once

#include "okx/connectivity/auth.hpp"
#include "okx/connectivity/instrument.hpp"
#include "okx/orders/order_types.hpp"
#include "rest/rest_client.hpp"

struct BootstrapResult {
    InstrumentSpec eth_spec;
    long long eth_usdt_inst_id_code = 0;
    OrderRequest order_request;
};

BootstrapResult RunStartupChecks(RestClient& rest_client, const OkxAuth& auth);
