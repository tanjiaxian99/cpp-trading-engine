#include "okx/order_store.hpp"

#include <format>
#include <stdexcept>
#include <utility>

void OrderStore::Add(std::string cl_ord_id, std::string inst_id, std::string side, std::string px,
                     std::string sz) {
    if (count_ >= kCapacity) {
        throw std::runtime_error("OrderStore is full");
    }

    const std::size_t i = LowerBound(cl_ord_id);
    for (std::size_t j = count_; j > i; j--) {
        slots_[j] = std::move(slots_[j - 1]);
    }
    slots_[i].emplace(std::move(cl_ord_id), std::move(inst_id), std::move(side), std::move(px),
                      std::move(sz));
    count_++;
}

void OrderStore::Remove(std::string_view cl_ord_id) {
    const std::size_t i = LowerBound(cl_ord_id);
    if (i >= count_ || At(i).ClOrdId() != cl_ord_id) {
        return;
    }

    for (std::size_t j = i; j + 1 < count_; j++) {
        slots_[j] = std::move(slots_[j + 1]);
    }
    slots_[count_ - 1].reset();
    count_--;
}

Order* OrderStore::FindByClOrdId(std::string_view cl_ord_id) {
    const std::size_t i = LowerBound(cl_ord_id);
    if (i < count_ && At(i).ClOrdId() == cl_ord_id) {
        return &At(i);
    }
    return nullptr;
}

Order* OrderStore::FindByOrdId(std::string_view ord_id) {
    for (std::size_t i = 0; i < count_; i++) {
        if (At(i).OrdId() == ord_id) {
            return &At(i);
        }
    }
    return nullptr;
}

std::size_t OrderStore::LowerBound(std::string_view cl_ord_id) {
    std::size_t begin = 0;
    std::size_t end = count_;
    while (begin < end) {
        const std::size_t mid = begin + (end - begin) / 2;
        if (At(mid).ClOrdId() < cl_ord_id) {
            begin = mid + 1;
        } else {
            end = mid;
        }
    }
    return begin;
}

Order& OrderStore::At(std::size_t i) {
    auto& slot = slots_[i];
    if (!slot) {
        throw std::runtime_error(std::format("OrderStore slot {} unexpectedly empty", i));
    }
    return *slot;
}
