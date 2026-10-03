#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <material/Material.hpp>
#include <serialization/ResourceIndex.hpp>
#include <memory>

namespace ursa {
// Loads and caches resources by ID through the index's virtual file system.
class ResourceSession final {
public:
    static Result<std::unique_ptr<ResourceSession>> open(const Vector<ResourceMount> &mounts);

    // Convenience for a caller that owns a single directory of resources; the
    // directory is mounted under the conventional project scheme.
    static Result<std::unique_ptr<ResourceSession>> open(const std::filesystem::path &root,
                                                         MountAccess access = MountAccess::ReadWrite);

    Result<std::unique_ptr<ResourceSession>> refreshed() const;

    Result<MaterialTexturePtr> loadTexture(ResourceId id);

    Result<std::shared_ptr<MaterialInstance>> loadMaterial(ResourceId id);

    Result<ResourceId> identifyMaterial(const std::shared_ptr<MaterialInstance> &material) const;

    const ResourceIndex &index() const {
        return index_;
    }

private:
    ResourceIndex index_;
    Map<String, MaterialTexturePtr> textures_;
    Map<String, std::shared_ptr<MaterialInstance>> materials_;
};
} // namespace ursa
