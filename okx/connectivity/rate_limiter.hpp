#pragma once

#include <chrono>
#include <cstddef>
#include <string_view>
#include <vector>

class SlidingWindowLimiter {
public:
    SlidingWindowLimiter(std::size_t capacity, std::chrono::milliseconds window);

    [[nodiscard]] bool TryAcquire();

private:
    std::size_t capacity_;
    std::chrono::milliseconds window_;
    std::vector<std::chrono::steady_clock::time_point> timestamps_;
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

struct RateLimit {
    std::size_t capacity;
    std::chrono::milliseconds window;
};

class EndpointRateLimiter {
public:
    EndpointRateLimiter(RateLimit order_limit, RateLimit cancel_limit, RateLimit amend_limit);

    // op must be one of kOrderOp/kCancelOrderOp/kAmendOrderOp.
    [[nodiscard]] bool TryAcquire(std::string_view op);

private:
    SlidingWindowLimiter order_limiter_;
    SlidingWindowLimiter cancel_limiter_;
    SlidingWindowLimiter amend_limiter_;
};
