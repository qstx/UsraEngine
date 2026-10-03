#include <Vfs/AssetRoots.hpp>
#include <Core/StringInterop.hpp>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace Ursa
{
namespace
{
bool IsReadableDirectory(const std::filesystem::path& path)
{
	if (path.empty())
	{
		return false;
	}
	std::error_code error;
	return std::filesystem::is_directory(path, error) && !error;
}

std::filesystem::path EngineAssetSource()
{
#ifdef URSA_ENGINE_ASSET_SOURCE_DIR
	return std::filesystem::u8path(URSA_ENGINE_ASSET_SOURCE_DIR);
#else
	return {};
#endif
}

std::filesystem::path EngineAssetMirror()
{
#ifdef URSA_ASSET_MIRROR_DIR
	return std::filesystem::u8path(URSA_ASSET_MIRROR_DIR);
#else
	return {};
#endif
}
} // namespace

Result<std::filesystem::path> ResolveEngineAssetRoot(const EngineAssetRootCandidates& candidates)
{
	if (!candidates.explicitRoot.empty())
	{
		if (!IsReadableDirectory(candidates.explicitRoot))
		{
			return Result<std::filesystem::path>::Failure("Engine asset root is not a readable directory: " +
			                                              PathToUrsaString(candidates.explicitRoot));
		}
		return Result<std::filesystem::path>::Success(candidates.explicitRoot);
	}

	const std::filesystem::path besideExecutable = candidates.executablePath.empty()
	                                                   ? std::filesystem::path{}
	                                                   : candidates.executablePath.parent_path() / "Assets";
	if (IsReadableDirectory(besideExecutable))
	{
		return Result<std::filesystem::path>::Success(besideExecutable);
	}

	if (IsReadableDirectory(candidates.buildMirror))
	{
		return Result<std::filesystem::path>::Success(candidates.buildMirror);
	}

	return Result<std::filesystem::path>::Failure("No engine asset root found. Tried an explicit override, " +
	                                              PathToUrsaString(besideExecutable) + ", and " +
	                                              PathToUrsaString(candidates.buildMirror));
}

Result<Vector<std::filesystem::path>> ResolveEngineAssetRoots(const std::filesystem::path& explicitRoot)
{
	if (!explicitRoot.empty())
	{
		if (!IsReadableDirectory(explicitRoot))
		{
			return Result<Vector<std::filesystem::path>>::Failure("Engine asset root is not a readable directory: " +
			                                                      PathToUrsaString(explicitRoot));
		}
		Vector<std::filesystem::path> single;
		single.push_back(explicitRoot);
		return Result<Vector<std::filesystem::path>>::Success(std::move(single));
	}

	const auto executable = CurrentExecutablePath();
	const std::filesystem::path besideExecutable =
	    executable.empty() ? std::filesystem::path{} : executable.parent_path() / "Assets";
	if (IsReadableDirectory(besideExecutable))
	{
		Vector<std::filesystem::path> packaged;
		packaged.push_back(besideExecutable);
		return Result<Vector<std::filesystem::path>>::Success(std::move(packaged));
	}

	const auto source = EngineAssetSource();
	const auto mirror = EngineAssetMirror();
	Vector<std::filesystem::path> roots;
	if (IsReadableDirectory(source))
	{
		roots.push_back(source);
	}
	if (IsReadableDirectory(mirror))
	{
		roots.push_back(mirror);
	}
	if (roots.empty())
	{
		return Result<Vector<std::filesystem::path>>::Failure(
		    "No engine asset root found. Tried " + PathToUrsaString(besideExecutable) + ", " +
		    PathToUrsaString(source) + ", and " + PathToUrsaString(mirror));
	}
	return Result<Vector<std::filesystem::path>>::Success(std::move(roots));
}

Result<VirtualFileSystem> OpenEngineFileSystem(const std::filesystem::path& explicitRoot)
{
	auto roots = ResolveEngineAssetRoots(explicitRoot);
	if (!roots)
	{
		return Result<VirtualFileSystem>::Failure(roots.error);
	}
	VirtualFileSystem files;
	for (const std::filesystem::path& root : roots.value)
	{
		auto mounted = files.Mount(engineScheme, root, MountAccess::readOnly);
		if (!mounted)
		{
			return Result<VirtualFileSystem>::Failure(mounted.error);
		}
	}
	return Result<VirtualFileSystem>::Success(std::move(files));
}

std::filesystem::path CurrentExecutablePath()
{
#ifdef _WIN32
	std::vector<wchar_t> buffer(MAX_PATH);
	while (true)
	{
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0)
		{
			return {};
		}
		if (length < buffer.size() - 1)
		{
			return std::filesystem::path(std::wstring(buffer.data(), length));
		}
		buffer.resize(buffer.size() * 2);
	}
#else
	std::vector<char> buffer(PATH_MAX);
	const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
	if (length <= 0)
	{
		return {};
	}
	return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length)));
#endif
}
} // namespace Ursa
