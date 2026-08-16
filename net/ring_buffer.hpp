#pragma once

#include <algorithm>
#include <array>
#include <boost/asio/buffer.hpp>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace asio = boost::asio;

template <std::size_t N>
class RingBuffer {
public:
    void Write(std::string_view data) {
        if (data.size() > FreeSpace()) {
            throw std::runtime_error("RingBuffer overflow");
        }

        std::size_t offset = 0;
        while (offset < data.size()) {  // At most 2 passes to copy into the ring buffer
            const auto region = ContiguousWritableRegion();
            const std::size_t n = std::min(region.size(), data.size() - offset);
            std::memcpy(region.data(), data.data() + offset, n);
            size_ += n;
            offset += n;
        }
    }

    [[nodiscard]] asio::const_buffer ContiguousReadableRegion() const {
        const std::size_t contiguous_available = std::min(size_, N - read_pos_);
        return asio::buffer(data_.data() + read_pos_, contiguous_available);
    }

    void CommitRead(std::size_t n) {
        read_pos_ = (read_pos_ + n) % N;
        size_ -= n;
    }

    void Reset() {
        read_pos_ = 0;
        size_ = 0;
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
    [[nodiscard]] asio::mutable_buffer ContiguousWritableRegion() {
        const std::size_t write_pos = (read_pos_ + size_) % N;
        const std::size_t contiguous_free = std::min(N - size_, N - write_pos);
        return asio::buffer(data_.data() + write_pos, contiguous_free);
    }

    std::array<std::byte, N> data_{};
    std::size_t read_pos_ = 0;
    std::size_t size_ = 0;
};
