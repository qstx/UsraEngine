#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <serialization/ComponentRegistry.hpp>
#include <filesystem>
#include <memory>

namespace ursa {
struct LoadedSceneResource {
    ResourceId id;
    std::unique_ptr<Scene> scene;
};

Result<nlohmann::json> encodeSceneDocument(const std::filesystem::path &path, ResourceId id, const Scene &scene,
                                           const ComponentRegistry &registry, const MaterialIdResolver &materialId = {},
                                           NodeId subtree = {});

Result<LoadedSceneResource> decodeSceneDocument(const std::filesystem::path &path, const nlohmann::json &document,
                                                const ComponentRegistry &registry, ComponentLoadContext &context);

Result<void> saveSceneResource(const std::filesystem::path &path, ResourceId id, const Scene &scene,
                               const ComponentRegistry &registry, const MaterialIdResolver &materialId = {});

Result<LoadedSceneResource> loadSceneResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                              ComponentLoadContext &context);

Result<void> reloadSceneResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                 ComponentLoadContext &context, std::unique_ptr<Scene> &activeScene);
} // namespace ursa
