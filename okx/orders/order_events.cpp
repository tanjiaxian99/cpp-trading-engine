#include "okx/orders/order_events.hpp"

#include <format>
#include <stdexcept>

#include "okx/common/okx_constants.hpp"
#include "util/json.hpp"

namespace {
OrderEventType ClassifyState(std::string_view state) {
    if (state == kStateLive) {
        return OrderEventType::kLive;
    }
    if (state == kStatePartiallyFilled) {
        return OrderEventType::kPartialFill;
    }
    if (state == kStateFilled) {
        return OrderEventType::kFill;
    }
    if (state == kStateCanceled || state == kStateMmpCanceled) {
        return OrderEventType::kReject;
    }
    throw std::runtime_error(std::format("Unrecognized order state: {}", state));
}
}  // namespace

void ForEachOrderEvent(std::string_view message,
                       const std::function<void(const OrderEvent&)>& action) {
    if (json::FindString(message, kChannel) != kOrdersChannel) {
        return;
    }

    json::ForEachArrayElement(message, kData, [&action](std::string_view order) {
        const auto state = json::FindString(order, kState);
        if (!state) {
            throw std::runtime_error(std::format("Malformed orders channel message: {}", order));
        }

        const OrderEvent event{
            .type = ClassifyState(*state),
            .ord_id = json::FindString(order, kOrdId).value_or(kEmpty),
            .cl_ord_id = json::FindString(order, kClOrdId).value_or(kEmpty),
            .inst_id = json::FindString(order, kInstId).value_or(kEmpty),
            .side = json::FindString(order, kSide).value_or(kEmpty),
            .px = json::FindString(order, kPx).value_or(kEmpty),
            .sz = json::FindString(order, kSz).value_or(kEmpty),
            .acc_fill_sz = json::FindString(order, kAccFillSz).value_or(kEmpty),
            .avg_px = json::FindString(order, kAvgPx).value_or(kEmpty),
            .fill_px = json::FindString(order, kFillPx).value_or(kEmpty),
            .fill_sz = json::FindString(order, kFillSz).value_or(kEmpty),
        };
        action(event);
    });
}

std::string_view ToString(OrderEventType type) {
    switch (type) {
        case OrderEventType::kLive:
            return "live";
        case OrderEventType::kPartialFill:
            return "partial-fill";
        case OrderEventType::kFill:
            return "fill";
        case OrderEventType::kReject:
            return "reject";
    }
    throw std::runtime_error("Unrecognized OrderEventType");
}
