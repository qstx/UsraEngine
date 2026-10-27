#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/AssetReference.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <imgui.h>
#include <optional>

namespace Ursa::Editor
{
inline constexpr const char* editorDragSessionPayloadType = "URSA_EDITOR_DRAG_SESSION";
inline constexpr const char* legacyAssetDragPayloadType = "URSA_ASSET_REFERENCE";
inline constexpr const char* legacyGameObjectDragPayloadType = "URSA_GAME_OBJECT_REFERENCE";

struct EditorDragPayload
{
	UInt64 token = 0;
	UInt64 selectionRevision = 0;
};

struct GameObjectDragPayload
{
	ResourceId documentId;
	ResourceId objectId;
};

struct EditorDragSource
{
	EditorSelectionEntry identity;
	ResourceId componentId;
	String componentType;
};

struct EditorDragSession
{
	UInt64 token = 0;
	UInt64 selectionRevision = 0;
	Vector<EditorDragSource> sources;
	bool legacySingleItem = false;
	bool deliveryClaimed = false;
};

struct ComponentDragReference
{
	GameObjectDragPayload owner;
	ResourceId componentId;
	String componentType;
};

Result<EditorDragPayload> BeginEditorDragSession(const Vector<EditorSelectionEntry>& sources, UInt64 selectionRevision);
Result<EditorDragPayload> BeginEditorDragSession(const EditorSelectionContext& selection,
                                                 const EditorSelectionEntry& clicked, bool includeSelection);
Result<EditorDragPayload> BeginEditorComponentDragSession(const EditorSelectionEntry& owner, ResourceId componentId,
                                                          String componentType, UInt64 selectionRevision);
Result<EditorDragSession> ResolveEditorDragSession(const EditorDragPayload& payload,
                                                   std::optional<UInt64> expectedRevision = std::nullopt);
Result<EditorDragSession> DecodeEditorDragPayload(const char* type, const void* data, Size dataSize,
                                                  std::optional<UInt64> expectedRevision = std::nullopt);
bool ClaimEditorDragSessionDelivery(const EditorDragPayload& payload);
bool ClaimEditorDragDropDelivery(const ImGuiPayload* payload);
Result<AssetReference> SingleAssetDragReference(const EditorDragSession& session);
Result<GameObjectDragPayload> SingleGameObjectDragReference(const EditorDragSession& session);
Result<ComponentDragReference> SingleComponentDragReference(const EditorDragSession& session);
const ImGuiPayload* AcceptEditorDragDropPayload();
void EndEditorDragSession(UInt64 token);
void ClearEditorDragSessions();
void PruneEditorDragSessions();
} // namespace Ursa::Editor
