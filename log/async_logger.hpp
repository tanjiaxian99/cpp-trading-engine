#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <thread>

enum class LogLevel : std::uint8_t {
    kDebug,
    kInfo,
    kWarn,
    kError,
};

class AsyncLogger {
public:
    explicit AsyncLogger(LogLevel level = LogLevel::kInfo);
    ~AsyncLogger();

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    AsyncLogger(AsyncLogger&&) = delete;
    AsyncLogger& operator=(AsyncLogger&&) = delete;

    void Debug(std::string_view message) {
        Log(LogLevel::kDebug, message);
    }
    void Info(std::string_view message) {
        Log(LogLevel::kInfo, message);
    }
    void Warn(std::string_view message) {
        Log(LogLevel::kWarn, message);
    }
    void Error(std::string_view message) {
        Log(LogLevel::kError, message);
    }

private:
    static constexpr std::size_t kRingCapacity = 4096;
    static constexpr std::size_t kMaxMessageLen = 256;
    static constexpr auto kPollInterval = std::chrono::milliseconds(1);

    struct Record {
        std::chrono::system_clock::time_point timestamp;
        LogLevel level = LogLevel::kInfo;
        std::uint16_t len = 0;
        std::array<char, kMaxMessageLen> data{};
    };

    [[nodiscard]] static std::string_view LevelName(LogLevel level);

    void Log(LogLevel level, std::string_view message);

    void WriterLoop();

    const LogLevel min_level;
    std::array<Record, kRingCapacity> ring_{};
    // Producer writes to the tail_ while consumer writes to the head_. To avoid false sharing,
    // start each atomic at its own 64-byte-aligned address
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::atomic<std::uint64_t> dropped_count_{0};
    std::atomic<bool> running_{true};
    std::thread writer_thread_;
};
