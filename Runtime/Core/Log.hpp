#pragma once

#include <Core/Result.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace Ursa
{
enum class LogLevel : uint8_t
{
	trace,
	debug,
	info,
	warn,
	error,
	critical,
	off
};

struct LogConfig
{
	std::filesystem::path filePath;
	LogLevel level;
	size_t maxFileBytes = 5 * 1024 * 1024;
	size_t maxFiles = 3;

	LogConfig();
};

class Log final
{
public:
	static LogLevel DefaultLevel() noexcept;

	static Result<void> Initialize(const std::filesystem::path& executable, const LogConfig& config = {});

	static Result<void> InitializeForCurrentProcess(const LogConfig& config = {});

	static void Shutdown() noexcept;

	static bool Initialized() noexcept;

	static LogLevel Level() noexcept;

	static void SetLevel(LogLevel level) noexcept;

	static bool Enabled(LogLevel level) noexcept;

	static void Write(LogLevel level, const char* module, const String& message) noexcept;
};

class LogSession final
{
public:
	explicit LogSession(const LogConfig& config = {});

	~LogSession();

	LogSession(const LogSession&) = delete;
	LogSession& operator=(const LogSession&) = delete;

	const Result<void>& GetStatus() const
	{
		return status;
	}

private:
	Result<void> status;
};
} // namespace Ursa

#define URSA_LOG_AT(level, module, message)                                                                            \
	do                                                                                                                 \
	{                                                                                                                  \
		if (::Ursa::Log::Enabled(level))                                                                               \
			::Ursa::Log::Write(level, module, message);                                                                \
	} while (false)

#define URSA_LOG_TRACE(module, message) URSA_LOG_AT(::Ursa::LogLevel::trace, module, message)
#define URSA_LOG_DEBUG(module, message) URSA_LOG_AT(::Ursa::LogLevel::debug, module, message)
#define URSA_LOG_INFO(module, message) URSA_LOG_AT(::Ursa::LogLevel::info, module, message)
#define URSA_LOG_WARN(module, message) URSA_LOG_AT(::Ursa::LogLevel::warn, module, message)
#define URSA_LOG_ERROR(module, message) URSA_LOG_AT(::Ursa::LogLevel::error, module, message)
#define URSA_LOG_CRITICAL(module, message) URSA_LOG_AT(::Ursa::LogLevel::critical, module, message)
