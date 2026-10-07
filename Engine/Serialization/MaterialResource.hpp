#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace Ursa
{
using TextureIdResolver = std::function<Result<ResourceId>(MaterialTexturePtr)>;
using TextureResourceLoader = std::function<Result<MaterialTexturePtr>(ResourceId)>;

struct LoadedMaterialResource
{
	ResourceId id;
	std::shared_ptr<MaterialInstance> material;
};

Result<void> SaveMaterialResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                  const MaterialInstance& material, const TextureIdResolver& resolveTexture);

Result<LoadedMaterialResource> LoadMaterialResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                    const TextureResourceLoader& loadTexture);
} // namespace Ursa
