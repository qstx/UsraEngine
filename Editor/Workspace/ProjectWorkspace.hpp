#pragma once

#include <Serialization/ResourceIndex.hpp>
#include <filesystem>
#include <memory>

namespace Ursa::Editor
{
// The scheme names are a convention this editor registers: project resources
// live under "<project>/Assets", engine resources under the read-only mounts.
inline constexpr const char* projectResourceScheme = projectScheme;
inline constexpr const char* engineResourceScheme = engineScheme;

class ProjectWorkspace final
{
public:
	static Result<std::unique_ptr<ProjectWorkspace>> Open(const std::filesystem::path& projectDirectory,
	                                                      const Vector<ResourceMount>& engineMounts = {});

	Result<void> Refresh();

	// Host path a save would target. The virtual path must resolve inside the
	// writable project mount.
	Result<std::filesystem::path> WritablePath(const VirtualPath& path) const;

	Vector<ResourceEntry> Resources() const
	{
		return index.Entries();
	}

	const ResourceIndex& Index() const
	{
		return index;
	}

	const std::filesystem::path& ProjectRoot() const
	{
		return projectRoot;
	}

	const std::filesystem::path& AssetsRoot() const
	{
		return assetsRoot;
	}

private:
	std::filesystem::path projectRoot;
	std::filesystem::path assetsRoot;
	Vector<ResourceMount> engineMounts;
	ResourceIndex index;
};
} // namespace Ursa::Editor
