#pragma once

#include <boost/asio.hpp>

#include "okx/connectivity/auth.hpp"
#include "okx/orders/order_store.hpp"
#include "rest/rest_client.hpp"

namespace asio = boost::asio;

class OrderTimeoutMonitor {
public:
    OrderTimeoutMonitor(asio::io_context& io_context, OrderStore& order_store,
                        RestClient& rest_client, const OkxAuth& auth);
    void Start();

private:
    void ScheduleCheck();
    void CheckAndResolve();

    asio::steady_timer timer_;
    OrderStore& order_store_;
    RestClient& rest_client_;
    const OkxAuth& auth_;
};
