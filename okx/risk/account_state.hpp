#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class AccountState {
public:
    struct Balance {
        std::string ccy;
        double cash_bal = 0.0;
        double avail_bal = 0.0;
    };

    void ApplyMessage(std::string_view message);
    [[nodiscard]] std::optional<Balance> GetBalance(std::string_view ccy) const;
    void ForEachBalance(const std::function<void(const Balance&)>& action) const;

private:
    static constexpr std::size_t kCapacity = 16;
    std::array<Balance, kCapacity> balances_{};
    std::size_t count_ = 0;
};
