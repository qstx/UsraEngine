#pragma once

#include <Scene/Scene.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <Workspace/EditorCommands.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <utility>
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
Result<void> SetSceneGameObjectWorldTransform(Scene& scene, const GameObject::Ptr& object, const Mat4& worldTransform);

class EditorHierarchy final
{
public:
	using DuplicateObjectsAction = std::function<Result<void>(Scene&, const Vector<ResourceId>&, ResourceId)>;
	using DeleteObjectsAction = std::function<Result<void>(Scene&, const Vector<ResourceId>&, ResourceId)>;
	using AssetDropAction =
	    std::function<Result<void>(Scene&, const EditorDragSession&, ResourceId, HierarchyDropZone, ResourceId)>;
	using CopySelectionAction = std::function<Result<void>()>;
	using CutSelectionAction = std::function<Result<void>()>;
	using PasteAsChildAction = std::function<Result<void>()>;
	Result<void> Select(const Scene& scene, GameObject::Ptr node);
	Result<void> SelectChildren(const Scene& scene, EditorSelectionContext& selection, ResourceId documentId,
                            bool playSession);
	Result<void> SelectChildren(const PrefabAsset& asset, EditorSelectionContext& selection,
	                            ResourceId documentId);
	Result<void> InvertSelection(const Scene& scene, EditorSelectionContext& selection, ResourceId documentId,
	                             bool playSession);
	Result<void> InvertSelection(const PrefabAsset& asset, EditorSelectionContext& selection, ResourceId documentId);

	Result<void> Select(const PrefabAsset& asset, GameObject::Ptr node);

	void ClearSelection();

	bool SceneObjectVisible(ResourceId id) const;
	bool SceneObjectPickable(ResourceId id) const;
	void SetDefaultParent(ResourceId documentId, ResourceId objectId);
	void ClearDefaultParent();
	ResourceId DefaultParent(const Scene& scene, ResourceId documentId) const;
	void ToggleSceneObjectVisibility(Scene& scene, const GameObject::Ptr& object, bool onlyThisObject);
	void ToggleSceneObjectsVisibility(Scene& scene, const Vector<GameObject::Ptr>& objects, bool onlyThisObject = false);
	void ToggleSceneObjectPickability(Scene& scene, const GameObject::Ptr& object, bool onlyThisObject);
	void ToggleSceneObjectsPickability(Scene& scene, const Vector<GameObject::Ptr>& objects, bool onlyThisObject = false);
	Vector<ResourceId> SceneVisibilityExclusions(const Scene& scene) const;
	String SerializeSceneViewOverrides(const Scene& scene) const;
	Result<void> RestoreSceneViewOverrides(Scene& scene, const String& text);

	UInt64 SceneViewOverridesRevision() const
	{
		return sceneViewOverridesRevision;
	}

	Result<GameObject::Ptr> SelectedGameObject(const Scene& scene) const;

	Result<GameObject::Ptr> SelectedGameObject(const PrefabAsset& asset) const;

	ResourceId SelectedId() const
	{
		return selected;
	}

	std::array<char, 257>& SearchText()
	{
		return searchText;
	}

	const std::array<char, 257>& SearchText() const
	{
		return searchText;
	}

	bool IsDirectSearchMatch(ResourceId objectId) const
	{
		const auto match = directSearchMatches.find(objectId.ToString());
		return match != directSearchMatches.end() && match->second;
	}

	bool HasDirectSearchMatches() const
	{
		return std::any_of(directSearchMatches.begin(), directSearchMatches.end(),
		                   [](const auto& entry) { return entry.second; });
	}

	void RefreshSceneSearchMatches(const Scene& scene, ResourceId documentId);

	bool ConsumeFocusSelectionRequest()
	{
		return std::exchange(focusSelectionRequested, false);
	}

	const Vector<EditorSelectionEntry>& VisibleSelectionRows() const
	{
		return visibleSelectionRows;
	}

	bool IsPrefabObject(ResourceId id) const
	{
		return prefabObjects.find(id.ToString()) != prefabObjects.end();
	}

	// Returns true if a persistent scene property changed.
	Result<bool> Draw(Scene& scene, bool* open = nullptr, const LinkedSceneResource* linked = nullptr,
	                  EditorSelectionContext* selection = nullptr, ResourceId documentId = {}, bool playSession = false,
	                  UInt64 documentRevision = 0, DuplicateObjectsAction duplicateObjects = {},
	                  DeleteObjectsAction deleteObjects = {}, AssetDropAction assetDrop = {}, String sceneName = {},
	                 VirtualFileSystem* userFiles = nullptr, bool copySelectionAvailable = false,
	                  bool pasteAsChildAvailable = false, CopySelectionAction copyAction = {},
	                  CutSelectionAction cutAction = {}, PasteAsChildAction pasteAction = {});

	Result<bool> Draw(PrefabAsset& asset, bool* open = nullptr, EditorSelectionContext* selection = nullptr,
	                  ResourceId documentId = {}, UInt64 documentRevision = 0);

	void DrawEmpty(bool* open = nullptr);

private:
	Result<bool> DrawGameObject(Scene& scene, GameObject::Ptr object, const Vector<EditorSelectionEntry>& visibleRows,
	                            EditorSelectionContext* selection, ResourceId documentId, bool playSession,
	                            UInt64 documentRevision, bool editableScene, const Map<String, bool>& searchMatches,
	                            bool filtering, bool expandFilteredBranches);

	ResourceId selected;
	ResourceId defaultParent;
	ResourceId defaultParentDocument;
	Vector<ImGuiID> hierarchyRowIds;
	Vector<EditorSelectionEntry> visibleSelectionRows;
	ResourceId keyboardFocusTarget;
	bool focusSelectionRequested = false;
	Vector<std::pair<ResourceId, bool>> sceneVisibilityOverrides;
	Vector<std::pair<ResourceId, bool>> scenePickabilityOverrides;
	UInt64 sceneViewOverridesRevision = 0;
	Map<String, bool> prefabObjects;
	Map<String, String> prefabInstanceByObject;
	PendingEditorSelectionClick pendingSelectionClick;
	bool pendingClickHovered = false;
	bool pointerOverHierarchyRow = false;
	std::optional<EditorCommandContext> contextMenuTarget;
	DuplicateObjectsAction duplicateObjectsAction;
	DeleteObjectsAction deleteObjectsAction;
	AssetDropAction assetDropAction;
	CopySelectionAction copySelectionAction;
	CutSelectionAction cutSelectionAction;
	PasteAsChildAction pasteAsChildAction;
	bool canCopySelection = false;
	bool canPasteAsChild = false;
	Vector<ResourceId> pendingDeleteIds;
	ResourceId pendingDeletePrimary;
	String commandDiagnostic;
	ResourceId dragHoverTarget;
	double dragHoverStartedAt = 0.0;
	ImVec2 dragHoverPosition{};
	ResourceId renameTarget;
	bool renameFocusPending = false;
	ResourceId renameClickCandidate;
	double renameClickReleasedAt = -1.0;
	bool renameClickDragged = false;
	bool renameNewObjects = true;
	bool hierarchyAlphabeticalSort = false;
	bool hierarchySettingsLoaded = false;
	std::array<char, 257> renameBuffer{};
	std::array<char, 257> searchText{};
	Map<String, bool> directSearchMatches;
	Map<String, bool> sceneGroupOpenByDocument;
	String directSearchQuery;
	ResourceId directSearchDocument;
	String previousSearchText;
	ResourceId previousSearchDocument;
};
} // namespace Ursa::Editor
