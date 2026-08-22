#pragma once

#include <optional>
#include <string>

#include "okx/orders/order_store.hpp"
#include "okx/orders/order_types.hpp"

struct RiskLimits {
    double max_order_sz = 0.0;
    double max_notional = 0.0;
    double price_collar_pct = 0.0;
    std::size_t max_open_orders = 0;
};

struct RiskCheckResult {
    bool passed = false;
    std::string reason;
};

[[nodiscard]] RiskCheckResult CheckPreTradeRisk(const RiskLimits& limits,
                                                const OrderRequest& request,
                                                const OrderStore& store,
                                                std::optional<double> maybe_reference_px);
