#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <material/Material.hpp>
#include <serialization/ResourceId.hpp>
#include <filesystem>

namespace ursa {
struct LoadedTextureResource {
    ResourceId id;
    MaterialTexturePtr texture;
};

Result<void> saveTextureResource(const std::filesystem::path &path, ResourceId id, const MaterialTexture &texture);

Result<LoadedTextureResource> loadTextureResource(const std::filesystem::path &path);
} // namespace ursa
