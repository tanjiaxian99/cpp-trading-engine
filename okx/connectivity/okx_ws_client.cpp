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
#include "perf/clock.hpp"
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
                         std::string path, std::optional<OkxAuth> auth, WsEndpointKind kind)
    : io_context_(io_context),
      host_(std::move(host)),
      port_(std::move(port)),
      path_(std::move(path)),
      auth_(std::move(auth)),
      kind_(kind),
      reconnect_timer_(io_context_),
      heartbeat_timer_(io_context_) {}

void OkxWsClient::SetOnConnected(ConnectHandler handler) {
    on_connected_ = std::move(handler);
}

void OkxWsClient::SetOnMessage(MessageHandler handler) {
    on_message_ = std::move(handler);
}

void OkxWsClient::SetOnTraceResolved(TraceHandler handler) {
    on_trace_resolved_ = std::move(handler);
}

void OkxWsClient::Start() {
    Connect();
}

void OkxWsClient::Send(std::string_view payload, std::optional<TickToTradeTrace> trace) {
    WriteRaw(EncodeFrame(WebSocketOpcode::kText, payload), trace);
}

void OkxWsClient::Connect() {
    try {
        transport_.emplace(io_context_, host_, port_);
        transport_->Connect();
        const std::vector<std::string> handshake_headers = kind_ == WsEndpointKind::kSbe && auth_
                                                               ? auth_->SbeWsLoginHeaders()
                                                               : std::vector<std::string>{};
        PerformWebSocketHandshake(*transport_, host_, path_, handshake_headers);

        reconnect_attempt_ = 0;
        rx_buffer_.clear();
        authenticated_ = false;

        Log.Info("OKX WebSocket connected to {}{}", host_, path_);
        if (kind_ == WsEndpointKind::kSbe) {
            // For SBE, authentication is done in the header of the upgrade message
            authenticated_ = true;
        } else if (auth_) {
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

                                  last_message_arrival_ticks_ = perf::ReadCounter();
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

void OkxWsClient::WriteRaw(const std::string& frame, std::optional<TickToTradeTrace> trace) {
    tx_ring_.Write(frame);
    if (trace) {
        pending_send_traces_.push_back(*trace);
    }

    if (!write_in_flight_ && !batching_) {
        StartWrite();
    }
}

void OkxWsClient::BeginBatch() {
    batching_ = true;
}

void OkxWsClient::EndBatch() {
    batching_ = false;
    if (!write_in_flight_) {
        StartWrite();
    }
}

void OkxWsClient::StartWrite() {
    if (!transport_) {
        return;
    }

    const std::uint64_t send_ticks = perf::ReadCounter();
    while (!pending_send_traces_.empty()) {
        TickToTradeTrace trace = pending_send_traces_.front();
        pending_send_traces_.pop_front();
        trace.send_ticks = send_ticks;
        if (on_trace_resolved_) {
            on_trace_resolved_(trace);
        }
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
    if (kind_ == WsEndpointKind::kSbe) {
        // For SBE, the server drives liveness checks,
        // which BuildControlResponse then responds with pong
        return;
    }
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
    Send(auth_->BuildPrivateWsLoginMessage());
    Log.Debug("Sent WS login request");
}

void OkxWsClient::DispatchMessage(std::string_view message) {
    last_message_decoded_ticks_ = perf::ReadCounter();
    if (auth_ && !authenticated_ && json::FindString(message, kEvent) == kLoginEvent) {
        authenticated_ = json::FindString(message, kCode) == kSuccessCode;
    }
    if (on_message_) {
        on_message_(message);
    }
}
