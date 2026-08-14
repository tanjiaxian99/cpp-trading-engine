#include "okx/order_book.hpp"

void OrderBook::BeginSnapshot() {
    bids_.clear();
    asks_.clear();
    has_snapshot_ = true;
}

void OrderBook::SetBid(double price, double size) {
    if (size == 0.0) {
        bids_.erase(price);
    } else {
        bids_[price] = size;
    }
}

void OrderBook::SetAsk(double price, double size) {
    if (size == 0.0) {
        asks_.erase(price);
    } else {
        asks_[price] = size;
    }
}

void OrderBook::SetSeqId(long long seq_id) {
    last_seq_id_ = seq_id;
}

std::optional<double> OrderBook::BestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<double> OrderBook::BestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}
