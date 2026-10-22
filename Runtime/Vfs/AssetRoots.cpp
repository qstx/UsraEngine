#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Profiler.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/PakArchive.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <mutex>

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
URSA_PROFILE_LOCK(std::mutex, engineFilesMutex, "Ursa.Vfs.EngineFiles");
std::shared_ptr<const VirtualFileSystem> configuredEngineFiles;

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

	return Result<std::filesystem::path>::Failure("No deployed engine asset root found; run UrsaAssetImport --project "
	                                              "<project> and configure the project content view");
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

	auto source = ResolveEngineSourceRoot();
	return source ? Result<Vector<std::filesystem::path>>::Success({source.value})
	              : Result<Vector<std::filesystem::path>>::Failure(source.error);
}

Result<std::filesystem::path> ResolveEngineSourceRoot(const std::filesystem::path& explicitRoot)
{
	const auto source = explicitRoot.empty() ? EngineAssetSource() : explicitRoot;
	return IsReadableDirectory(source) ? Result<std::filesystem::path>::Success(source)
	                                   : Result<std::filesystem::path>::Failure(
	                                         "Builtin source assets are unavailable: " + PathToUrsaString(source));
}

String EngineBuildConfiguration()
{
	return URSA_BUILD_CONFIGURATION;
}

String EngineFrameworkScheme(const String& configuration)
{
	String scheme = "framework" + configuration;
	for (char& character : scheme)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character - 'A' + 'a');
		}
	}
	return scheme;
}

std::filesystem::path EngineFrameworkDirectory()
{
	return std::filesystem::u8path(URSA_ENGINE_FRAMEWORK_DIR);
}

Result<VirtualFileSystem> OpenEngineFileSystem(const std::filesystem::path& explicitRoot)
{
	if (explicitRoot.empty())
	{
		std::lock_guard lock(engineFilesMutex);
		if (configuredEngineFiles)
		{
			return Result<VirtualFileSystem>::Success(*configuredEngineFiles);
		}
#ifdef _WIN32
		wchar_t selectedProject[32768]{};
		const auto length = GetEnvironmentVariableW(L"URSA_CONTENT_PROJECT", selectedProject, 32768);
		const std::filesystem::path project =
		    length && length < 32768 ? std::filesystem::path(selectedProject) : std::filesystem::path{};
#else
		const auto* selectedProject = std::getenv("URSA_CONTENT_PROJECT");
		const auto project = selectedProject ? std::filesystem::u8path(selectedProject) : std::filesystem::path{};
#endif
		if (!project.empty())
		{
			auto storage = std::make_shared<VirtualFileSystem>();
			auto mounted = storage->Mount(editorLibraryScheme, project / "Library", MountAccess::ReadOnly);
			if (!mounted)
			{
				return Result<VirtualFileSystem>::Failure(mounted.error);
			}
			auto ready = ReadRuntimeContent(*storage, EngineBuildConfiguration(), "Editor");
			if (!ready)
			{
				return Result<VirtualFileSystem>::Failure(ready.error);
			}
			auto view = OpenRuntimeContentView(storage, ready.value);
			if (!view)
			{
				return view;
			}
			VirtualFileSystem engine;
			for (const auto& provider : view.value.Providers())
			{
				if (provider.scheme == engineScheme)
				{
					auto mountedView = engine.MountProvider(engineScheme, provider.provider, MountAccess::ReadOnly);
					if (!mountedView)
					{
						return Result<VirtualFileSystem>::Failure(mountedView.error);
					}
				}
			}
			return Result<VirtualFileSystem>::Success(std::move(engine));
		}
		const auto package = CurrentExecutablePath().parent_path() / "Content.pak";
		std::error_code error;
		if (std::filesystem::is_regular_file(package, error) && !error)
		{
			return OpenEngineFileSystem(package);
		}
	}
	if (explicitRoot.extension() == ".pak")
	{
		auto archive = PakArchive::Open(explicitRoot);
		if (!archive)
		{
			return Result<VirtualFileSystem>::Failure(archive.error);
		}
		VirtualFileSystem files;
		auto mounted = archive.value->Mount(files);
		return mounted ? Result<VirtualFileSystem>::Success(std::move(files))
		               : Result<VirtualFileSystem>::Failure(mounted.error);
	}
	if (explicitRoot.empty())
	{
		return Result<VirtualFileSystem>::Failure("Runtime content view is not configured; run UrsaAssetImport "
		                                          "--project <project> and open its prepared content");
	}
	auto roots = ResolveEngineAssetRoots(explicitRoot);
	if (!roots)
	{
		return Result<VirtualFileSystem>::Failure(roots.error);
	}
	VirtualFileSystem files;
	for (const std::filesystem::path& root : roots.value)
	{
		auto mounted = files.Mount(engineScheme, root, MountAccess::ReadOnly);
		if (!mounted)
		{
			return Result<VirtualFileSystem>::Failure(mounted.error);
		}
	}
	return Result<VirtualFileSystem>::Success(std::move(files));
}

void ConfigureEngineFileSystem(std::shared_ptr<const VirtualFileSystem> filesystem)
{
	std::lock_guard lock(engineFilesMutex);
	configuredEngineFiles = std::move(filesystem);
}

std::filesystem::path CurrentExecutablePath()
{
#ifdef _WIN32
	Ursa::Vector<wchar_t> buffer(MAX_PATH);
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
	Ursa::Vector<char> buffer(PATH_MAX);
	const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
	if (length <= 0)
	{
		return {};
	}
	return std::filesystem::path(std::string(buffer.data(), static_cast<Ursa::Size>(length)));
#endif
}
} // namespace Ursa
