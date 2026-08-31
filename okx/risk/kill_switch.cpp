#include "okx/risk/kill_switch.hpp"

#include <string>
#include <utility>
#include <vector>

#include "log/async_logger.hpp"
#include "okx/orders/rest_orders.hpp"

KillSwitch::KillSwitch(RestClient& rest_client, const OkxAuth& auth, OrderStore& order_store)
    : rest_client_(rest_client), auth_(auth), order_store_(order_store) {}

void KillSwitch::Trigger(std::string_view reason) {
    if (triggered_) {
        return;
    }
    triggered_ = true;
    Log.Error("KILL SWITCH triggered: {}", reason);

    std::vector<std::pair<std::string, std::string>> to_cancel;  // (inst_id, ord_id)
    order_store_.ForEach([&to_cancel](const Order& order) {
        if (order.OrdId().empty()) {
            Log.Warn("Order {} has no ordId yet, cannot REST-cancel", order.ClOrdId());
            return;
        }
        to_cancel.emplace_back(order.InstId(), order.OrdId());
    });

    for (const auto& [inst_id, ord_id] : to_cancel) {
        const OrderResult result = CancelOrder(rest_client_, auth_, inst_id, ord_id);
        Log.Info("Order cancel status: ordId={} accepted={} sMsg={}", ord_id, result.accepted,
                 result.s_msg);
    }
}
