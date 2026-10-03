#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Serialization/ResourceIndex.hpp>
#include <memory>

namespace Ursa
{
// Loads and caches resources by ID through the index's virtual file system.
class ResourceSession final
{
public:
	static Result<std::unique_ptr<ResourceSession>> Open(const Vector<ResourceMount>& mounts);

	// Convenience for a caller that owns a single directory of resources; the
	// directory is mounted under the conventional project scheme.
	static Result<std::unique_ptr<ResourceSession>> Open(const std::filesystem::path& root,
	                                                     MountAccess access = MountAccess::readWrite);

	Result<std::unique_ptr<ResourceSession>> Refreshed() const;

	Result<MaterialTexturePtr> LoadTexture(ResourceId id);

	Result<std::shared_ptr<MaterialInstance>> LoadMaterial(ResourceId id);

	Result<ResourceId> IdentifyMaterial(const std::shared_ptr<MaterialInstance>& material) const;

	const ResourceIndex& Index() const
	{
		return index;
	}

private:
	ResourceIndex index;
	Map<String, MaterialTexturePtr> textures;
	Map<String, std::shared_ptr<MaterialInstance>> materials;
};
} // namespace Ursa
