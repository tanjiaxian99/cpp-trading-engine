#include "okx/risk.hpp"

#include <cmath>
#include <format>

#include "util/json.hpp"

RiskCheckResult CheckPreTradeRisk(const RiskLimits& limits, const OrderRequest& request,
                                  const OrderStore& store,
                                  std::optional<double> maybe_reference_px) {
    const double sz = json::ParseDouble(request.sz);
    const double px = json::ParseDouble(request.px);

    if (sz > limits.max_order_sz) {
        return {.passed = false,
                .reason = std::format("Order size {} exceeds max {}", sz, limits.max_order_sz)};
    }

    const double notional = sz * px;
    if (notional > limits.max_notional) {
        return {.passed = false,
                .reason = std::format("Notional {} exceeds max {}", notional, limits.max_notional)};
    }

    if (maybe_reference_px.has_value()) {
        const double ref_px = *maybe_reference_px;
        const double deviation = std::abs(px - ref_px) / ref_px;
        if (deviation > limits.price_collar_pct) {
            return {.passed = false,
                    .reason = std::format(
                        "px {} is {:.2f}% from reference px {}, exceeds collar {:.2f}%", px,
                        deviation * 100.0, ref_px, limits.price_collar_pct * 100.0)};
        }
    }

    if (store.Size() >= limits.max_open_orders) {
        return {.passed = false,
                .reason = std::format("Open order count {} exceeds limit of {} orders",
                                      store.Size(), limits.max_open_orders)};
    }

    return {.passed = true, .reason = ""};
}
