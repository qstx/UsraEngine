#pragma once

#include <core/Result.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ursa {
enum class LogLevel : uint8_t { Trace, Debug, Info, Warn, Error, Critical, Off };

struct LogConfig {
    std::filesystem::path filePath;
    LogLevel level;
    size_t maxFileBytes = 5 * 1024 * 1024;
    size_t maxFiles = 3;

    LogConfig();
};

class Log final {
public:
    static LogLevel defaultLevel() noexcept;

    static Result<void> initialize(const std::filesystem::path &executable, const LogConfig &config = {});

    static Result<void> initializeForCurrentProcess(const LogConfig &config = {});

    static void shutdown() noexcept;

    static bool initialized() noexcept;

    static LogLevel level() noexcept;

    static void setLevel(LogLevel level) noexcept;

    static bool enabled(LogLevel level) noexcept;

    static void write(LogLevel level, const char *module, const String &message) noexcept;
};

class LogSession final {
public:
    explicit LogSession(const LogConfig &config = {});

    ~LogSession();

    LogSession(const LogSession &) = delete;
    LogSession &operator=(const LogSession &) = delete;

    const Result<void> &status() const {
        return status_;
    }

private:
    Result<void> status_;
};
} // namespace ursa

#define URSA_LOG_AT(level, module, message)                                                        \
    do {                                                                                           \
        if (::ursa::Log::enabled(level))                                                           \
            ::ursa::Log::write(level, module, message);                                            \
    } while (false)

#define URSA_LOG_TRACE(module, message) URSA_LOG_AT(::ursa::LogLevel::Trace, module, message)
#define URSA_LOG_DEBUG(module, message) URSA_LOG_AT(::ursa::LogLevel::Debug, module, message)
#define URSA_LOG_INFO(module, message) URSA_LOG_AT(::ursa::LogLevel::Info, module, message)
#define URSA_LOG_WARN(module, message) URSA_LOG_AT(::ursa::LogLevel::Warn, module, message)
#define URSA_LOG_ERROR(module, message) URSA_LOG_AT(::ursa::LogLevel::Error, module, message)
#define URSA_LOG_CRITICAL(module, message) URSA_LOG_AT(::ursa::LogLevel::Critical, module, message)
