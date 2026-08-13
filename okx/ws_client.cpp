#include "okx/ws_client.hpp"

#include <algorithm>
#include <iostream>
#include <random>
#include <utility>

#include "net/websocket_control.hpp"
#include "net/websocket_handshake.hpp"
#include "okx/heartbeat.hpp"

namespace {
constexpr auto kBaseBackoff = std::chrono::milliseconds(500);
constexpr auto kMaxBackoff = std::chrono::milliseconds(30'000);
constexpr auto kHeartbeatInterval = std::chrono::seconds(15);

// The backoff is a random value with a cap so a burst of clients reconnecting after
// the same outage doesn't retry in lockstep
std::chrono::milliseconds ComputeBackoff(int attempt) {
    const auto exponential = kBaseBackoff * (1 << std::min(attempt, 10));
    const auto capped =
        std::min(kMaxBackoff, std::chrono::duration_cast<std::chrono::milliseconds>(exponential));

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<long long> jitter(0, capped.count());
    return std::chrono::milliseconds(jitter(rng));
}
}  // namespace

OkxWsClient::OkxWsClient(asio::io_context& io_context, std::string host, std::string port,
                         std::string path)
    : io_context_(io_context),
      host_(std::move(host)),
      port_(std::move(port)),
      path_(std::move(path)),
      reconnect_timer_(io_context_),
      heartbeat_timer_(io_context_) {}

void OkxWsClient::SetOnConnected(ConnectHandler handler) {
    on_connected_ = std::move(handler);
}

void OkxWsClient::SetOnMessage(MessageHandler handler) {
    on_message_ = std::move(handler);
}

void OkxWsClient::Start() {
    Connect();
}

void OkxWsClient::Send(std::string_view payload) {
    WriteRaw(EncodeFrame(WebSocketOpcode::kText, payload));
}

void OkxWsClient::Connect() {
    try {
        transport_.emplace(io_context_, host_, port_);
        transport_->Connect();
        PerformWebSocketHandshake(*transport_, host_, path_);

        reconnect_attempt_ = 0;
        rx_buffer_.clear();

        std::cout << "OKX WebSocket connected to " << host_ << path_ << "\n";
        if (on_connected_) {
            on_connected_();
        }
        ScheduleHeartbeat();
        ReadLoop();
    } catch (const std::exception& e) {
        std::cerr << "OKX WebSocket connect failed: " << e.what() << "\n";
        ScheduleReconnect();
    }
}

void OkxWsClient::ScheduleReconnect() {
    transport_.reset();
    heartbeat_timer_.cancel();
    write_queue_.clear();

    const auto delay = ComputeBackoff(reconnect_attempt_);
    reconnect_attempt_++;
    std::cerr << "Reconnecting in " << delay.count() << " ms (attempt " << reconnect_attempt_
              << ")\n";

    reconnect_timer_.expires_after(delay);
    reconnect_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            return;  // Timer was cancelled
        }
        Connect();
    });
}

void OkxWsClient::ReadLoop() {
    transport_->AsyncReadSome(
        asio::buffer(read_chunk_), [this](const boost::system::error_code& ec, std::size_t n) {
            if (ec) {
                std::cerr << "OKX WebSocket read error: " << ec.message() << "\n";
                ScheduleReconnect();
                return;
            }

            rx_buffer_.append(read_chunk_.data(), n);
            while (const auto frame = DecodeFrame(rx_buffer_)) {
                HandleFrame(*frame);
                rx_buffer_.erase(0, frame->total_size);
            }
            ReadLoop();
        });
}

void OkxWsClient::HandleFrame(const WebSocketFrame& frame) {
    if (const auto response = BuildControlResponse(frame)) {
        WriteRaw(*response);
        return;
    }

    if (frame.header.opcode == WebSocketOpcode::kPong || IsOkxPong(frame)) {
        return;
    }

    if (frame.header.opcode == WebSocketOpcode::kContinuation) {
        reassembler_.Append(frame.payload, frame.header.fin);
    } else if (!frame.header.fin) {
        reassembler_.Append(frame.payload, false);
        return;
    } else if (on_message_) {
        on_message_(frame.payload);
        return;
    }

    if (reassembler_.IsComplete()) {
        if (on_message_) {
            on_message_(reassembler_.GetMessage());
        }
        reassembler_.Reset();
    }
}

void OkxWsClient::WriteRaw(std::string frame) {
    write_queue_.push_back(std::move(frame));
    if (write_queue_.size() == 1) {
        StartWrite();  // Nothing in flight, so we can start sending
    }
}

void OkxWsClient::StartWrite() {
    auto on_write = [this](const boost::system::error_code& ec, std::size_t) {
        if (ec) {
            std::cerr << "OKX WebSocket write error: " << ec.message() << "\n";
            ScheduleReconnect();
            return;
        }
        write_queue_.pop_front();
        if (!write_queue_.empty()) {
            StartWrite();
        }
    };
    transport_->AsyncWrite(asio::buffer(write_queue_.front()), std::move(on_write));
}

void OkxWsClient::ScheduleHeartbeat() {
    heartbeat_timer_.expires_after(kHeartbeatInterval);
    heartbeat_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            return;  // Timer was cancelled, either from reconnection or shutdown
        }
        WriteRaw(EncodeOkxPing());
        ScheduleHeartbeat();
    });
}
