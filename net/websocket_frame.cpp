#include "net/websocket_frame.hpp"

#include <openssl/rand.h>

#include <array>
#include <cstddef>
#include <stdexcept>

namespace {
constexpr std::size_t kBaseHeaderSize = 2;
constexpr std::uint8_t kFinBit = 0x80;
constexpr std::uint8_t kRsvMask = 0x70;
constexpr std::uint8_t kOpcodeMask = 0x0F;
constexpr std::uint8_t kMaskBit = 0x80;
constexpr std::uint8_t kPayloadLengthMask = 0x7F;
constexpr std::uint8_t kExtended16Marker = 126;
constexpr std::uint8_t kExtended64Marker = 127;
constexpr std::size_t kExtended16Bytes = 2;
constexpr std::size_t kExtended64Bytes = 8;
constexpr std::size_t kMaskKeyBytes = 4;
constexpr std::uint64_t kMaxBaseLength = 125;
constexpr std::uint64_t kMaxExtended16Length = 0xFFFF;

constexpr std::array kValidOpcodes = {
    true, true, true, false, false, false, false, false,
    true, true, true, false, false, false, false, false,
};

std::uint16_t ReadBigEndian16(std::string_view data, std::size_t offset) {
    const auto high = static_cast<std::uint8_t>(data[offset]);
    const auto low = static_cast<std::uint8_t>(data[offset + 1]);
    return static_cast<std::uint16_t>((high << 8) | low);
}

std::uint64_t ReadBigEndian64(std::string_view data, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t chunk = 0; chunk < kExtended64Bytes; chunk += kExtended16Bytes) {
        value = (value << 16) | ReadBigEndian16(data, offset + chunk);
    }
    return value;
}

void AppendBigEndian16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value >> 8));
    out.push_back(static_cast<char>(value));
}

void AppendBigEndian64(std::string& out, std::uint64_t value) {
    for (int shift = 48; shift >= 0; shift -= 16) {
        AppendBigEndian16(out, static_cast<std::uint16_t>(value >> shift));
    }
}

std::array<std::uint8_t, kMaskKeyBytes> GenerateMaskingKey() {
    std::array<std::uint8_t, kMaskKeyBytes> key{};
    if (RAND_bytes(key.data(), key.size()) != 1) {
        throw std::runtime_error("Failed to generate WebSocket frame masking key");
    }
    return key;
}
}  // namespace

std::optional<WebSocketFrameHeader> DecodeFrameHeader(std::string_view data) {
    if (data.size() < kBaseHeaderSize) {
        return std::nullopt;
    }

    const auto byte0 = static_cast<std::uint8_t>(data[0]);
    const auto byte1 = static_cast<std::uint8_t>(data[1]);

    if ((byte0 & kRsvMask) != 0) {
        throw std::runtime_error("WebSocket frame sets reserved bits with no extension negotiated");
    }
    if ((byte1 & kMaskBit) != 0) {
        throw std::runtime_error("WebSocket frame from server must not be masked");
    }

    const std::uint8_t opcode_value = byte0 & kOpcodeMask;
    if (!kValidOpcodes.at(opcode_value)) {
        throw std::runtime_error("WebSocket frame uses a reserved opcode");
    }

    const std::uint8_t length_marker = byte1 & kPayloadLengthMask;
    std::uint64_t payload_length = length_marker;
    std::size_t header_size = kBaseHeaderSize;

    if (length_marker == kExtended16Marker) {
        header_size += kExtended16Bytes;
        if (data.size() < header_size) {
            return std::nullopt;
        }
        payload_length = ReadBigEndian16(data, kBaseHeaderSize);
    } else if (length_marker == kExtended64Marker) {
        header_size += kExtended64Bytes;
        if (data.size() < header_size) {
            return std::nullopt;
        }
        payload_length = ReadBigEndian64(data, kBaseHeaderSize);
    }

    return WebSocketFrameHeader{
        .fin = (byte0 & kFinBit) != 0,
        .opcode = static_cast<WebSocketOpcode>(opcode_value),
        .payload_length = payload_length,
        .header_size = header_size,
    };
}

std::optional<WebSocketFrame> DecodeFrame(std::string_view data) {
    const auto header = DecodeFrameHeader(data);
    if (!header) {
        return std::nullopt;
    }

    const auto payload_length = static_cast<std::size_t>(header->payload_length);
    const std::size_t total_size = header->header_size + payload_length;
    if (data.size() < total_size) {
        return std::nullopt;
    }

    return WebSocketFrame{
        .header = *header,
        .payload = data.substr(header->header_size, payload_length),
        .total_size = total_size,
    };
}

std::string EncodeFrame(WebSocketOpcode opcode, std::string_view payload) {
    std::string frame;

    frame.push_back(static_cast<char>(kFinBit | static_cast<std::uint8_t>(opcode)));

    const std::uint64_t payload_length = payload.size();
    if (payload_length <= kMaxBaseLength) {
        frame.push_back(static_cast<char>(kMaskBit | static_cast<std::uint8_t>(payload_length)));
    } else if (payload_length <= kMaxExtended16Length) {
        frame.push_back(static_cast<char>(kMaskBit | kExtended16Marker));
        AppendBigEndian16(frame, static_cast<std::uint16_t>(payload_length));
    } else {
        frame.push_back(static_cast<char>(kMaskBit | kExtended64Marker));
        AppendBigEndian64(frame, payload_length);
    }

    const auto mask_key = GenerateMaskingKey();
    for (const std::uint8_t key_byte : mask_key) {
        frame.push_back(static_cast<char>(key_byte));
    }

    for (std::size_t i = 0; i < payload.size(); i++) {
        const auto original = static_cast<std::uint8_t>(payload[i]);
        frame.push_back(static_cast<char>(original ^ mask_key[i % kMaskKeyBytes]));
    }

    return frame;
}
