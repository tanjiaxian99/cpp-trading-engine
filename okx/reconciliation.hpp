#pragma once

#include <string_view>

#include "okx/order_store.hpp"

void ReconcileOrders(OrderStore& store, std::string_view pending_orders_response);
