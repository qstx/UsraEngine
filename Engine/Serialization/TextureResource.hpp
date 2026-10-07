#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Serialization/ResourceId.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <filesystem>

namespace Ursa
{
Result<void> SaveTextureResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                 const MaterialTexture& texture);

Result<struct LoadedTextureResource> LoadTextureResource(const VirtualFileSystem& filesystem, const VirtualPath& path);

struct LoadedTextureResource
{
	ResourceId id;
	MaterialTexturePtr texture;
};

} // namespace Ursa
