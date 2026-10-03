#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Material.hpp>
#include <Serialization/ResourceId.hpp>
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
