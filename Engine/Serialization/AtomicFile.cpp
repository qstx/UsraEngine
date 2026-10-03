#include <Serialization/AtomicFile.hpp>
#include <Serialization/ResourceId.hpp>
#include <Core/StringInterop.hpp>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Ursa
{
namespace
{
Result<void> replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target)
{
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesW(target.c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES)
	{
		if (!ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
		                  nullptr))
		{
			return Result<void>::Failure("Cannot replace resource " + PathToUrsaString(target) + ": Windows error " +
			                             ToUrsaString(std::to_string(GetLastError())));
		}
	}
	else
	{
		const DWORD error = GetLastError();
		if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
		{
			return Result<void>::Failure("Cannot inspect target resource " + PathToUrsaString(target) +
			                             ": Windows error " + ToUrsaString(std::to_string(error)));
		}
		if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			return Result<void>::Failure("Cannot install resource " + PathToUrsaString(target) + ": Windows error " +
			                             ToUrsaString(std::to_string(GetLastError())));
		}
	}
#else
	std::error_code error;
	std::filesystem::rename(temporary, target, error);
	if (error)
	{
		return Result<void>::Failure("Cannot replace resource " + PathToUrsaString(target) + ": " +
		                             ToUrsaString(error.message()));
	}
#endif
	return {};
}
} // namespace

Result<void> WriteResourceAtomically(const std::filesystem::path& target, const ResourceWriteFunction& write,
                                     const ResourceReplaceFunction& replace)
{
	if (!write)
	{
		return Result<void>::Failure("Resource writer is empty");
	}
	std::error_code error;
	const auto absolute = std::filesystem::absolute(target, error);
	if (error)
	{
		return Result<void>::Failure("Cannot resolve resource target " + PathToUrsaString(target) + ": " +
		                             ToUrsaString(error.message()));
	}
	const auto temporary = absolute.parent_path() / (absolute.filename().u8string() + "." +
	                                                 ToStdString(ResourceId::Generate().ToString()) + ".tmp");
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			return Result<void>::Failure("Cannot create temporary resource file " + PathToUrsaString(temporary));
		}
		auto result = write(output);
		if (result)
		{
			output.flush();
			if (!output)
			{
				result = Result<void>::Failure("Cannot flush temporary resource file " + PathToUrsaString(temporary));
			}
		}
		output.close();
		if (result && !output)
		{
			result = Result<void>::Failure("Cannot close temporary resource file " + PathToUrsaString(temporary));
		}
		if (!result)
		{
			std::filesystem::remove(temporary, error);
			return result;
		}
	}
	const auto result = replace ? replace(temporary, absolute) : replaceFile(temporary, absolute);
	if (!result)
	{
		std::filesystem::remove(temporary, error);
	}
	return result;
}
} // namespace Ursa
