#include "okx/reconciliation.hpp"

#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "okx/okx_constants.hpp"
#include "okx/order_events.hpp"
#include "util/json.hpp"

namespace {
OrderEventType ClassifyPendingState(std::string_view state) {
    if (state == kStateLive) {
        return OrderEventType::kLive;
    }
    if (state == kStatePartiallyFilled) {
        return OrderEventType::kPartialFill;
    }
    throw std::runtime_error(std::format("Unexpected pending-order state: {}", state));
}
}  // namespace

void ReconcileOrders(OrderStore& store, std::string_view pending_orders_response) {
    std::unordered_set<std::string> exchange_cl_ord_ids;

    json::ForEachArrayElement(
        pending_orders_response, kData, [&store, &exchange_cl_ord_ids](std::string_view order) {
            const auto cl_ord_id = json::FindString(order, kClOrdId);
            const auto state = json::FindString(order, kState);
            if (!cl_ord_id || !state) {
                throw std::runtime_error(std::format("Malformed pending order: {}", order));
            }
            exchange_cl_ord_ids.emplace(*cl_ord_id);

            if (store.FindByClOrdId(*cl_ord_id)) {
                return;
            }

            const OrderEvent event{
                .type = ClassifyPendingState(*state),
                .ord_id = json::FindString(order, kOrdId).value_or(kEmpty),
                .cl_ord_id = *cl_ord_id,
                .inst_id = json::FindString(order, kInstId).value_or(kEmpty),
                .side = json::FindString(order, kSide).value_or(kEmpty),
                .px = json::FindString(order, kPx).value_or(kEmpty),
                .sz = json::FindString(order, kSz).value_or(kEmpty),
                .acc_fill_sz = json::FindString(order, kAccFillSz).value_or(kEmpty),
                .avg_px = json::FindString(order, kAvgPx).value_or(kEmpty),
            };

            std::cout << "Reconciliation: adopting exchange-known order clOrdId=" << *cl_ord_id
                      << " state=" << *state << "\n";
            store.Add(std::string(event.cl_ord_id), std::string(event.inst_id),
                      std::string(event.side), std::string(event.px), std::string(event.sz));

            Order* adopted = store.FindByClOrdId(*cl_ord_id);
            if (!adopted) {
                throw std::runtime_error(std::format(
                    "Reconciliation: order {} vanished immediately after Add", *cl_ord_id));
            }
            adopted->ApplyEvent(event);
        });

    std::vector<std::string> to_drop;
    store.ForEach([&exchange_cl_ord_ids, &to_drop](Order& order) {
        if (!exchange_cl_ord_ids.contains(order.ClOrdId())) {
            to_drop.push_back(order.ClOrdId());
        }
    });

    for (const auto& cl_ord_id : to_drop) {
        std::cout << "Reconciliation: local order clOrdId=" << cl_ord_id
                  << " no longer pending on exchange, dropping\n";
        store.Remove(cl_ord_id);
    }
}
