#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <source_location>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

enum class LogLevel : std::uint8_t {
    kDebug,
    kInfo,
    kWarn,
    kError,
};

template <typename... Args>
struct LogFmt {
    std::format_string<Args...> fmt;
    std::source_location loc;

    template <typename T>
    consteval LogFmt(const T& fmt, std::source_location loc = std::source_location::current())
        : fmt(fmt), loc(loc) {}
};

class AsyncLogger {
public:
    explicit AsyncLogger(LogLevel level = LogLevel::kInfo);
    ~AsyncLogger();

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    AsyncLogger(AsyncLogger&&) = delete;
    AsyncLogger& operator=(AsyncLogger&&) = delete;

    void Emit(LogLevel level, std::string_view message, std::source_location loc);

private:
    static constexpr std::size_t kRingCapacity = 4096;
    static constexpr std::size_t kMaxMessageLen = 256;
    static constexpr auto kPollInterval = std::chrono::milliseconds(1);

    struct Record {
        std::chrono::system_clock::time_point timestamp;
        std::string_view function_name;
        LogLevel level = LogLevel::kInfo;
        std::uint16_t len = 0;
        std::array<char, kMaxMessageLen> data{};
    };

    [[nodiscard]] static std::string_view LevelName(LogLevel level);

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

[[nodiscard]] AsyncLogger& LoggerInstance();

struct LogProxy {
    // The compiler can't deduce Args from fmt which is just a string, so we use
    // std::type_identity_t to tell the compiler to skip this parameter for type deduction
    template <typename... Args>
    void Debug(LogFmt<std::type_identity_t<Args>...> fmt, Args&&... args) const {
        LoggerInstance().Emit(LogLevel::kDebug, std::format(fmt.fmt, std::forward<Args>(args)...),
                              fmt.loc);
    }
    template <typename... Args>
    void Info(LogFmt<std::type_identity_t<Args>...> fmt, Args&&... args) const {
        LoggerInstance().Emit(LogLevel::kInfo, std::format(fmt.fmt, std::forward<Args>(args)...),
                              fmt.loc);
    }
    template <typename... Args>
    void Warn(LogFmt<std::type_identity_t<Args>...> fmt, Args&&... args) const {
        LoggerInstance().Emit(LogLevel::kWarn, std::format(fmt.fmt, std::forward<Args>(args)...),
                              fmt.loc);
    }
    template <typename... Args>
    void Error(LogFmt<std::type_identity_t<Args>...> fmt, Args&&... args) const {
        LoggerInstance().Emit(LogLevel::kError, std::format(fmt.fmt, std::forward<Args>(args)...),
                              fmt.loc);
    }
};

inline constexpr LogProxy Log;
