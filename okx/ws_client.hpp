#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "net/ring_buffer.hpp"
#include "net/transport.hpp"
#include "net/websocket_frame.hpp"
#include "net/websocket_reassembler.hpp"

namespace asio = boost::asio;

class OkxWsClient {
public:
    using MessageHandler = std::function<void(std::string_view message)>;
    using ConnectHandler = std::function<void()>;

    OkxWsClient(asio::io_context& io_context, std::string host, std::string port, std::string path);

    void SetOnConnected(ConnectHandler handler);
    void SetOnMessage(MessageHandler handler);
    void Start();
    void Send(std::string_view payload);
    [[nodiscard]] bool IsConnected() const {
        return transport_.has_value();
    }

private:
    static constexpr std::size_t kReadChunkSize = 4096;
    static constexpr std::size_t kMaxMessageSize = 1 << 20;
    static constexpr std::size_t kTxRingCapacity = 1 << 14;

    void Connect();
    void ScheduleReconnect();
    void ReadLoop();
    void HandleFrame(const WebSocketFrame& frame);
    void WriteRaw(const std::string& frame);
    void StartWrite();
    void ScheduleHeartbeat();

    asio::io_context& io_context_;
    std::string host_;
    std::string port_;
    std::string path_;

    std::optional<Transport> transport_;
    asio::steady_timer reconnect_timer_;
    asio::steady_timer heartbeat_timer_;
    int reconnect_attempt_ = 0;

    std::array<char, kReadChunkSize> read_chunk_{};
    std::string rx_buffer_;
    RingBuffer<kTxRingCapacity> tx_ring_;
    bool write_in_flight_ = false;
    WebSocketReassembler<kMaxMessageSize> reassembler_;

    ConnectHandler on_connected_;
    MessageHandler on_message_;
};
