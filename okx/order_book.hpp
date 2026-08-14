#pragma once

#include <map>
#include <optional>

class OrderBook {
public:
    void BeginSnapshot();
    void SetBid(double price, double size);
    void SetAsk(double price, double size);
    void SetSeqId(long long seq_id);

    [[nodiscard]] bool HasSnapshot() const {
        return has_snapshot_;
    }

    [[nodiscard]] long long LastSeqId() const {
        return last_seq_id_;
    }

    [[nodiscard]] std::optional<double> BestBid() const;
    [[nodiscard]] std::optional<double> BestAsk() const;

private:
    std::map<double, double, std::greater<>> bids_;
    std::map<double, double> asks_;
    long long last_seq_id_ = -1;
    bool has_snapshot_ = false;
};
