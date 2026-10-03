#pragma once
#include <Core/Containers.hpp>
#include <Serialization/SceneResource.hpp>
#include <map>

namespace Ursa
{
struct LinkedSceneResource
{
	ResourceId id;
	std::unique_ptr<Scene> scene;
	nlohmann::json document;
	nlohmann::json snapshot;
	Map<String, Map<String, String>> scopedIds;
	ResourceSession* resources = nullptr;
};

Result<void> SavePrefabResource(const std::filesystem::path& path, ResourceId id, const Scene& scene, NodeId root,
                                const ComponentRegistry& registry, const MaterialIdResolver& materialId = {});

Result<LoadedSceneResource> LoadPrefabResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                               ComponentLoadContext& context);

Result<void> AddPrefabInstance(nlohmann::json& sceneDocument, ResourceId prefabId, ResourceId instanceId,
                               ResourceId parentId = {});

Result<LinkedSceneResource> LoadLinkedSceneResource(const std::filesystem::path& path,
                                                    const ComponentRegistry& registry, ComponentLoadContext& context);

Result<LinkedSceneResource> DecodeLinkedSceneDocument(const std::filesystem::path& path, nlohmann::json document,
                                                      const ComponentRegistry& registry, ComponentLoadContext& context);

Result<void> SaveLinkedSceneResource(const std::filesystem::path& path, LinkedSceneResource& linked,
                                     const ComponentRegistry& registry, const MaterialIdResolver& materialId = {});

Result<void> ReloadLinkedSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                       ComponentLoadContext& context, LinkedSceneResource& active);

Result<void> SetPrefabPropertyOverride(LinkedSceneResource& linked, ResourceId instanceId, ResourceId objectId,
                                       const String& pointer, const nlohmann::json& value);

Result<void> ClearPrefabPropertyOverride(LinkedSceneResource& linked, ResourceId instanceId, ResourceId objectId,
                                         const String& pointer);

Result<void> SetPrefabStructureOverride(LinkedSceneResource& linked, ResourceId instanceId, const String& operation,
                                        const nlohmann::json& value);
} // namespace Ursa
