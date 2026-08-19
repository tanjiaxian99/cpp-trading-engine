#include "okx/kill_switch.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "okx/rest_orders.hpp"

KillSwitch::KillSwitch(RestClient& rest_client, const OkxAuth& auth, OrderStore& order_store)
    : rest_client_(rest_client), auth_(auth), order_store_(order_store) {}

void KillSwitch::Trigger(std::string_view reason) {
    if (triggered_) {
        return;
    }
    triggered_ = true;
    std::cerr << "KILL SWITCH triggered: " << reason << "\n";

    std::vector<std::pair<std::string, std::string>> to_cancel;  // (inst_id, ord_id)
    order_store_.ForEach([&to_cancel](const Order& order) {
        if (order.OrdId().empty()) {
            std::cerr << "Kill switch: order " << order.ClOrdId()
                      << " has no ordId yet, cannot REST-cancel\n";
            return;
        }
        to_cancel.emplace_back(order.InstId(), order.OrdId());
    });

    for (const auto& [inst_id, ord_id] : to_cancel) {
        const OrderResult result = CancelOrder(rest_client_, auth_, inst_id, ord_id);
        std::cerr << "kill switch: cancel ordId=" << ord_id << " accepted=" << result.accepted
                  << " sMsg=" << result.s_msg << "\n";
    }
}
