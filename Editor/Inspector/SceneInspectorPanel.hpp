#pragma once

#include <Assets/SceneDraft.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Workspace/EditorSelectionContext.hpp>

namespace Ursa::Editor
{
Result<std::unique_ptr<SerializedObject>> CreateGameObjectSerializedProperties(GameObject::Ptr object);

Result<void> DeleteGameObjectSelection(Scene& scene, const Vector<ResourceId>& selectedIds, ComponentRegistry& registry,
                                       SceneDraft& draft, EditorSelectionContext* selection = nullptr);

Result<void> RecordPrefabDeletionOverrides(LinkedSceneResource& linked, const Vector<ResourceId>& deletedIds);

Result<void> AddComponentToSelection(const Vector<GameObject::Ptr>& selectedObjects, ComponentEditors& componentEditors,
                                     const String& type, const ComponentInspectorContext& baseContext = {});

Result<void> RemoveCommonComponentFromSelection(const Vector<GameObject::Ptr>& selectedObjects,
                                                const ComponentTargetGroup& group);

Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                ProjectWorkspace& workspace, ComponentRegistry& registry,
                                ComponentEditors& componentEditors, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes = nullptr,
                                bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr, bool drawInspector = true,
                                EditorSelectionContext* selection = nullptr,
                                EditorHierarchy::AssetDropAction assetDrop = {});

void DrawRuntimeSceneInspector(Scene& scene, EditorHierarchy& hierarchy, bool* inspectorOpen, String& diagnostic,
                               ProjectWorkspace* workspace = nullptr, ResourceSession* resources = nullptr,
                               EditorSelectionContext* selection = nullptr, ResourceId documentId = {});
} // namespace Ursa::Editor
