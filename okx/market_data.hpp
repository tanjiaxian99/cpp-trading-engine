#pragma once

#include <string_view>

#include "okx/order_book.hpp"

bool ApplyBookMessage(std::string_view message, OrderBook& book);
