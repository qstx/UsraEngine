#include <core/StringInterop.hpp>
#include <core/Log.hpp>
#include <serialization/ResourceSession.hpp>
#include <serialization/MaterialResource.hpp>
#include <serialization/TextureResource.hpp>

namespace ursa {
Result<std::unique_ptr<ResourceSession>> ResourceSession::open(const Vector<ResourceMount> &mounts) {
    auto session = std::unique_ptr<ResourceSession>(new ResourceSession);
    for (const ResourceMount &mount : mounts) {
        auto added = session->index_.mount(mount);
        if (!added)
            return Result<std::unique_ptr<ResourceSession>>::failure(added.error);
    }
    auto scanned = session->index_.rebuild();
    if (!scanned)
        return Result<std::unique_ptr<ResourceSession>>::failure(scanned.error);
    return Result<std::unique_ptr<ResourceSession>>::success(std::move(session));
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::open(const std::filesystem::path &root, MountAccess access) {
    Vector<ResourceMount> mounts;
    mounts.push_back(ResourceMount{projectScheme, root, access});
    return open(mounts);
}

Result<std::unique_ptr<ResourceSession>> ResourceSession::refreshed() const {
    return open(index_.mounts());
}

Result<MaterialTexturePtr> ResourceSession::loadTexture(ResourceId id) {
    const auto key = id.toString();
    auto cached = textures_.find(key);
    if (cached != textures_.end())
        return Result<MaterialTexturePtr>::success(cached->second);
    auto location = index_.resolve(id);
    if (!location)
        return Result<MaterialTexturePtr>::failure(location.error);
    if (location.value.kind != ResourceKind::Texture)
        return Result<MaterialTexturePtr>::failure("Resource " + key +
                                                   " is not a texture: " + location.value.path.toString());
    auto resolved = index_.locate(location.value);
    if (!resolved)
        return Result<MaterialTexturePtr>::failure(resolved.error);
    auto loaded = loadTextureResource(resolved.value);
    if (!loaded)
        return Result<MaterialTexturePtr>::failure(loaded.error);
    if (loaded.value.id != id)
        return Result<MaterialTexturePtr>::failure("Texture ID changed after indexing: " +
                                                   location.value.path.toString());
    textures_.emplace(key, loaded.value.texture);
    URSA_LOG_DEBUG("Engine", "Loaded texture " + key + " from " + location.value.path.toString());
    return Result<MaterialTexturePtr>::success(std::move(loaded.value.texture));
}

Result<std::shared_ptr<MaterialInstance>> ResourceSession::loadMaterial(ResourceId id) {
    const auto key = id.toString();
    auto cached = materials_.find(key);
    if (cached != materials_.end())
        return Result<std::shared_ptr<MaterialInstance>>::success(cached->second);
    auto location = index_.resolve(id);
    if (!location)
        return Result<std::shared_ptr<MaterialInstance>>::failure(location.error);
    if (location.value.kind != ResourceKind::Material)
        return Result<std::shared_ptr<MaterialInstance>>::failure("Resource " + key +
                                                                  " is not a material: " +
                                                                  location.value.path.toString());
    auto resolved = index_.locate(location.value);
    if (!resolved)
        return Result<std::shared_ptr<MaterialInstance>>::failure(resolved.error);
    auto loaded =
        loadMaterialResource(resolved.value, [this](ResourceId textureId) { return loadTexture(textureId); });
    if (!loaded)
        return Result<std::shared_ptr<MaterialInstance>>::failure(loaded.error);
    if (loaded.value.id != id)
        return Result<std::shared_ptr<MaterialInstance>>::failure("Material ID changed after indexing: " +
                                                                  location.value.path.toString());
    materials_.emplace(key, loaded.value.material);
    URSA_LOG_DEBUG("Engine", "Loaded material " + key + " from " + location.value.path.toString());
    return Result<std::shared_ptr<MaterialInstance>>::success(std::move(loaded.value.material));
}

Result<ResourceId> ResourceSession::identifyMaterial(const std::shared_ptr<MaterialInstance> &material) const {
    for (const auto &entry : materials_)
        if (entry.second == material)
            return ResourceId::parse(entry.first);
    return Result<ResourceId>::failure("Material object is not in this resource session");
}
} // namespace ursa
