#include "okx/order_types.hpp"

#include <chrono>

std::string GenerateId() {
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    return std::to_string(now_ms);
}
