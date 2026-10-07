#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Ursa
{
namespace
{
Result<void> MountDirectories(VirtualFileSystem& filesystem, const String& scheme,
                              const Vector<std::filesystem::path>& directories)
{
	for (const std::filesystem::path& directory : directories)
	{
		auto mounted = filesystem.Mount(scheme, directory, MountAccess::ReadOnly);
		if (!mounted)
		{
			return mounted;
		}
	}
	return {};
}

Result<std::filesystem::path> WindowsLocalDataRoot()
{
	Ursa::Vector<wchar_t> buffer(32768);
	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
	if (length == 0 || length >= buffer.size())
	{
		return Result<std::filesystem::path>::Failure("Cannot locate the Windows LocalAppData directory");
	}
	return Result<std::filesystem::path>::Success(std::filesystem::path(std::wstring(buffer.data(), length)));
}
} // namespace

Result<std::filesystem::path> ResolveUserDataDirectory(const String& applicationId,
                                                       const std::filesystem::path& overrideDirectory)
{
	if (!overrideDirectory.empty())
	{
		return Result<std::filesystem::path>::Success(overrideDirectory);
	}
	if (applicationId.empty() || applicationId == "." || applicationId == ".." ||
	    applicationId.find('/') != String::npos || applicationId.find('\\') != String::npos ||
	    applicationId.find(':') != String::npos)
	{
		return Result<std::filesystem::path>::Failure("Application ID is not a valid user-data directory name");
	}
	std::filesystem::path base;
#ifdef _WIN32
	auto localData = WindowsLocalDataRoot();
	if (!localData)
	{
		return Result<std::filesystem::path>::Failure(localData.error);
	}
	base = std::move(localData.value);
#elif defined(__APPLE__)
	const char* home = std::getenv("HOME");
	if (!home || !*home)
	{
		return Result<std::filesystem::path>::Failure("Cannot locate the user's home directory");
	}
	base = std::filesystem::u8path(home) / "Library" / "Application Support";
#else
	const char* dataHome = std::getenv("XDG_DATA_HOME");
	if (dataHome && *dataHome)
	{
		base = std::filesystem::u8path(dataHome);
	}
	else
	{
		const char* home = std::getenv("HOME");
		if (!home || !*home)
		{
			return Result<std::filesystem::path>::Failure("Cannot locate the user's home directory");
		}
		base = std::filesystem::u8path(home) / ".local" / "share";
	}
#endif
	return Result<std::filesystem::path>::Success(base / PathFromUrsaString(applicationId));
}

Result<VirtualFileSystem> OpenRuntimeFileSystem(const RuntimeMountProfileConfig& config)
{
	VirtualFileSystem filesystem;
	if (config.mode == RuntimeMountMode::PackagedRuntime)
	{
		bool mountedEngine = false;
		bool mountedProject = false;
		for (const PackagedSchemeProvider& packaged : config.packagedProviders)
		{
			if (packaged.scheme != engineScheme && packaged.scheme != projectScheme)
			{
				return Result<VirtualFileSystem>::Failure("Packaged runtime providers may only mount ursa or prj");
			}
			auto mounted = filesystem.MountProvider(packaged.scheme, packaged.provider, MountAccess::ReadOnly);
			if (!mounted)
			{
				return Result<VirtualFileSystem>::Failure(mounted.error);
			}
			mountedEngine |= packaged.scheme == engineScheme;
			mountedProject |= packaged.scheme == projectScheme;
		}
		if (!mountedEngine || !mountedProject)
		{
			return Result<VirtualFileSystem>::Failure("Packaged runtime needs providers for both ursa and prj");
		}
	}
	else
	{
		if (config.engineDirectories.empty())
		{
			return Result<VirtualFileSystem>::Failure(
			    "Runtime mount profile needs at least one Engine asset directory");
		}
		auto mountedEngine = MountDirectories(filesystem, engineScheme, config.engineDirectories);
		if (!mountedEngine)
		{
			return Result<VirtualFileSystem>::Failure(mountedEngine.error);
		}
		if (config.projectAssetsDirectory.empty())
		{
			return Result<VirtualFileSystem>::Failure("Runtime mount profile needs a project assets directory");
		}
		const MountAccess projectAccess =
		    config.mode == RuntimeMountMode::Editor ? MountAccess::ReadWrite : MountAccess::ReadOnly;
		auto mountedProject = filesystem.Mount(projectScheme, config.projectAssetsDirectory, projectAccess);
		if (!mountedProject)
		{
			return Result<VirtualFileSystem>::Failure(mountedProject.error);
		}
		if (config.mode == RuntimeMountMode::Editor)
		{
			if (config.projectLibraryDirectory.empty())
			{
				return Result<VirtualFileSystem>::Failure("Editor mount profile needs a project Library directory");
			}
			auto libraryProvider = DirectoryFileProvider::Open(config.projectLibraryDirectory, true);
			if (!libraryProvider)
			{
				return Result<VirtualFileSystem>::Failure(libraryProvider.error);
			}
			auto mountedLibrary =
			    filesystem.MountProvider(editorLibraryScheme, std::move(libraryProvider.value), MountAccess::ReadWrite);
			if (!mountedLibrary)
			{
				return Result<VirtualFileSystem>::Failure(mountedLibrary.error);
			}
		}
	}

	auto userRoot = ResolveUserDataDirectory(config.applicationId, config.userDataDirectoryOverride);
	if (!userRoot)
	{
		return Result<VirtualFileSystem>::Failure(userRoot.error);
	}
	auto userProvider = DirectoryFileProvider::Open(userRoot.value, true);
	if (!userProvider)
	{
		return Result<VirtualFileSystem>::Failure(userProvider.error);
	}
	auto mountedUser = filesystem.MountProvider(userDataScheme, std::move(userProvider.value), MountAccess::ReadWrite);
	if (!mountedUser)
	{
		return Result<VirtualFileSystem>::Failure(mountedUser.error);
	}
	return Result<VirtualFileSystem>::Success(std::move(filesystem));
}
} // namespace Ursa
