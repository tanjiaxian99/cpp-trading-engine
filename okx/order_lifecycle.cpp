#include "okx/order_lifecycle.hpp"

#include <format>
#include <stdexcept>
#include <utility>

std::string_view ToString(OrderState state) {
    switch (state) {
        case OrderState::kPendingNew:
            return "PendingNew";
        case OrderState::kNew:
            return "New";
        case OrderState::kPartiallyFilled:
            return "PartiallyFilled";
        case OrderState::kFilled:
            return "Filled";
        case OrderState::kCanceled:
            return "Canceled";
        case OrderState::kRejected:
            return "Rejected";
        case OrderState::kPendingCancel:
            return "PendingCancel";
        case OrderState::kPendingAmend:
            return "PendingAmend";
    }
    throw std::runtime_error("Unrecognized OrderState");
}

Order::Order(std::string cl_ord_id, std::string inst_id, std::string side, std::string px,
             std::string sz)
    : cl_ord_id_(std::move(cl_ord_id)),
      inst_id_(std::move(inst_id)),
      side_(std::move(side)),
      px_(std::move(px)),
      sz_(std::move(sz)) {}

void Order::TransitionTo(OrderState next) {
    state_ = next;
}

void Order::ApplyEvent(const OrderEvent& event) {
    if (ord_id_.empty()) {
        ord_id_ = std::string(event.ord_id);
    }
    acc_fill_sz_ = std::string(event.acc_fill_sz);
    avg_px_ = std::string(event.avg_px);

    // WS push might come after we have performed state transitions. Regardless,
    // we will treat those as stale states and not throw
    switch (event.type) {
        case OrderEventType::kLive:
            if (state_ == OrderState::kPendingNew) {
                TransitionTo(OrderState::kNew);
            } else if (state_ == OrderState::kPendingAmend) {
                // kLive returned for successful amend
                px_ = std::string(event.px);
                // If the order was at kPendingNew, it is now live
                // If it was kNew/kPartiallyFilled, leave it as such
                TransitionTo(pre_pending_state_ == OrderState::kPendingNew ? OrderState::kNew
                                                                           : pre_pending_state_);
            } else if (state_ == OrderState::kNew || state_ == OrderState::kPartiallyFilled ||
                       state_ == OrderState::kPendingCancel) {
                px_ = std::string(event.px);
            } else {
                throw std::runtime_error(std::format("Order {}: unexpected {} -> kLive transition",
                                                     cl_ord_id_, ToString(state_)));
            }
            break;

        case OrderEventType::kPartialFill:
            if (state_ == OrderState::kFilled || state_ == OrderState::kCanceled ||
                state_ == OrderState::kRejected) {
                throw std::runtime_error(
                    std::format("Order {}: unexpected {} -> kPartialFill transition", cl_ord_id_,
                                ToString(state_)));
            }
            TransitionTo(OrderState::kPartiallyFilled);
            break;

        case OrderEventType::kFill:
            if (state_ == OrderState::kFilled || state_ == OrderState::kCanceled ||
                state_ == OrderState::kRejected) {
                throw std::runtime_error(std::format("Order {}: unexpected {} -> kFill transition",
                                                     cl_ord_id_, ToString(state_)));
            }
            TransitionTo(OrderState::kFilled);
            break;

        case OrderEventType::kReject:
            if (state_ == OrderState::kFilled || state_ == OrderState::kCanceled ||
                state_ == OrderState::kRejected) {
                throw std::runtime_error(std::format(
                    "Order {}: unexpected {} -> kReject transition", cl_ord_id_, ToString(state_)));
            }

            if (state_ == OrderState::kPendingNew) {
                TransitionTo(OrderState::kRejected);
            } else {
                TransitionTo(OrderState::kCanceled);
            }
            break;
    }
}

void Order::OnCancelRequested() {
    if (state_ != OrderState::kPendingNew && state_ != OrderState::kNew &&
        state_ != OrderState::kPartiallyFilled) {
        throw std::runtime_error(
            std::format("Order {}: cancel requested from state {}", cl_ord_id_, ToString(state_)));
    }
    pre_pending_state_ = state_;
    TransitionTo(OrderState::kPendingCancel);
}

void Order::OnAmendRequested() {
    if (state_ != OrderState::kPendingNew && state_ != OrderState::kNew &&
        state_ != OrderState::kPartiallyFilled) {
        throw std::runtime_error(
            std::format("Order {}: amend requested from state {}", cl_ord_id_, ToString(state_)));
    }
    pre_pending_state_ = state_;
    TransitionTo(OrderState::kPendingAmend);
}

void Order::OnRequestRejected() {
    if (state_ != OrderState::kPendingCancel && state_ != OrderState::kPendingAmend) {
        throw std::runtime_error(std::format("Order {}: request-rejected event from state {}",
                                             cl_ord_id_, ToString(state_)));
    }
    TransitionTo(pre_pending_state_);
}
