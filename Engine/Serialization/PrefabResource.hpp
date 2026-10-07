#pragma once
#include <Vfs/VirtualFileSystem.hpp>
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabAsset.hpp>

namespace Ursa
{
constexpr Ursa::UInt32 prefabGraphFormatVersion = 2;

struct LinkedSceneResource
{
	ResourceId id;
	std::unique_ptr<Scene> scene;
	nlohmann::json document;
	nlohmann::json snapshot;
	Map<String, Map<String, String>> scopedIds;
	ResourceSession* resources = nullptr;
};

Result<void> AddPrefabInstance(nlohmann::json& sceneDocument, ResourceId prefabId, ResourceId instanceId,
                               ResourceId parentId = {});

Result<LinkedSceneResource> DecodeLinkedSceneDocument(const String& path, nlohmann::json document,
                                                      const ComponentRegistry& registry, ComponentLoadContext& context);

Result<void> SetPrefabPropertyOverride(LinkedSceneResource& linked, ResourceId instanceId, ResourceId objectId,
                                       const String& pointer, const nlohmann::json& value);

Result<void> ClearPrefabPropertyOverride(LinkedSceneResource& linked, ResourceId instanceId, ResourceId objectId,
                                         const String& pointer);

Result<void> SetPrefabStructureOverride(LinkedSceneResource& linked, ResourceId instanceId, const String& operation,
                                        const nlohmann::json& value);
Result<void> SavePrefabResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                const Scene& scene, GameObject::Ptr root, const ComponentRegistry& registry,
                                const MaterialIdResolver& materialId = {});

Result<LoadedSceneResource> LoadPrefabResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                               const ComponentRegistry& registry, ComponentLoadContext& context);

Result<LinkedSceneResource> LoadLinkedSceneResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                    const ComponentRegistry& registry, ComponentLoadContext& context);

Result<void> SaveLinkedSceneResource(VirtualFileSystem& filesystem, const VirtualPath& path,
                                     LinkedSceneResource& linked, const ComponentRegistry& registry,
                                     const MaterialIdResolver& materialId = {});

Result<void> ReloadLinkedSceneResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                       const ComponentRegistry& registry, ComponentLoadContext& context,
                                       LinkedSceneResource& active);

} // namespace Ursa
