#include <core/Log.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void run(const std::filesystem::path &root) {
    using namespace ursa;
    static_assert(static_cast<int>(LogLevel::Trace) < static_cast<int>(LogLevel::Debug));
    static_assert(static_cast<int>(LogLevel::Debug) < static_cast<int>(LogLevel::Info));
    static_assert(static_cast<int>(LogLevel::Info) < static_cast<int>(LogLevel::Warn));
    static_assert(static_cast<int>(LogLevel::Warn) < static_cast<int>(LogLevel::Error));
    static_assert(static_cast<int>(LogLevel::Error) < static_cast<int>(LogLevel::Critical));
    static_assert(static_cast<int>(LogLevel::Critical) < static_cast<int>(LogLevel::Off));

#ifndef NDEBUG
    expect(Log::defaultLevel() == LogLevel::Debug, "Debug default level is wrong");
#else
    expect(Log::defaultLevel() == LogLevel::Info, "Release default level is wrong");
#endif
    Log::write(LogLevel::Error, "Bootstrap", "message before initialization");

    const auto blocked = root / "blocked";
    {
        std::ofstream file(blocked);
        file << "not a directory";
    }
    LogConfig badConfig;
    badConfig.filePath = blocked / "log.txt";
    auto failed = Log::initialize(root / "LogTests.exe", badConfig);
    expect(!failed && failed.error.find("blocked") != String::npos && !Log::initialized(),
           "Unwritable log path was not diagnosed");

    const auto filePath = root / "logs" / "rotation.log";
    LogConfig config;
    config.filePath = filePath;
    config.level = LogLevel::Trace;
    config.maxFileBytes = 512;
    config.maxFiles = 2;
    auto initialized = Log::initialize(root / "LogTests.exe", config);
    expect(static_cast<bool>(initialized) && Log::initialized(), "Log initialization failed");
    expect(Log::level() == LogLevel::Trace, "Configured log level was lost");
    URSA_LOG_TRACE("Runtime", "trace message");
    URSA_LOG_DEBUG("Runtime", "debug message");
    URSA_LOG_INFO("Runtime", "info message");
    URSA_LOG_WARN("RHI", String(u8"警告消息"));
    const std::string immediate = readFile(filePath);
    expect(immediate.find("[RHI]") != std::string::npos &&
               immediate.find(u8"警告消息") != std::string::npos &&
               immediate.find("[warning]") != std::string::npos,
           "Warn was not flushed with level, module and UTF-8 text");
    URSA_LOG_CRITICAL("Runtime", "critical message");

    Log::setLevel(LogLevel::Warn);
    int evaluated = 0;
    URSA_LOG_INFO("Runtime", String(std::to_string(++evaluated).c_str()));
    expect(evaluated == 0 && !Log::enabled(LogLevel::Info), "Filtered message was evaluated");
    URSA_LOG_ERROR("Runtime", "kept error");
    Log::setLevel(LogLevel::Off);
    URSA_LOG_CRITICAL("Runtime", "filtered critical");
    expect(!Log::enabled(LogLevel::Critical), "Off did not suppress messages");
    Log::setLevel(LogLevel::Trace);
    for (int index = 0; index < 40; ++index)
        Log::write(LogLevel::Info, "Rotation", String(("entry-" + std::to_string(index) +
                                                        std::string(100, 'x')).c_str()));
    Log::shutdown();
    expect(!Log::initialized(), "Shutdown retained the logger");
    expect(readFile(filePath).find("filtered critical") == std::string::npos,
           "Filtered message reached the file");
    size_t rotatedFiles = 0;
    for (const auto &entry : std::filesystem::directory_iterator(filePath.parent_path()))
        if (entry.path().filename().string().find("rotation") == 0)
            ++rotatedFiles;
    expect(rotatedFiles >= 2 && rotatedFiles <= 3, "Rotation did not honor the file limit");

    config.filePath = root / "logs" / "threads.log";
    config.maxFileBytes = 1024 * 1024;
    expect(static_cast<bool>(Log::initialize(root / "LogTests.exe", config)), "Thread log init failed");
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 4; ++thread)
        threads.emplace_back([thread] {
            for (int index = 0; index < 50; ++index)
                Log::write(LogLevel::Info, "Threads",
                           String(("thread-" + std::to_string(thread) + "-" + std::to_string(index)).c_str()));
        });
    for (auto &thread : threads)
        thread.join();
    Log::shutdown();
    const auto allMessages = readFile(config.filePath);
    for (int thread = 0; thread < 4; ++thread)
        for (int index = 0; index < 50; ++index) {
            const auto message = "thread-" + std::to_string(thread) + "-" + std::to_string(index);
            expect(allMessages.find(message + "\n") != std::string::npos ||
                       allMessages.find(message + "\r\n") != std::string::npos,
                   "Concurrent message was lost or corrupted");
        }

    expect(static_cast<bool>(Log::initialize(root / "bin" / "LogTests.exe")), "Default log init failed");
    URSA_LOG_ERROR("Runtime", "default path message");
    Log::shutdown();
    expect(readFile(root / "bin" / "logs" / "LogTests.log").find("default path message") != std::string::npos,
           "Default log path is not beside the executable");
    Log::write(LogLevel::Error, "Bootstrap", "message after shutdown");
}
} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("UrsaLogTests-" + std::to_string(unique));
    try {
        std::filesystem::create_directories(root);
        run(root);
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return 0;
    } catch (const std::exception &error) {
        ursa::Log::shutdown();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
