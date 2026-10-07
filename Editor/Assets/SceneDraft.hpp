#pragma once

#include <Workspace/EditorDocuments.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Scene/EditorScenePreview.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Scene/ComponentEditors.hpp>

namespace Ursa::Editor
{
using EditContextSetup = std::function<Result<void>(ComponentLoadContext&)>;

class SceneDraft final
{
public:
	static Result<std::shared_ptr<SceneDraft>> Create(ResourceKind resourceKind, ComponentRegistry& componentRegistry,
	                                                  ResourceSession& resourceSession,
	                                                  const EditContextSetup& contextSetup = {});

	static Result<std::shared_ptr<SceneDraft>> Load(const VirtualFileSystem& filesystem, const VirtualPath& path,
	                                                ResourceKind resourceKind, ComponentRegistry& componentRegistry,
	                                                ResourceSession& resourceSession,
	                                                const EditContextSetup& contextSetup = {});

	Result<void> Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id);

	Scene& GetScene()
	{
		return linked ? *linked->scene : *scene;
	}

	const Scene& GetScene() const
	{
		return linked ? *linked->scene : *scene;
	}

	ResourceKind Kind() const
	{
		return kind;
	}

	const EditContextSetup& ContextSetup() const
	{
		return setup;
	}

	bool Linked() const
	{
		return static_cast<bool>(linked);
	}

	LinkedSceneResource* LinkedScene()
	{
		return linked.get();
	}

	PrefabAsset* GetPrefabAsset()
	{
		return prefabAsset.get();
	}

	Result<void> RefreshPrefabPreview();

	Result<GameObject::Ptr> PreviewGameObject(ResourceId prefabObjectId) const;

	Result<void> SyncPrefabTransform(ResourceId prefabObjectId);

	Result<void> AssignMaterial(GameObject::Ptr node, ResourceId materialId, ResourceSession& resourceSession);

	Result<void> AssignMaterial(ComponentId component, ResourceId materialId, ResourceSession& resourceSession);

	Result<GameObject::Ptr> InstantiateModel(ResourceId modelGuid, const nlohmann::json& modelDocument);

	Result<ResourceId> MaterialId(const std::shared_ptr<MaterialInstance>& material) const;

	Result<void> SetInstanceProperty(ResourceId instanceId, ResourceId sourceObjectId, const String& pointer,
	                                 const nlohmann::json& value);

	Result<void> SetInstanceStructure(ResourceId instanceId, const String& operation, const nlohmann::json& value);

	Result<ResourceId> PlacePrefab(ResourceId prefabId, ResourceId parentId, const VirtualPath& scenePath,
	                               ResourceId sceneId);

private:
	SceneDraft(ResourceKind resourceKind, ComponentRegistry& componentRegistry, ResourceSession& resourceSession,
	           EditContextSetup contextSetup)
	    : kind(resourceKind), registry(componentRegistry), resources(resourceSession), setup(std::move(contextSetup))
	{
	}

	ResourceKind kind;
	ComponentRegistry& registry;
	ResourceSession& resources;
	EditContextSetup setup;
	std::unique_ptr<Scene> scene;
	std::unique_ptr<LinkedSceneResource> linked;
	std::unique_ptr<PrefabAsset> prefabAsset;
	Map<String, GameObject::Ptr> previewObjects;
	Map<const MaterialInstance*, ResourceId> assignedMaterials;
};

Result<std::unique_ptr<EditorDocument>> NewSceneDocument(const VirtualPath& path, ResourceKind kind,
                                                         ComponentRegistry& registry, ResourceSession& resources,
                                                         const EditContextSetup& setup = {});

Result<std::unique_ptr<EditorDocument>> OpenSceneDocument(const VirtualPath& path, ResourceKind kind,
                                                          ComponentRegistry& registry, ResourceSession& resources,
                                                          const EditContextSetup& setup = {});

Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                const ProjectWorkspace& workspace, ComponentRegistry& registry,
                                ComponentEditors& componentEditors, String& diagnostic);
} // namespace Ursa::Editor
