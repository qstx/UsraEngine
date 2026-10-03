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
	static Result<std::shared_ptr<SceneDraft>> Create(ResourceKind kind, ComponentRegistry& registry,
	                                                  ResourceSession& resources, const EditContextSetup& setup = {});

	static Result<std::shared_ptr<SceneDraft>> Load(const std::filesystem::path& path, ResourceKind kind,
	                                                ComponentRegistry& registry, ResourceSession& resources,
	                                                const EditContextSetup& setup = {});

	Result<void> Save(const std::filesystem::path& path, ResourceId id);

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

	bool Linked() const
	{
		return static_cast<bool>(linked);
	}

	LinkedSceneResource* LinkedScene()
	{
		return linked.get();
	}

	Result<void> AssignMaterial(NodeId node, ResourceId materialId, ResourceSession& resources);

	Result<void> AssignMaterial(ComponentId component, ResourceId materialId, ResourceSession& resources);

	Result<ResourceId> MaterialId(const std::shared_ptr<MaterialInstance>& material) const;

	Result<void> SetInstanceProperty(ResourceId instanceId, ResourceId sourceObjectId, const String& pointer,
	                                 const nlohmann::json& value);

	Result<void> SetInstanceStructure(ResourceId instanceId, const String& operation, const nlohmann::json& value);

	Result<ResourceId> PlacePrefab(ResourceId prefabId, NodeId parent, const std::filesystem::path& scenePath,
	                               ResourceId sceneId);

private:
	SceneDraft(ResourceKind kind, ComponentRegistry& registry, ResourceSession& resources, EditContextSetup setup)
	    : kind(kind), registry(registry), resources(resources), setup(std::move(setup))
	{
	}

	ResourceKind kind;
	ComponentRegistry& registry;
	ResourceSession& resources;
	EditContextSetup setup;
	std::unique_ptr<Scene> scene;
	std::unique_ptr<LinkedSceneResource> linked;
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
