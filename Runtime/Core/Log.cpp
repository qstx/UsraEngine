#include <Core/Containers.hpp>
#include <Core/Log.hpp>
#include <spdlog/logger.h>
#include <Core/VfsLogSink.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
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
	return level >= LogLevel::Trace && level <= LogLevel::Off;
}

spdlog::level::level_enum NativeLevel(LogLevel level)
{
	switch (level)
	{
		case LogLevel::Trace:
			return spdlog::level::trace;
		case LogLevel::Debug:
			return spdlog::level::debug;
		case LogLevel::Info:
			return spdlog::level::info;
		case LogLevel::Warn:
			return spdlog::level::warn;
		case LogLevel::Error:
			return spdlog::level::err;
		case LogLevel::Critical:
			return spdlog::level::critical;
		case LogLevel::Off:
			return spdlog::level::off;
	}
	return spdlog::level::off;
}

const char* LevelName(LogLevel level)
{
	switch (level)
	{
		case LogLevel::Trace:
			return "trace";
		case LogLevel::Debug:
			return "debug";
		case LogLevel::Info:
			return "info";
		case LogLevel::Warn:
			return "warn";
		case LogLevel::Error:
			return "error";
		case LogLevel::Critical:
			return "critical";
		case LogLevel::Off:
			return "off";
	}
	return "unknown";
}

void fallback(LogLevel level, const char* module, const String& message) noexcept
{
	std::fprintf(stderr, "[%s] [%s] %s\n", LevelName(level), module ? module : "Unknown", message.c_str());
	std::fflush(stderr);
}

void SetConsoleOutputUtf8() noexcept
{
#ifdef _WIN32
	const auto isConsole = [](DWORD standardHandle)
	{
		const HANDLE handle = GetStdHandle(standardHandle);
		DWORD mode = 0;
		return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != FALSE;
	};
	if (isConsole(STD_OUTPUT_HANDLE) || isConsole(STD_ERROR_HANDLE))
	{
		SetConsoleOutputCP(CP_UTF8);
	}
#endif
}
} // namespace

LogConfig::LogConfig() : level(Log::DefaultLevel())
{
}

LogLevel Log::DefaultLevel() noexcept
{
#ifndef NDEBUG
	return LogLevel::Debug;
#else
	return LogLevel::Info;
#endif
}

void Log::ConfigureConsoleEncoding() noexcept
{
	SetConsoleOutputUtf8();
}

Result<void> Log::Initialize(const std::filesystem::path& executable, const LogConfig& config)
{
	if (executable.empty())
	{
		return Result<void>::Failure("Invalid logging executable path");
	}
	auto root = ResolveUserDataDirectory(PathToUrsaString(executable.stem()), config.userDataDirectoryOverride);
	std::filesystem::path name = std::filesystem::path("Logs") / executable.filename();
	name.replace_extension(".log");
	if (!config.filePath.empty())
	{
		std::error_code error;
		const auto overridePath = std::filesystem::absolute(config.filePath, error);
		if (error)
		{
			return Result<void>::Failure("Invalid log bootstrap path");
		}
		root = Result<std::filesystem::path>::Success(overridePath.parent_path());
		name = overridePath.filename();
	}
	if (!root)
	{
		fallback(LogLevel::Error, "Log", root.error);
		return Result<void>::Failure(root.error);
	}
	auto provider = DirectoryFileProvider::Open(root.value, true);
	if (!provider)
	{
		const String error = "Cannot mount log user directory " + PathToUrsaString(root.value) + ": " + provider.error;
		fallback(LogLevel::Error, "Log", error);
		return Result<void>::Failure(error);
	}
	auto filesystem = std::make_shared<VirtualFileSystem>();
	auto mounted = filesystem->MountProvider(userDataScheme, provider.value, MountAccess::ReadWrite);
	auto path = VirtualPath::Parse("user:" + ToUrsaString(name.generic_u8string()));
	if (!mounted || !path)
	{
		return Result<void>::Failure(!mounted ? mounted.error : path.error);
	}
	return Initialize(std::move(filesystem), path.value, config);
}

Result<void> Log::Initialize(std::shared_ptr<VirtualFileSystem> filesystem, const VirtualPath& path,
                             const LogConfig& config)
{
	if (!filesystem || path.scheme != userDataScheme || !valid(config.level) || config.maxFileBytes == 0 ||
	    config.maxFiles == 0)
	{
		return Result<void>::Failure("Invalid user: logging path, level or rotation settings");
	}
	std::lock_guard<std::mutex> lock(logMutex);
	if (logger)
	{
		return Result<void>::Failure("Logging is already initialized");
	}
	try
	{
		auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
		auto file = std::make_shared<VfsLogSink>(std::move(filesystem), path, config.maxFileBytes, config.maxFiles);
		Vector<spdlog::sink_ptr> sinks{console, file};
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
		const String diagnostic = "Cannot initialize log " + path.ToString() + ": " + error.what();
		fallback(LogLevel::Error, "Log", diagnostic);
		return Result<void>::Failure(diagnostic);
	}
}

Result<void> Log::InitializeForCurrentProcess(const LogConfig& config)
{
#ifdef _WIN32
	ConfigureConsoleEncoding();
	std::wstring buffer(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (length == 0 || length >= buffer.size())
	{
		const String diagnostic = "Cannot locate executable for default log path";
		fallback(LogLevel::Error, "Log", diagnostic);
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
	return valid(level) && level != LogLevel::Off && level >= minimumLevel.load(std::memory_order_acquire);
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
