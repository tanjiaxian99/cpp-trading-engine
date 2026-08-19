#pragma once

#include <string_view>

// Weighted-average-cost-basis position tracker for a single instrument
class Position {
public:
    void ApplyFill(std::string_view side, double fill_px, double fill_sz);

    [[nodiscard]] double NetQty() const {
        // Negative for short positions
        return net_qty_;
    }
    [[nodiscard]] double AvgEntryPx() const {
        return avg_entry_px_;
    }
    [[nodiscard]] double RealizedPnl() const {
        return realized_pnl_;
    }
    [[nodiscard]] double UnrealizedPnl(double mark_px) const {
        return net_qty_ * (mark_px - avg_entry_px_);
    }

private:
    double net_qty_ = 0.0;
    double avg_entry_px_ = 0.0;
    double realized_pnl_ = 0.0;
};
