#include "okx/marketdata/order_book.hpp"

void OrderBook::BeginSnapshot() {
    bids_.Clear();
    asks_.Clear();
    has_snapshot_ = true;
}

void OrderBook::SetBid(double price, double size) {
    bids_.Set(price, size);
}

void OrderBook::SetAsk(double price, double size) {
    asks_.Set(price, size);
}

void OrderBook::SetSeqId(long long seq_id) {
    last_seq_id_ = seq_id;
}
