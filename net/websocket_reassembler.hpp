#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

template <std::size_t N>
class WebSocketReassembler {
public:
    void Append(std::string_view payload, bool fin) {
        if (payload.size() > N - size_) {
            throw std::runtime_error("WebSocket message exceeds reassembly buffer capacity");
        }
        payload.copy(data_.data() + size_, payload.size());
        size_ += payload.size();
        complete_ = fin;
    }

    [[nodiscard]] bool IsComplete() const {
        return complete_;
    }

    [[nodiscard]] std::string_view GetMessage() const {
        return std::string_view(data_.data(), size_);
    }

    void Reset() {
        size_ = 0;
        complete_ = false;
    }

private:
    std::array<char, N> data_{};
    std::size_t size_ = 0;
    bool complete_ = false;
};
