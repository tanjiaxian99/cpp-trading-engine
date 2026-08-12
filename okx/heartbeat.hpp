#pragma once

#include <string>

#include "net/websocket_frame.hpp"

std::string EncodeOkxPing();
bool IsOkxPong(const WebSocketFrame& frame);
