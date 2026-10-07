#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>
#include <memory>

namespace Ursa
{
enum class RuntimeMountMode
{
	Editor,
	LooseRuntime,
	PackagedRuntime
};

struct PackagedSchemeProvider
{
	String scheme;
	std::shared_ptr<VirtualFileProvider> provider;
};

struct RuntimeMountProfileConfig
{
	RuntimeMountMode mode = RuntimeMountMode::LooseRuntime;
	String applicationId = "UrsaEngine";
	Vector<std::filesystem::path> engineDirectories;
	std::filesystem::path projectAssetsDirectory;
	Vector<PackagedSchemeProvider> packagedProviders;
	std::filesystem::path userDataDirectoryOverride;
};

Result<std::filesystem::path> ResolveUserDataDirectory(const String& applicationId,
                                                       const std::filesystem::path& overrideDirectory = {});

Result<VirtualFileSystem> OpenRuntimeFileSystem(const RuntimeMountProfileConfig& config);
} // namespace Ursa
