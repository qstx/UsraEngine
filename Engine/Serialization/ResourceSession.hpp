#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Serialization/ResourceIndex.hpp>
#include <Shader/ShaderAsset.hpp>
#include <memory>

namespace Ursa
{
// Loads and caches resources by ID through the index's virtual file system.
class ResourceSession final
{
public:
	static Result<std::unique_ptr<ResourceSession>> Open(const Vector<ResourceMount>& mounts);

	static Result<std::unique_ptr<ResourceSession>> Open(const VirtualFileSystem& filesystem);

	// Convenience for a caller that owns a single directory of resources; the
	// directory is mounted under the conventional project scheme.
	static Result<std::unique_ptr<ResourceSession>> Open(const std::filesystem::path& root,
	                                                     MountAccess access = MountAccess::ReadWrite);

	Result<std::unique_ptr<ResourceSession>> Refreshed() const;

	Result<MaterialTexturePtr> LoadTexture(ResourceId id);

	Result<std::shared_ptr<MaterialInstance>> LoadMaterial(ResourceId id);

	Result<std::shared_ptr<const Shader::ShaderAsset>> LoadShader(ResourceId id);

	Result<ResourceId> IdentifyMaterial(const std::shared_ptr<MaterialInstance>& material) const;

	const ResourceIndex& Index() const
	{
		return index;
	}

	ResourceIndex& Index()
	{
		return index;
	}

private:
	Result<std::shared_ptr<const Shader::ShaderAsset>> LoadShaderInternal(ResourceId id, Set<String>& active);

	ResourceIndex index;
	Map<String, MaterialTexturePtr> textures;
	Map<String, std::shared_ptr<MaterialInstance>> materials;
	Map<String, std::shared_ptr<const Shader::ShaderAsset>> shaders;
};
} // namespace Ursa
