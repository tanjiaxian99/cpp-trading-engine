#pragma once

#include <string_view>

#include "net/transport.hpp"

void PerformWebSocketHandshake(Transport& transport, std::string_view host, std::string_view path);
