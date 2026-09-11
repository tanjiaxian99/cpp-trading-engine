#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "net/transport.hpp"

void PerformWebSocketHandshake(Transport& transport, std::string_view host, std::string_view path,
                               const std::vector<std::string>& extra_headers = {});
