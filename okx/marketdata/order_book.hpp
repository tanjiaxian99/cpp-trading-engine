#pragma once

#include <array>
#include <functional>
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

    [[nodiscard]] std::optional<double> BestBid() const {
        return bids_.Best();
    }

    [[nodiscard]] std::optional<double> BestAsk() const {
        return asks_.Best();
    }

private:
    static constexpr std::size_t kCapacity = 32;

    struct Level {
        double price = 0.0;
        double size = 0.0;
    };

    template <typename Compare>
    class PriceLevels {
    public:
        void Clear() {
            count_ = 0;
        }

        void Set(double price, double size) {
            const std::size_t i = LowerBound(price);
            const bool found = i < count_ && levels_[i].price == price;

            if (found) {
                if (size == 0.0) {
                    RemoveAt(i);
                } else {
                    levels_[i].size = size;
                }
                return;
            }
            InsertAt(i, Level{.price = price, .size = size});
        }

        [[nodiscard]] std::optional<double> Best() const {
            if (count_ == 0) {
                return std::nullopt;
            }
            return levels_[0].price;
        }

    private:
        [[nodiscard]] std::size_t LowerBound(double price) const {
            std::size_t begin = 0;
            // Use count_ because count_ is a valid return value when we want to insert at the end
            std::size_t end = count_;
            while (begin < end) {
                const std::size_t mid = begin + (end - begin) / 2;
                if (Compare{}(levels_[mid].price, price)) {
                    begin = mid + 1;
                } else {
                    end = mid;
                }
            }
            return begin;
        }

        void InsertAt(std::size_t i, Level level) {
            if (i >= kCapacity) {
                return;
            }

            const std::size_t last = (count_ == kCapacity) ? count_ - 1 : count_;
            for (std::size_t j = last; j > i; j--) {
                levels_[j] = levels_[j - 1];
            }
            levels_[i] = level;
            if (count_ < kCapacity) {
                count_++;
            }
        }

        void RemoveAt(std::size_t i) {
            for (std::size_t j = i; j + 1 < count_; j++) {
                levels_[j] = levels_[j + 1];
            }
            count_--;
        }

        std::array<Level, kCapacity> levels_{};
        std::size_t count_ = 0;
    };

    PriceLevels<std::greater<>> bids_;
    PriceLevels<std::less<>> asks_;
    long long last_seq_id_ = -1;
    bool has_snapshot_ = false;
};
