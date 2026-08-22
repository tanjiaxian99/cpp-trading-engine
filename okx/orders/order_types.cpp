#include "okx/orders/order_types.hpp"

#include <algorithm>
#include <chrono>

std::string GenerateId() {
    static long long last_id = 0;

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    last_id = std::max(last_id + 1, now_ms);
    return std::to_string(last_id);
}
