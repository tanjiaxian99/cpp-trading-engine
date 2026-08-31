#include "okx/orders/order_timeout_monitor.hpp"

#include <chrono>

#include "log/async_logger.hpp"
#include "okx/orders/order_lifecycle.hpp"
#include "okx/orders/reconciliation.hpp"
#include "okx/orders/rest_orders.hpp"

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
        Log.Warn("Order {} unacknowledged for over {}s (state={})", order.ClOrdId(),
                 kOrderUnacknowledgedTimeout.count(), ToString(order.State()));
        any_timed_out = true;
    });

    if (any_timed_out) {
        const HttpResponse pending = GetPendingOrders(rest_client_, auth_);
        Log.Warn("Reconciliation triggered by unacknowledged pending orders: {}", pending.body);
        ReconcileOrders(order_store_, pending.body);
    }
}
