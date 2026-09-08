#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "net/ring_buffer.hpp"
#include "net/transport.hpp"
#include "net/websocket_frame.hpp"
#include "net/websocket_reassembler.hpp"
#include "okx/connectivity/auth.hpp"
#include "perf/tick_to_trade_trace.hpp"

namespace asio = boost::asio;

class OkxWsClient {
public:
    using MessageHandler = std::function<void(std::string_view message)>;
    using ConnectHandler = std::function<void()>;
    using TraceHandler = std::function<void(const TickToTradeTrace&)>;

    OkxWsClient(asio::io_context& io_context, std::string host, std::string port, std::string path,
                std::optional<OkxAuth> auth = std::nullopt);

    void SetOnConnected(ConnectHandler handler);
    void SetOnMessage(MessageHandler handler);
    void SetOnTraceResolved(TraceHandler handler);
    void Start();
    void Send(std::string_view payload, std::optional<TickToTradeTrace> trace = std::nullopt);
    void BeginBatch();
    void EndBatch();
    [[nodiscard]] bool IsConnected() const {
        return transport_.has_value();
    }
    [[nodiscard]] bool IsAuthenticated() const {
        return authenticated_;
    }
    [[nodiscard]] std::uint64_t LastMessageArrivalTicks() const {
        return last_message_arrival_ticks_;
    }
    [[nodiscard]] std::uint64_t LastMessageDecodedTicks() const {
        return last_message_decoded_ticks_;
    }

private:
    static constexpr std::size_t kReadChunkSize = 4096;
    static constexpr std::size_t kMaxMessageSize = 1 << 20;
    static constexpr std::size_t kTxRingCapacity = 1 << 14;

    void Connect();
    void ScheduleReconnect();
    void ReadLoop();
    void HandleFrame(const WebSocketFrame& frame);
    void DispatchMessage(std::string_view message);
    void WriteRaw(const std::string& frame, std::optional<TickToTradeTrace> trace = std::nullopt);
    void StartWrite();
    void ScheduleHeartbeat();
    void SendLogin();

    asio::io_context& io_context_;
    std::string host_;
    std::string port_;
    std::string path_;
    std::optional<OkxAuth> auth_;
    bool authenticated_ = false;

    std::optional<Transport> transport_;
    asio::steady_timer reconnect_timer_;
    asio::steady_timer heartbeat_timer_;
    int reconnect_attempt_ = 0;

    std::array<char, kReadChunkSize> read_chunk_{};
    std::string rx_buffer_;
    std::uint64_t last_message_arrival_ticks_ = 0;
    std::uint64_t last_message_decoded_ticks_ = 0;
    RingBuffer<kTxRingCapacity> tx_ring_;
    bool write_in_flight_ = false;
    bool batching_ = false;
    WebSocketReassembler<kMaxMessageSize> reassembler_;
    std::deque<TickToTradeTrace> pending_send_traces_;

    ConnectHandler on_connected_;
    MessageHandler on_message_;
    TraceHandler on_trace_resolved_;
};
