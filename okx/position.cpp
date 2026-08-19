#include "okx/position.hpp"
#include <algorithm>
#include "okx/okx_constants.hpp"

void Position::ApplyFill(std::string_view side, double fill_px, double fill_sz) {
    const double signed_qty = (side == kBuy) ? fill_sz : -fill_sz;

    // Adding positions so no P&L is realised
    if (net_qty_ == 0.0 || (net_qty_ > 0.0) == (signed_qty > 0.0)) {
        const double new_qty = net_qty_ + signed_qty;
        avg_entry_px_ =
            (std::abs(net_qty_) * avg_entry_px_ + fill_sz * fill_px) / std::abs(new_qty);
        net_qty_ = new_qty;
        return;
    }

    // Closing positions so P&L is realised
    const double closing_qty = std::min(fill_sz, std::abs(net_qty_));
    const double pnl_per_unit =
        (net_qty_ > 0.0) ? (fill_px - avg_entry_px_) : (avg_entry_px_ - fill_px);
    realized_pnl_ += pnl_per_unit * closing_qty;

    const double new_qty = net_qty_ + signed_qty;
    if (new_qty == 0.0) {
        avg_entry_px_ = 0.0;
    } else if ((new_qty > 0.0) != (net_qty_ > 0.0)) {
        // The new fill closed positions but also opened new ones
        avg_entry_px_ = fill_px;
    }
    net_qty_ = new_qty;
}
