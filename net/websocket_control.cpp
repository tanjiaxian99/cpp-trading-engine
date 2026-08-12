#include "net/websocket_control.hpp"

#include <stdexcept>

namespace {
constexpr std::size_t kMaxControlPayload = 125;
constexpr std::size_t kCloseStatusCodeBytes = 2;

void ValidateControlPayloadSize(std::size_t size) {
    if (size > kMaxControlPayload) {
        throw std::invalid_argument("WebSocket control frame payload exceeds 125-byte limit");
    }
}
} // namespace

std::string EncodeClose(std::uint16_t status_code, std::string_view reason) {
    ValidateControlPayloadSize(kCloseStatusCodeBytes + reason.size());

    std::string body;
    body.push_back(static_cast<char>(status_code >> 8));
    body.push_back(static_cast<char>(status_code));
    body += reason;

    return EncodeFrame(WebSocketOpcode::kClose, body);
}

std::optional<std::string> BuildControlResponse(const WebSocketFrame& frame) {
    using enum WebSocketOpcode;
    switch (frame.header.opcode) {
        case kPing:
            return EncodeFrame(kPong, frame.payload);
        case kClose:
            return EncodeFrame(kClose, frame.payload);
        default:
            return std::nullopt;
    }
}
