#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class WebSocketOpcode : std::uint8_t {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA,
};

struct WebSocketFrameHeader {
    bool fin = false;
    WebSocketOpcode opcode = WebSocketOpcode::kContinuation;
    std::uint64_t payload_length = 0;
    std::size_t header_size = 0;
};

struct WebSocketFrame {
    WebSocketFrameHeader header;
    std::string_view payload;
    std::size_t total_size = 0;
};

std::optional<WebSocketFrameHeader> DecodeFrameHeader(std::string_view data);

// The frame is a string_view into the data
std::optional<WebSocketFrame> DecodeFrame(std::string_view data);

std::string EncodeFrame(WebSocketOpcode opcode, std::string_view payload);
