#pragma once

#include <algorithm>
#include <array>
#include <boost/asio/buffer.hpp>
#include <cstddef>

namespace asio = boost::asio;

template <std::size_t N>
class RingBuffer {
public:
    [[nodiscard]] asio::mutable_buffer WritableRegion() {
        const std::size_t write_pos = (read_pos_ + size_) % N;
        const std::size_t contiguous_free = std::min(N - size_, N - write_pos);
        return asio::buffer(data_.data() + write_pos, contiguous_free);
    }

    void CommitWrite(std::size_t n) {
        size_ += n;
    }

    [[nodiscard]] asio::const_buffer ReadableRegion() const {
        const std::size_t contiguous_available = std::min(size_, N - read_pos_);
        return asio::buffer(data_.data() + read_pos_, contiguous_available);
    }

    void Consume(std::size_t n) {
        read_pos_ = (read_pos_ + n) % N;
        size_ -= n;
    }

    [[nodiscard]] std::size_t Size() const {
        return size_;
    }

    [[nodiscard]] std::size_t FreeSpace() const {
        return N - size_;
    }

    [[nodiscard]] static constexpr std::size_t Capacity() {
        return N;
    }

private:
    std::array<std::byte, N> data_{};
    std::size_t read_pos_ = 0;
    std::size_t size_ = 0;
};
