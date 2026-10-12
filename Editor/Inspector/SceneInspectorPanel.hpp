#pragma once

#include <Assets/SceneDraft.hpp>

namespace Ursa::Editor
{
Result<bool> DrawSceneInspector(EditorDocument& document, EditorHierarchy& hierarchy, EditorDocuments& documents,
                                ProjectWorkspace& workspace, ComponentRegistry& registry,
                                ComponentEditors& componentEditors, String& diagnostic,
                                const Vector<ScriptTypeDescriptor>* scriptTypes = nullptr,
                                bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr,
                                bool drawInspector = true);

void DrawRuntimeSceneInspector(Scene& scene, EditorHierarchy& hierarchy, bool* inspectorOpen, String& diagnostic,
                               ProjectWorkspace* workspace = nullptr, ResourceSession* resources = nullptr);
} // namespace Ursa::Editor
