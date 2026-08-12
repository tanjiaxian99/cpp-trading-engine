#include "net/websocket_handshake.hpp"

#include <openssl/rand.h>
#include <boost/asio/buffer.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/base64.hpp"
#include "util/sha1.hpp"

namespace {
// RFC 6455 4.2.2 — fixed GUID the server appends to the client's key before
// hashing, so the accept value can't just be an echo of the key.
constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr std::string_view kRequestFormat =
    "GET {} HTTP/1.1\r\n"
    "Host: {}\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: {}\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";

constexpr std::string_view kExpectedStatusLine = "HTTP/1.1 101";
constexpr std::string_view kAcceptHeaderName = "sec-websocket-accept";
constexpr std::string_view kCrLf = "\r\n";
constexpr char kColon = ':';
constexpr char kWhitespace = ' ';

// The last header ends with \r\n and the entire header block ends with \r\n
constexpr std::string_view kHeaderTerminator = "\r\n\r\n";

std::string GenerateWebSocketKey() {
    std::array<unsigned char, 16> nonce{};
    if (RAND_bytes(nonce.data(), nonce.size()) != 1) {
        throw std::runtime_error("failed to generate random Sec-WebSocket-Key");
    }
    return Base64Encode(std::vector(nonce.begin(), nonce.end()));
}

std::string ComputeExpectedAccept(std::string_view key) {
    const std::string message = std::string(key) + std::string(kWebSocketGuid);
    return Base64Encode(Sha1(message));
}

std::string ToLower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

std::string ReadHandshakeResponse(Transport& transport) {
    std::string response;
    std::array<char, 512> chunk{};
    while (response.find(kHeaderTerminator) == std::string::npos) {
        const std::size_t n = transport.ReadSome(asio::buffer(chunk));
        response.append(chunk.data(), n);
    }
    return response;
}

std::optional<std::string_view> FindHeader(std::string_view response, std::string_view name) {
    std::size_t pos = 0;
    while (pos < response.size()) {
        const std::size_t line_end = response.find(kCrLf, pos);
        const std::string_view line = response.substr(pos, line_end - pos);
        if (line.empty()) {
            break;  // The blank line ending the headers
        }

        const std::size_t colon = line.find(kColon);
        if (colon != std::string_view::npos && ToLower(line.substr(0, colon)) == name) {
            std::size_t value_start = colon + 1;
            while (value_start < line.size() && line[value_start] == kWhitespace) {
                value_start++;
            }
            return line.substr(value_start);
        }

        pos = line_end + kCrLf.size();
    }
    return std::nullopt;
}
}  // namespace

void PerformWebSocketHandshake(Transport& transport, std::string_view host, std::string_view path) {
    const std::string key = GenerateWebSocketKey();
    transport.Write(std::format(kRequestFormat, path, host, key));

    const std::string response = ReadHandshakeResponse(transport);
    if (!response.starts_with(kExpectedStatusLine)) {
        throw std::runtime_error("WebSocket handshake failed: server did not switch protocols");
    }

    const auto accept_header = FindHeader(response, kAcceptHeaderName);
    if (!accept_header) {
        throw std::runtime_error("WebSocket handshake failed: missing Sec-WebSocket-Accept header");
    }
    if (*accept_header != ComputeExpectedAccept(key)) {
        throw std::runtime_error("WebSocket handshake failed: Sec-WebSocket-Accept did not verify");
    }
}
