#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <material/Material.hpp>
#include <serialization/ResourceId.hpp>
#include <filesystem>

namespace Ursa
{
struct LoadedTextureResource
{
	ResourceId id;
	MaterialTexturePtr texture;
};

Result<void> SaveTextureResource(const std::filesystem::path& path, ResourceId id, const MaterialTexture& texture);

Result<LoadedTextureResource> LoadTextureResource(const std::filesystem::path& path);
} // namespace Ursa
