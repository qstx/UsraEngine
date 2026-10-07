#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>

namespace Ursa
{
// One configured resource root. A scheme may be mounted several times; lookup
// orders the roots the way they were mounted.
struct ResourceMount
{
	String scheme;
	std::filesystem::path root;
	MountAccess access = MountAccess::ReadOnly;
	std::shared_ptr<VirtualFileProvider> provider;
};

struct ResourceLocation
{
	ResourceKind kind = ResourceKind::Texture;
	VirtualPath path;
};

struct ResourceEntry
{
	ResourceId id;
	ResourceLocation location;
};

// Maps stable resource IDs to virtual paths across the mounted resource roots.
class ResourceIndex final
{
public:
	Result<void> Mount(const ResourceMount& mount);

	Result<void> Rebuild();

	Result<ResourceLocation> Resolve(ResourceId id, const String& referencedBy = {}) const;

	Vector<ResourceEntry> Entries() const;

	VirtualFileSystem& Filesystem()
	{
		return filesystem;
	}

	const VirtualFileSystem& Filesystem() const
	{
		return filesystem;
	}

	const Vector<ResourceMount>& Mounts() const
	{
		return mounts;
	}

private:
	VirtualFileSystem filesystem;
	Vector<ResourceMount> mounts;
	Map<String, ResourceLocation> locations;
};
} // namespace Ursa
