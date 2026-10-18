#include <Core/Log.hpp>
#include <chrono>
#include <filesystem>
#include <string>

int TestLogHeader()
{
	const auto logRoot =
	    std::filesystem::temp_directory_path() /
	    std::filesystem::u8path("UrsaLogHeaderSmoke-" +
	                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::error_code error;
	std::filesystem::remove_all(logRoot, error);
	Ursa::LogConfig config;
	config.userDataDirectoryOverride = logRoot;
	{
		Ursa::LogSession log(config);
		if (!log.GetStatus())
		{
			std::filesystem::remove_all(logRoot, error);
			return 1;
		}
		URSA_LOG_INFO("Example", "Logging ready");
	}
	std::filesystem::remove_all(logRoot, error);
	if (error)
	{
		return 1;
	}
	return 0;
}
