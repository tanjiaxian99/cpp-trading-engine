#include "okx/order_timeout_monitor.hpp"

#include <chrono>
#include <iostream>

#include "okx/order_lifecycle.hpp"
#include "okx/reconciliation.hpp"
#include "okx/rest_orders.hpp"

namespace {
constexpr auto kOrderUnacknowledgedTimeout = std::chrono::seconds(5);
constexpr auto kOrderTimeoutCheckInterval = std::chrono::seconds(1);
}  // namespace

OrderTimeoutMonitor::OrderTimeoutMonitor(asio::io_context& io_context, OrderStore& order_store,
                                         RestClient& rest_client, const OkxAuth& auth)
    : timer_(io_context), order_store_(order_store), rest_client_(rest_client), auth_(auth) {}

void OrderTimeoutMonitor::Start() {
    ScheduleCheck();
}

void OrderTimeoutMonitor::ScheduleCheck() {
    timer_.expires_after(kOrderTimeoutCheckInterval);
    timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        CheckAndResolve();
        ScheduleCheck();
    });
}

void OrderTimeoutMonitor::CheckAndResolve() {
    bool any_timed_out = false;
    const auto now = std::chrono::steady_clock::now();
    order_store_.ForEach([&any_timed_out, now](const Order& order) {
        if (any_timed_out || !IsPending(order.State()) ||
            now - order.PendingSince() <= kOrderUnacknowledgedTimeout) {
            return;
        }
        std::cerr << "Order " << order.ClOrdId() << " unacknowledged for over "
                  << kOrderUnacknowledgedTimeout.count() << "s (state=" << ToString(order.State())
                  << ")\n";
        any_timed_out = true;
    });

    if (any_timed_out) {
        const HttpResponse pending = GetPendingOrders(rest_client_, auth_);
        std::cout << "Reconciliation triggered by unacknowledged pending orders : " << pending.body
                  << "\n";
        ReconcileOrders(order_store_, pending.body);
    }
}
