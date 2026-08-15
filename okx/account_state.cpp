#include "okx/account_state.hpp"

#include <format>
#include <stdexcept>

#include "okx/okx_constants.hpp"
#include "util/json.hpp"

void AccountState::ApplyMessage(std::string_view message) {
    if (json::FindString(message, kChannel) != kAccountChannel) {
        return;
    }

    const auto maybe_data = json::FindArrayElement(message, kData, 0);
    if (!maybe_data) {
        return;
    }

    json::ForEachArrayElement(*maybe_data, kDetails, [this](std::string_view detail) {
        const auto ccy = json::FindString(detail, kCcy);
        const auto cash_bal = json::FindString(detail, kCashBal);
        const auto avail_bal = json::FindString(detail, kAvailBal);
        if (!ccy || !cash_bal || !avail_bal) {
            throw std::runtime_error(std::format("Malformed account channel detail: {}", detail));
        }

        for (std::size_t i = 0; i < count_; i++) {
            if (balances_[i].ccy == *ccy) {
                balances_[i].cash_bal = json::ParseDouble(*cash_bal);
                balances_[i].avail_bal = json::ParseDouble(*avail_bal);
                return;
            }
        }
        if (count_ < kCapacity) {
            balances_[count_++] = Balance{
                .ccy = std::string(*ccy),
                .cash_bal = json::ParseDouble(*cash_bal),
                .avail_bal = json::ParseDouble(*avail_bal),
            };
        }
    });
}

std::optional<AccountState::Balance> AccountState::GetBalance(std::string_view ccy) const {
    for (std::size_t i = 0; i < count_; i++) {
        if (balances_[i].ccy == ccy) {
            return balances_[i];
        }
    }
    return std::nullopt;
}

void AccountState::ForEachBalance(const std::function<void(const Balance&)>& action) const {
    for (std::size_t i = 0; i < count_; i++) {
        action(balances_[i]);
    }
}
