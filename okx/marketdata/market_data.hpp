#pragma once

#include <cstdint>
#include <string_view>

#include "okx/marketdata/order_book.hpp"

enum class BookMessageResult : std::uint8_t { kIgnored, kApplied, kGapDetected };

BookMessageResult ApplyBookMessage(std::string_view message, OrderBook& book);
