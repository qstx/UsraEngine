#include <Core/Log.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Ursa
{
namespace
{
std::mutex logMutex;
std::shared_ptr<spdlog::logger> logger;
std::atomic<LogLevel> minimumLevel{Log::DefaultLevel()};

bool valid(LogLevel level)
{
	return level >= LogLevel::trace && level <= LogLevel::off;
}

spdlog::level::level_enum NativeLevel(LogLevel level)
{
	switch (level)
	{
		case LogLevel::trace:
			return spdlog::level::trace;
		case LogLevel::debug:
			return spdlog::level::debug;
		case LogLevel::info:
			return spdlog::level::info;
		case LogLevel::warn:
			return spdlog::level::warn;
		case LogLevel::error:
			return spdlog::level::err;
		case LogLevel::critical:
			return spdlog::level::critical;
		case LogLevel::off:
			return spdlog::level::off;
	}
	return spdlog::level::off;
}

const char* LevelName(LogLevel level)
{
	switch (level)
	{
		case LogLevel::trace:
			return "trace";
		case LogLevel::debug:
			return "debug";
		case LogLevel::info:
			return "info";
		case LogLevel::warn:
			return "warn";
		case LogLevel::error:
			return "error";
		case LogLevel::critical:
			return "critical";
		case LogLevel::off:
			return "off";
	}
	return "unknown";
}

void fallback(LogLevel level, const char* module, const String& message) noexcept
{
	std::fprintf(stderr, "[%s] [%s] %s\n", LevelName(level), module ? module : "Unknown", message.c_str());
	std::fflush(stderr);
}
} // namespace

LogConfig::LogConfig() : level(Log::DefaultLevel())
{
}

LogLevel Log::DefaultLevel() noexcept
{
#ifndef NDEBUG
	return LogLevel::debug;
#else
	return LogLevel::Info;
#endif
}

Result<void> Log::Initialize(const std::filesystem::path& executable, const LogConfig& config)
{
	if (executable.empty() || !valid(config.level) || config.maxFileBytes == 0 || config.maxFiles == 0)
	{
		return Result<void>::Failure("Invalid logging executable path, level or rotation settings");
	}
	std::lock_guard<std::mutex> lock(logMutex);
	if (logger)
	{
		return Result<void>::Failure("Logging is already initialized");
	}
	std::filesystem::path path;
	try
	{
		const auto absoluteExecutable = std::filesystem::absolute(executable);
		path = config.filePath.empty()
		           ? absoluteExecutable.parent_path() / "logs" / (absoluteExecutable.stem().string() + ".log")
		           : std::filesystem::absolute(config.filePath);
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error)
		{
			const String diagnostic = "Cannot create log directory " + String(path.u8string().c_str()) + ": " +
			                          String(error.message().c_str());
			fallback(LogLevel::error, "Log", diagnostic);
			return Result<void>::Failure(diagnostic);
		}

		auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
		auto file =
		    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path.string(), config.maxFileBytes, config.maxFiles);
		std::vector<spdlog::sink_ptr> sinks{console, file};
		auto created = std::make_shared<spdlog::logger>("Ursa", sinks.begin(), sinks.end());
		created->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
		created->set_level(NativeLevel(config.level));
		created->flush_on(spdlog::level::warn);
		created->set_error_handler(
		    [](const std::string& errorText)
		    {
			    std::fprintf(stderr, "[Ursa log] %s\n", errorText.c_str());
			    std::fflush(stderr);
		    });
		logger = std::move(created);
		minimumLevel.store(config.level, std::memory_order_release);
		return {};
	}
	catch (const std::exception& error)
	{
		const String diagnostic =
		    "Cannot initialize log file " + String(path.u8string().c_str()) + ": " + String(error.what());
		fallback(LogLevel::error, "Log", diagnostic);
		return Result<void>::Failure(diagnostic);
	}
}

Result<void> Log::InitializeForCurrentProcess(const LogConfig& config)
{
#ifdef _WIN32
	std::wstring buffer(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (length == 0 || length >= buffer.size())
	{
		const String diagnostic = "Cannot locate executable for default log path";
		fallback(LogLevel::error, "Log", diagnostic);
		return Result<void>::Failure(diagnostic);
	}
	buffer.resize(length);
	return Initialize(std::filesystem::path(buffer), config);
#else
	return Result<void>::Failure("Current executable path is not supported on this platform");
#endif
}

void Log::Shutdown() noexcept
{
	std::lock_guard<std::mutex> lock(logMutex);
	if (logger)
	{
		try
		{
			logger->flush();
		}
		catch (const std::exception& error)
		{
			std::fprintf(stderr, "[Ursa log] flush failed: %s\n", error.what());
		}
		logger.reset();
	}
	minimumLevel.store(DefaultLevel(), std::memory_order_release);
}

bool Log::Initialized() noexcept
{
	std::lock_guard<std::mutex> lock(logMutex);
	return static_cast<bool>(logger);
}

LogLevel Log::Level() noexcept
{
	return minimumLevel.load(std::memory_order_acquire);
}

void Log::SetLevel(LogLevel level) noexcept
{
	if (!valid(level))
	{
		return;
	}
	std::lock_guard<std::mutex> lock(logMutex);
	try
	{
		if (logger)
		{
			logger->set_level(NativeLevel(level));
		}
		minimumLevel.store(level, std::memory_order_release);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[Ursa log] cannot change level: %s\n", error.what());
	}
}

bool Log::Enabled(LogLevel level) noexcept
{
	return valid(level) && level != LogLevel::off && level >= minimumLevel.load(std::memory_order_acquire);
}

void Log::Write(LogLevel level, const char* module, const String& message) noexcept
{
	if (!Enabled(level))
	{
		return;
	}
	std::lock_guard<std::mutex> lock(logMutex);
	try
	{
		if (logger)
		{
			logger->log(NativeLevel(level), "[{}] {}", module ? module : "Unknown",
			            std::string(message.data(), message.size()));
			return;
		}
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[Ursa log] write failed: %s\n", error.what());
	}
	fallback(level, module, message);
}

LogSession::LogSession(const LogConfig& config) : status(Log::InitializeForCurrentProcess(config))
{
}

LogSession::~LogSession()
{
	Log::Shutdown();
}
} // namespace Ursa
