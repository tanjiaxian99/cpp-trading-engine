#include "okx/connectivity/okx_ws_client.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

#include "log/async_logger.hpp"
#include "net/websocket_control.hpp"
#include "net/websocket_handshake.hpp"
#include "okx/common/okx_constants.hpp"
#include "okx/connectivity/heartbeat.hpp"
#include "util/json.hpp"

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
                         std::string path, std::optional<OkxAuth> auth)
    : io_context_(io_context),
      host_(std::move(host)),
      port_(std::move(port)),
      path_(std::move(path)),
      auth_(std::move(auth)),
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
        authenticated_ = false;

        Log.Info("OKX WebSocket connected to {}{}", host_, path_);
        if (auth_) {
            SendLogin();
        }
        if (on_connected_) {
            on_connected_();
        }
        ScheduleHeartbeat();
        ReadLoop();
    } catch (const std::exception& e) {
        Log.Warn("OKX WebSocket connect failed: {}", e.what());
        ScheduleReconnect();
    }
}

void OkxWsClient::ScheduleReconnect() {
    transport_.reset();
    authenticated_ = false;
    heartbeat_timer_.cancel();
    tx_ring_.Reset();
    write_in_flight_ = false;

    const auto delay = ComputeBackoff(reconnect_attempt_);
    reconnect_attempt_++;
    Log.Warn("Reconnecting in {} ms (attempt {})", delay.count(), reconnect_attempt_);

    reconnect_timer_.expires_after(delay);
    reconnect_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            return;  // Timer was cancelled
        }
        Connect();
    });
}

void OkxWsClient::ReadLoop() {
    if (!transport_) {
        return;  // ScheduleReconnect reset the optional but will set it back soon
    }

    transport_->AsyncReadSome(asio::buffer(read_chunk_),
                              [this](const boost::system::error_code& ec, std::size_t n) {
                                  if (ec) {
                                      Log.Warn("OKX WebSocket read error: {}", ec.message());
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
    } else {
        DispatchMessage(frame.payload);
        return;
    }

    if (reassembler_.IsComplete()) {
        DispatchMessage(reassembler_.GetMessage());
        reassembler_.Reset();
    }
}

void OkxWsClient::WriteRaw(const std::string& frame) {
    tx_ring_.Write(frame);
    if (!write_in_flight_) {
        StartWrite();
    }
}

void OkxWsClient::StartWrite() {
    if (!transport_) {
        return;
    }

    write_in_flight_ = true;
    auto on_write = [this](const boost::system::error_code& ec, std::size_t bytes_written) {
        if (ec) {
            Log.Warn("OKX WebSocket write error: {}", ec.message());
            ScheduleReconnect();
            return;
        }
        tx_ring_.CommitRead(bytes_written);
        if (tx_ring_.Size() > 0) {
            StartWrite();  // More queued past this write, or past the wrap point
        } else {
            write_in_flight_ = false;
        }
    };
    transport_->AsyncWrite(tx_ring_.ContiguousReadableRegion(), std::move(on_write));
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

void OkxWsClient::SendLogin() {
    if (!auth_) {
        throw std::runtime_error("OkxWsClient::SendLogin called without auth");
    }
    Send(auth_->BuildWsLoginMessage());
    Log.Debug("Sent WS login request");
}

void OkxWsClient::DispatchMessage(std::string_view message) {
    if (auth_ && !authenticated_ && json::FindString(message, kEvent) == kLoginEvent) {
        authenticated_ = json::FindString(message, kCode) == kSuccessCode;
    }
    if (on_message_) {
        on_message_(message);
    }
}
