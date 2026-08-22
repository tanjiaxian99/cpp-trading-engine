#include "okx/connectivity/heartbeat.hpp"

#include "okx/common/okx_constants.hpp"

std::string EncodeOkxPing() {
    return EncodeFrame(WebSocketOpcode::kText, kOkxPingText);
}

bool IsOkxPong(const WebSocketFrame& frame) {
    return frame.header.opcode == WebSocketOpcode::kText && frame.payload == kOkxPongText;
}
