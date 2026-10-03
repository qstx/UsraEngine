#pragma once
#include <core/Containers.hpp>
#include <serialization/SceneResource.hpp>
#include <map>

namespace ursa {
struct LinkedSceneResource {
    ResourceId id;
    std::unique_ptr<Scene> scene;
    nlohmann::json document;
    nlohmann::json snapshot;
    Map<String, Map<String, String>> scopedIds;
    ResourceSession *resources = nullptr;
};

Result<void> savePrefabResource(const std::filesystem::path &path, ResourceId id, const Scene &scene, NodeId root,
                                const ComponentRegistry &registry, const MaterialIdResolver &materialId = {});

Result<LoadedSceneResource> loadPrefabResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                               ComponentLoadContext &context);

Result<void> addPrefabInstance(nlohmann::json &sceneDocument, ResourceId prefabId, ResourceId instanceId,
                               ResourceId parentId = {});

Result<LinkedSceneResource> loadLinkedSceneResource(const std::filesystem::path &path,
                                                    const ComponentRegistry &registry, ComponentLoadContext &context);

Result<LinkedSceneResource> decodeLinkedSceneDocument(const std::filesystem::path &path, nlohmann::json document,
                                                      const ComponentRegistry &registry, ComponentLoadContext &context);

Result<void> saveLinkedSceneResource(const std::filesystem::path &path, LinkedSceneResource &linked,
                                     const ComponentRegistry &registry, const MaterialIdResolver &materialId = {});

Result<void> reloadLinkedSceneResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                       ComponentLoadContext &context, LinkedSceneResource &active);

Result<void> setPrefabPropertyOverride(LinkedSceneResource &linked, ResourceId instanceId, ResourceId objectId,
                                       const String &pointer, const nlohmann::json &value);

Result<void> clearPrefabPropertyOverride(LinkedSceneResource &linked, ResourceId instanceId, ResourceId objectId,
                                         const String &pointer);

Result<void> setPrefabStructureOverride(LinkedSceneResource &linked, ResourceId instanceId, const String &operation,
                                        const nlohmann::json &value);
} // namespace ursa
