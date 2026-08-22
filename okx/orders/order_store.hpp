#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "okx/orders/order_lifecycle.hpp"

class OrderStore {
public:
    static constexpr std::size_t kCapacity = 1024;

    void Add(std::string cl_ord_id, std::string inst_id, std::string side, std::string px,
             std::string sz);
    void Remove(std::string_view cl_ord_id);
    // The store should not be mutated during the loop-through to avoid shifting orders around
    void ForEach(const std::function<void(Order&)>& action);

    [[nodiscard]] Order* FindByClOrdId(std::string_view cl_ord_id);
    [[nodiscard]] Order* FindByOrdId(std::string_view ord_id);
    [[nodiscard]] std::size_t Size() const {
        return count_;
    }

private:
    [[nodiscard]] std::size_t LowerBound(std::string_view cl_ord_id);
    [[nodiscard]] Order& At(std::size_t i);

    std::array<std::optional<Order>, kCapacity> slots_{};
    std::size_t count_ = 0;
};
