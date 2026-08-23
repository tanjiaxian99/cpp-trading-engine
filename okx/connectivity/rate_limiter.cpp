#include "okx/connectivity/rate_limiter.hpp"

#include <format>
#include <stdexcept>

#include "okx/common/okx_constants.hpp"

SlidingWindowLimiter::SlidingWindowLimiter(std::size_t capacity, std::chrono::milliseconds window)
    : capacity_(capacity), window_(window), timestamps_(capacity) {}

bool SlidingWindowLimiter::TryAcquire() {
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - window_;

    while (count_ > 0 && timestamps_[head_] < cutoff) {
        head_ = (head_ + 1) % capacity_;
        --count_;
    }

    if (count_ >= capacity_) {
        return false;
    }

    timestamps_[(head_ + count_) % capacity_] = now;
    count_++;
    return true;
}

EndpointRateLimiter::EndpointRateLimiter(RateLimit order_limit, RateLimit cancel_limit,
                                         RateLimit amend_limit)
    : order_limiter_(order_limit.capacity, order_limit.window),
      cancel_limiter_(cancel_limit.capacity, cancel_limit.window),
      amend_limiter_(amend_limit.capacity, amend_limit.window) {}

bool EndpointRateLimiter::TryAcquire(std::string_view op) {
    if (op == kOrderOp) {
        return order_limiter_.TryAcquire();
    }
    if (op == kCancelOrderOp) {
        return cancel_limiter_.TryAcquire();
    }
    if (op == kAmendOrderOp) {
        return amend_limiter_.TryAcquire();
    }
    throw std::invalid_argument(std::format("Unknown rate-limited op: {}", op));
}
