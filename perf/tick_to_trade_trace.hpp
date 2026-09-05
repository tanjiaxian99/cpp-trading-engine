#pragma once

#include <cstdint>

struct TickToTradeTrace {
    std::uint64_t wire_arrival_ticks = 0;
    std::uint64_t message_decoded_ticks = 0;
    std::uint64_t book_consistent_ticks = 0;
    std::uint64_t message_built_ticks = 0;
    std::uint64_t send_ticks = 0;
};
