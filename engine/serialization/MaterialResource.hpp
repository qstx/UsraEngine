#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <material/Material.hpp>
#include <serialization/ResourceId.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace ursa {
using TextureIdResolver = std::function<Result<ResourceId>(MaterialTexturePtr)>;
using TextureResourceLoader = std::function<Result<MaterialTexturePtr>(ResourceId)>;

struct LoadedMaterialResource {
    ResourceId id;
    std::shared_ptr<MaterialInstance> material;
};

Result<void> saveMaterialResource(const std::filesystem::path &path, ResourceId id, const MaterialInstance &material,
                                  const TextureIdResolver &resolveTexture);

Result<LoadedMaterialResource> loadMaterialResource(const std::filesystem::path &path,
                                                    const TextureResourceLoader &loadTexture);
} // namespace ursa
