#include "okx/heartbeat.hpp"

#include "okx/okx_constants.hpp"

std::string EncodeOkxPing() {
    return EncodeFrame(WebSocketOpcode::kText, kOkxPingText);
}

bool IsOkxPong(const WebSocketFrame& frame) {
    return frame.header.opcode == WebSocketOpcode::kText && frame.payload == kOkxPongText;
}
