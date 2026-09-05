#include "log/async_logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <stdexcept>

AsyncLogger::AsyncLogger(LogLevel level)
    : min_level(level), writer_thread_(&AsyncLogger::WriterLoop, this) {}

AsyncLogger::~AsyncLogger() {
    running_.store(false, std::memory_order_release);
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
}

void AsyncLogger::Emit(LogLevel level, std::string_view message, std::source_location loc) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next_tail = (tail + 1) % kRingCapacity;

    if (next_tail == head_.load(std::memory_order_acquire)) {
        // Leave one slot empty to tell whether the buffer is full
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    Record& record = ring_[tail];
    record.timestamp = std::chrono::system_clock::now();
    record.function_name = loc.function_name();
    record.level = level;
    const std::size_t n = std::min(message.size(), kMaxMessageLen);
    std::memcpy(record.data.data(), message.data(), n);
    record.len = static_cast<std::uint16_t>(n);

    tail_.store(next_tail, std::memory_order_release);
}

std::string_view AsyncLogger::LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
    }
    throw std::runtime_error("Unrecognized LogLevel");
}

void AsyncLogger::WriterLoop() {
    for (;;) {
        if (const auto dropped = dropped_count_.exchange(0, std::memory_order_relaxed);
            dropped > 0) {
            std::cout << "AsyncLogger: dropped " << dropped << " log message(s) (ring full)\n";
        }

        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);

        if (head == tail) {
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(kPollInterval);
            continue;
        }

        const Record& record = ring_[head];
        const auto timestamp_ms =
            std::chrono::time_point_cast<std::chrono::milliseconds>(record.timestamp);
        std::cout << std::format("{:%Y-%m-%d %H:%M:%S}", timestamp_ms) << ' '
                  << LevelName(record.level) << ' ' << record.function_name << ' ';
        std::cout.write(record.data.data(), record.len);
        std::cout << '\n';

        head_.store((head + 1) % kRingCapacity, std::memory_order_release);
    }
}

namespace {
constexpr const char* kLogLevelEnvVar = "ASYNC_LOG_LEVEL";

LogLevel ReadLogLevelFromEnv() {
    const char* value = std::getenv(kLogLevelEnvVar);
    if (value == nullptr) {
        return LogLevel::kInfo;
    }

    return static_cast<LogLevel>(std::strtol(value, nullptr, 10));
}
}  // namespace

AsyncLogger& LoggerInstance() {
    static AsyncLogger instance(ReadLogLevelFromEnv());
    return instance;
}
