#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "net/websocket_frame.hpp"

std::string EncodeClose(std::uint16_t status_code, std::string_view reason = "");
std::optional<std::string> BuildControlResponse(const WebSocketFrame& frame);