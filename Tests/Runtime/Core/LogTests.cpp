#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Log.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

std::string ReadFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void Run(const std::filesystem::path& root)
{
	using namespace Ursa;
	static_assert(static_cast<int>(LogLevel::Trace) < static_cast<int>(LogLevel::Debug));
	static_assert(static_cast<int>(LogLevel::Debug) < static_cast<int>(LogLevel::Info));
	static_assert(static_cast<int>(LogLevel::Info) < static_cast<int>(LogLevel::Warn));
	static_assert(static_cast<int>(LogLevel::Warn) < static_cast<int>(LogLevel::Error));
	static_assert(static_cast<int>(LogLevel::Error) < static_cast<int>(LogLevel::Critical));
	static_assert(static_cast<int>(LogLevel::Critical) < static_cast<int>(LogLevel::Off));

#ifndef NDEBUG
	Expect(Log::DefaultLevel() == LogLevel::Debug, "Debug default level is wrong");
#else
	Expect(Log::DefaultLevel() == LogLevel::Info, "Release default level is wrong");
#endif
	Vector<LogRecord> observedRecords;
	const Ursa::UInt64 listener =
	    Log::Subscribe([&observedRecords](const LogRecord& record) { observedRecords.push_back(record); });
	Log::Write(LogLevel::Error, "Bootstrap", "message before initialization",
	           {"prj:Textures/Albedo.png", "11111111-1111-4111-8111-111111111111"});
	Expect(listener != 0 && observedRecords.size() == 1 && observedRecords.back().category == "Bootstrap" &&
	           observedRecords.back().context.resourcePath == "prj:Textures/Albedo.png" &&
	           observedRecords.back().context.objectId == "11111111-1111-4111-8111-111111111111",
	       "Log listener did not receive the structured resource and object context");

	const auto blocked = root / "blocked";
	{
		std::ofstream file(blocked);
		file << "not a directory";
	}
	LogConfig badConfig;
	badConfig.filePath = blocked / "log.txt";
	auto failed = Log::Initialize(root / "LogTests.exe", badConfig);
	Expect(!failed && failed.error.find("blocked") != String::npos && !Log::Initialized(),
	       "Unwritable log path was not diagnosed");

	const auto filePath = root / "logs" / "rotation.log";
	LogConfig config;
	config.filePath = filePath;
	config.level = LogLevel::Trace;
	config.maxFileBytes = 512;
	config.maxFiles = 2;
	auto initialized = Log::Initialize(root / "LogTests.exe", config);
	Expect(static_cast<bool>(initialized) && Log::Initialized(), "Log initialization failed");
	Expect(Log::Level() == LogLevel::Trace, "Configured log level was lost");
	Log::Write(LogLevel::Info, "Console", "visible log entry");
	Expect(observedRecords.size() == 2 && observedRecords.back().message == "visible log entry",
	       "Log listener did not receive initialized logger entries");
	Log::Unsubscribe(listener);
	Log::Write(LogLevel::Info, "Console", "unsubscribed log entry");
	Expect(observedRecords.size() == 2, "Log listener remained active after unsubscription");
	URSA_LOG_TRACE("Runtime", "trace message");
	URSA_LOG_DEBUG("Runtime", "debug message");
	URSA_LOG_INFO("Runtime", "info message");
	URSA_LOG_WARN("RHI", String(u8"警告消息"));
	const std::string immediate = ReadFile(filePath);
	Expect(immediate.find("[RHI]") != std::string::npos && immediate.find(u8"警告消息") != std::string::npos &&
	           immediate.find("[warning]") != std::string::npos,
	       "Warn was not flushed with level, module and UTF-8 text");
	URSA_LOG_CRITICAL("Runtime", "critical message");

	Log::SetLevel(LogLevel::Warn);
	int evaluated = 0;
	URSA_LOG_INFO("Runtime", String(std::to_string(++evaluated).c_str()));
	Expect(evaluated == 0 && !Log::Enabled(LogLevel::Info), "Filtered message was evaluated");
	URSA_LOG_ERROR("Runtime", "kept error");
	Log::SetLevel(LogLevel::Off);
	URSA_LOG_CRITICAL("Runtime", "filtered critical");
	Expect(!Log::Enabled(LogLevel::Critical), "Off did not suppress messages");
	Log::SetLevel(LogLevel::Trace);
	for (int index = 0; index < 40; ++index)
	{
		Log::Write(LogLevel::Info, "Rotation",
		           String(("entry-" + std::to_string(index) + std::string(100, 'x')).c_str()));
	}
	Log::Shutdown();
	Expect(!Log::Initialized(), "Shutdown retained the logger");
	Expect(ReadFile(filePath).find("filtered critical") == std::string::npos, "Filtered message reached the file");
	Ursa::Size rotatedFiles = 0;
	for (const auto& entry : std::filesystem::directory_iterator(filePath.parent_path()))
	{
		if (entry.path().filename().string().find("rotation") == 0)
		{
			++rotatedFiles;
		}
	}
	Expect(rotatedFiles >= 2 && rotatedFiles <= 3, "Rotation did not honor the file limit");

	config.filePath = root / "logs" / "threads.log";
	config.maxFileBytes = 1024 * 1024;
	Expect(static_cast<bool>(Log::Initialize(root / "LogTests.exe", config)), "Thread log init failed");
	Ursa::Vector<std::thread> threads;
	for (int thread = 0; thread < 4; ++thread)
	{
		threads.emplace_back(
		    [thread]
		    {
			    for (int index = 0; index < 50; ++index)
			    {
				    Log::Write(LogLevel::Info, "Threads",
				               String(("thread-" + std::to_string(thread) + "-" + std::to_string(index)).c_str()));
			    }
		    });
	}
	for (auto& thread : threads)
	{
		thread.join();
	}
	Log::Shutdown();
	const auto allMessages = ReadFile(config.filePath);
	for (int thread = 0; thread < 4; ++thread)
	{
		for (int index = 0; index < 50; ++index)
		{
			const auto message = "thread-" + std::to_string(thread) + "-" + std::to_string(index);
			Expect(allMessages.find(message + "\n") != std::string::npos ||
			           allMessages.find(message + "\r\n") != std::string::npos,
			       "Concurrent message was lost or corrupted");
		}
	}

	LogConfig defaultConfig;
	defaultConfig.userDataDirectoryOverride = root / "User";
	Expect(static_cast<bool>(Log::Initialize(root / "bin" / "LogTests.exe", defaultConfig)), "Default log init failed");
	URSA_LOG_ERROR("Runtime", "default path message");
	Log::Shutdown();
	Expect(ReadFile(root / "User" / "Logs" / "LogTests.log").find("default path message") != std::string::npos,
	       "Default log path is not in the user data mount");
	Log::Write(LogLevel::Error, "Bootstrap", "message after shutdown");
	auto virtualFiles = std::make_shared<VirtualFileSystem>();
	auto virtualPath = VirtualPath::Parse("user:Logs/Test.log").value;
	Expect(!Log::Initialize(virtualFiles, virtualPath), "Missing user mount was accepted for logging");
	Expect(static_cast<bool>(virtualFiles->MountProvider(userDataScheme, std::make_shared<MemoryFileProvider>(),
	                                                     MountAccess::ReadOnly)),
	       "Cannot mount read-only user fixture");
	Expect(!Log::Initialize(virtualFiles, virtualPath) && !Log::Initialized(), "Read-only log mount was accepted");
	Log::Write(LogLevel::Error, "Bootstrap", "fallback after VFS log failure");
}
} // namespace

int main()
{
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root = std::filesystem::temp_directory_path() / ("UrsaLogTests-" + std::to_string(unique));
	try
	{
		std::filesystem::create_directories(root);
		Run(root);
		std::error_code error;
		std::filesystem::remove_all(root, error);
		return 0;
	}
	catch (const std::exception& error)
	{
		Ursa::Log::Shutdown();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
