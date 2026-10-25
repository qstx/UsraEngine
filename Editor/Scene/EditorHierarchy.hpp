#pragma once

#include <Scene/Scene.hpp>
#include <Workspace/EditorCommands.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <array>
#include <functional>
#include <imgui.h>

namespace Ursa
{
class PrefabAsset;
struct LinkedSceneResource;
} // namespace Ursa

namespace Ursa::Editor
{
struct EditorDragSession;
enum class HierarchyDropZone
{
	Above,
	Into,
	Below
};

HierarchyDropZone ClassifyHierarchyDropZone(float rowY, float rowTop, float rowBottom);
Result<Vector<EditorSelectionEntry>>
NormalizeHierarchyDragSources(const EditorDragSession& session, ResourceId documentId, bool playSession,
                              const std::function<ResourceId(ResourceId)>& parentOf);
Result<void> ApplyHierarchyDrop(Scene& scene, const EditorDragSession& session, ResourceId targetId,
                                HierarchyDropZone zone, ResourceId documentId);

class EditorHierarchy final
{
public:
	using DuplicateObjectsAction = std::function<Result<void>(Scene&, const Vector<ResourceId>&, ResourceId)>;
	using DeleteObjectsAction = std::function<Result<void>(Scene&, const Vector<ResourceId>&, ResourceId)>;
	using AssetDropAction =
	    std::function<Result<void>(Scene&, const EditorDragSession&, ResourceId, HierarchyDropZone, ResourceId)>;
	Result<void> Select(const Scene& scene, GameObject::Ptr node);

	Result<void> Select(const PrefabAsset& asset, GameObject::Ptr node);

	void ClearSelection();

	Result<GameObject::Ptr> SelectedGameObject(const Scene& scene) const;

	Result<GameObject::Ptr> SelectedGameObject(const PrefabAsset& asset) const;

	ResourceId SelectedId() const
	{
		return selected;
	}

	bool IsPrefabObject(ResourceId id) const
	{
		return prefabObjects.find(id.ToString()) != prefabObjects.end();
	}

	// Returns true if a persistent scene property changed.
	Result<bool> Draw(Scene& scene, bool* open = nullptr, const LinkedSceneResource* linked = nullptr,
	                  EditorSelectionContext* selection = nullptr, ResourceId documentId = {}, bool playSession = false,
	                  UInt64 documentRevision = 0, DuplicateObjectsAction duplicateObjects = {},
	                  DeleteObjectsAction deleteObjects = {}, AssetDropAction assetDrop = {});

	Result<bool> Draw(PrefabAsset& asset, bool* open = nullptr, EditorSelectionContext* selection = nullptr,
	                  ResourceId documentId = {}, UInt64 documentRevision = 0);

	void DrawEmpty(bool* open = nullptr);

private:
	Result<bool> DrawGameObject(Scene& scene, GameObject::Ptr object, const Vector<EditorSelectionEntry>& visibleRows,
	                            EditorSelectionContext* selection, ResourceId documentId, bool playSession,
	                            UInt64 documentRevision, bool editableScene);

	ResourceId selected;
	Map<String, bool> prefabObjects;
	Map<String, String> prefabInstanceByObject;
	PendingEditorSelectionClick pendingSelectionClick;
	bool pendingClickHovered = false;
	bool pointerOverHierarchyRow = false;
	std::optional<EditorCommandContext> contextMenuTarget;
	DuplicateObjectsAction duplicateObjectsAction;
	DeleteObjectsAction deleteObjectsAction;
	AssetDropAction assetDropAction;
	Vector<ResourceId> pendingDeleteIds;
	ResourceId pendingDeletePrimary;
	String commandDiagnostic;
	ResourceId dragHoverTarget;
	double dragHoverStartedAt = 0.0;
	ResourceId renameTarget;
	std::array<char, 257> renameBuffer{};
};
} // namespace Ursa::Editor
