#pragma once

#include <Core/Result.hpp>
#include <Core/Types.hpp>
#include <cstddef>
#include <filesystem>

namespace Ursa
{
enum class LogLevel : UInt8
{
	Trace,
	Debug,
	Info,
	Warn,
	Error,
	Critical,
	Off
};

struct LogConfig
{
	std::filesystem::path filePath;
	LogLevel level;
	Ursa::Size maxFileBytes = 5 * 1024 * 1024;
	Ursa::Size maxFiles = 3;

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

#define URSA_LOG_TRACE(module, message) URSA_LOG_AT(::Ursa::LogLevel::Trace, module, message)
#define URSA_LOG_DEBUG(module, message) URSA_LOG_AT(::Ursa::LogLevel::Debug, module, message)
#define URSA_LOG_INFO(module, message) URSA_LOG_AT(::Ursa::LogLevel::Info, module, message)
#define URSA_LOG_WARN(module, message) URSA_LOG_AT(::Ursa::LogLevel::Warn, module, message)
#define URSA_LOG_ERROR(module, message) URSA_LOG_AT(::Ursa::LogLevel::Error, module, message)
#define URSA_LOG_CRITICAL(module, message) URSA_LOG_AT(::Ursa::LogLevel::Critical, module, message)
