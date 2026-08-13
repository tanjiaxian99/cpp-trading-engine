#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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

private:
    static constexpr std::size_t kReadChunkSize = 4096;
    static constexpr std::size_t kMaxMessageSize = 1 << 20;

    void Connect();
    void ScheduleReconnect();
    void ReadLoop();
    void HandleFrame(const WebSocketFrame& frame);
    void WriteRaw(std::string frame);
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
    // std::vector can cause a reallocation and invalidate write_queue_.front(),
    // so we use std::deque
    std::deque<std::string> write_queue_;
    WebSocketReassembler<kMaxMessageSize> reassembler_;

    ConnectHandler on_connected_;
    MessageHandler on_message_;
};
