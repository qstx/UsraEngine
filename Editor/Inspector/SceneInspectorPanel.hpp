#pragma once

#include <Assets/SceneDraft.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <Workspace/ProjectTagLayerSettings.hpp>
#include <functional>

namespace Ursa
{
class PrefabAsset;
}

namespace Ursa::Editor
{
class EditorDocuments;
using ManagedPropertyGroupCommit = std::function<Result<void>(SerializedObjectGroup&, const String&)>;
using ManagedTransformInspector =
    std::function<Result<bool>(const Vector<GameObject::Ptr>&, Size, SerializedObject*, SerializedObjectGroup&, Scene*,
                               ComponentLoadContext*, ManagedPropertyGroupCommit)>;
using ManagedTransformActionsInspector =
    std::function<Result<bool>(const Vector<GameObject::Ptr>&, Size, SerializedObject*, SerializedObjectGroup&, Scene*,
                               ComponentLoadContext*, ManagedPropertyGroupCommit)>;

Result<std::unique_ptr<SerializedObject>> CreateGameObjectSerializedProperties(GameObject::Ptr object);

Result<void> DeleteGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds, ComponentRegistry& registry,
                                       SceneDraft& draft, EditorSelectionContext* selection = nullptr);

Result<void> DuplicateGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds, ResourceId primaryId,
                                          ComponentRegistry& registry, EditorDocuments& documents, SceneDraft& draft,
                                          EditorSelectionContext* selection = nullptr, ResourceId parentOverride = {},
                                          const PrefabAsset* prefabSnapshot = nullptr,
                                          const Vector<Mat4>* sourceWorldTransforms = nullptr);
Result<std::shared_ptr<PrefabAsset>> CaptureGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds,
                                                                ResourceId primaryId, ComponentRegistry& registry,
                                                                EditorDocuments& documents, SceneDraft& draft,
                                                                Vector<Mat4>* sourceWorldTransforms = nullptr);

Result<void> RecordPrefabDeletionOverrides(LinkedSceneResource& linked, const Vector<ResourceId>& deletedIds);

Result<void> AddComponentToSelection(const Vector<GameObject::Ptr>& selectedObjects, ComponentEditors& componentEditors,
                                     const String& type, const ComponentInspectorContext& baseContext = {});

Result<void> RemoveCommonComponentFromSelection(const Vector<GameObject::Ptr>& selectedObjects,
                                                const ComponentTargetGroup& group);

Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                ProjectWorkspace& workspace, const ProjectTagLayerSettings& tagLayerSettings,
                                ComponentRegistry& registry, ComponentEditors& componentEditors, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes = nullptr,
                                bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr, bool drawInspector = true,
                                EditorSelectionContext* selection = nullptr,
                                EditorHierarchy::AssetDropAction assetDrop = {},
                                std::function<void(bool)> openTagLayerSettings = {},
                                bool canCopyGameObjects = false, bool canPasteGameObjectsAsChild = false,
                                EditorHierarchy::CopySelectionAction copyGameObjects = {},
                                EditorHierarchy::CutSelectionAction cutGameObjects = {},
                                EditorHierarchy::PasteAsChildAction pasteGameObjectsAsChild = {},
                                ManagedTransformInspector managedTransformInspector = {},
                                ManagedTransformActionsInspector managedTransformActionsInspector = {});

void DrawRuntimeSceneInspector(Scene& scene, EditorHierarchy& hierarchy, bool* inspectorOpen, String& diagnostic,
                               ProjectWorkspace* workspace = nullptr, ResourceSession* resources = nullptr,
                               EditorSelectionContext* selection = nullptr, ResourceId documentId = {},
                               ComponentEditors* componentEditors = nullptr);
} // namespace Ursa::Editor
