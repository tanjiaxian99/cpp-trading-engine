#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

enum class OrderEventType : std::uint8_t {
    kLive,
    kPartialFill,
    kFill,
    kReject,
};

struct OrderEvent {
    OrderEventType type;
    std::string_view ord_id;
    std::string_view cl_ord_id;
    std::string_view inst_id;
    std::string_view side;
    std::string_view px;
    std::string_view sz;
    std::string_view acc_fill_sz;
    std::string_view avg_px;
    std::string_view fill_px;
    std::string_view fill_sz;
};

void ForEachOrderEvent(std::string_view message,
                       const std::function<void(const OrderEvent&)>& action);
[[nodiscard]] std::string_view ToString(OrderEventType type);
