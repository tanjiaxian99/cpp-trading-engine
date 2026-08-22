#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include "okx/orders/order_events.hpp"

enum class OrderState : std::uint8_t {
    kPendingNew,
    kNew,
    kPartiallyFilled,
    kFilled,
    kCanceled,
    kRejected,
    kPendingCancel,
    kPendingAmend,
};

[[nodiscard]] std::string_view ToString(OrderState state);
[[nodiscard]] bool IsTerminal(OrderState state);
[[nodiscard]] bool IsPending(OrderState state);

class Order {
public:
    Order(std::string cl_ord_id, std::string inst_id, std::string side, std::string px,
          std::string sz);

    void ApplyEvent(const OrderEvent& event);
    void OnCancelRequested();
    void OnAmendRequested();
    void OnRequestRejected();

    [[nodiscard]] OrderState State() const {
        return state_;
    }
    [[nodiscard]] const std::string& OrdId() const {
        return ord_id_;
    }
    [[nodiscard]] const std::string& ClOrdId() const {
        return cl_ord_id_;
    }
    [[nodiscard]] const std::string& InstId() const {
        return inst_id_;
    }
    [[nodiscard]] const std::string& Px() const {
        return px_;
    }
    [[nodiscard]] const std::string& Sz() const {
        return sz_;
    }
    [[nodiscard]] const std::string& AccFillSz() const {
        return acc_fill_sz_;
    }
    [[nodiscard]] const std::string& AvgPx() const {
        return avg_px_;
    }
    [[nodiscard]] std::chrono::steady_clock::time_point PendingSince() const {
        return pending_since_;
    }

private:
    void TransitionTo(OrderState next);

    std::string ord_id_;
    std::string cl_ord_id_;
    std::string inst_id_;
    std::string side_;
    std::string px_;
    std::string sz_;
    std::string acc_fill_sz_;
    std::string avg_px_;

    OrderState state_ = OrderState::kPendingNew;
    // Revert to this state if the in-flight cancel/amend request is rejected
    OrderState pre_pending_state_ = OrderState::kPendingNew;
    std::chrono::steady_clock::time_point pending_since_ = std::chrono::steady_clock::now();
};
