#pragma once

#include <cstdint>

struct TickToTradeTrace {
    std::uint64_t wire_arrival_ticks = 0;
    std::uint64_t message_decoded_ticks = 0;
    std::uint64_t book_consistent_ticks = 0;
    std::uint64_t rate_limit_checked_ticks = 0;
    std::uint64_t price_formatted_ticks = 0;
    std::uint64_t message_built_ticks = 0;
    std::uint64_t send_ticks = 0;
};
