#include <Workspace/EditorDragSession.hpp>

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <imgui.h>

namespace Ursa::Editor
{
namespace
{
std::mutex sessionsMutex;
std::map<UInt64, EditorDragSession> sessions;
std::atomic<UInt64> nextToken{1};

bool HasStableIdentity(const EditorSelectionEntry& entry)
{
	if (entry.IsMissing())
	{
		return false;
	}
	if (entry.kind == EditorSelectionKind::GameObject)
	{
		return entry.documentId && entry.objectId;
	}
	if (entry.kind == EditorSelectionKind::Asset)
	{
		return entry.assetGuid || !entry.assetPath.scheme.empty();
	}
	return false;
}

EditorSelectionEntry AssetEntry(const AssetReference& reference)
{
	EditorSelectionEntry entry;
	entry.kind = EditorSelectionKind::Asset;
	entry.assetGuid = reference.guid;
	entry.assetLocalFileId = reference.localFileId;
	entry.assetRole =
	    reference.localFileId ? EditorAssetSelectionRole::ImportedObject : EditorAssetSelectionRole::Source;
	return entry;
}

bool SameSources(const Vector<EditorDragSource>& left, const Vector<EditorDragSource>& right)
{
	return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
	                                                 [](const auto& a, const auto& b)
	                                                 {
		                                                 return a.identity.SameIdentity(b.identity) &&
		                                                        a.componentId == b.componentId &&
		                                                        a.componentType == b.componentType;
	                                                 });
}

Result<EditorDragPayload> BeginEditorDragSources(Vector<EditorDragSource> sources, UInt64 selectionRevision)
{
	if (sources.empty())
	{
		return Result<EditorDragPayload>::Failure("A drag session requires at least one source");
	}
	Vector<EditorDragSource> ordered;
	ordered.reserve(sources.size());
	for (auto& source : sources)
	{
		if (!HasStableIdentity(source.identity))
		{
			return Result<EditorDragPayload>::Failure("A drag source has no stable, valid identity");
		}
		if (static_cast<bool>(source.componentId) != !source.componentType.empty() ||
		    (source.componentId && source.identity.kind != EditorSelectionKind::GameObject))
		{
			return Result<EditorDragPayload>::Failure("A component drag requires its owner and stable component type");
		}
		if (std::any_of(ordered.begin(), ordered.end(),
		                [&](const auto& existing)
		                {
			                return existing.identity.SameIdentity(source.identity) &&
			                       existing.componentId == source.componentId;
		                }))
		{
			return Result<EditorDragPayload>::Failure("A drag session cannot contain duplicate sources");
		}
		ordered.push_back(std::move(source));
	}

	if (ImGui::GetCurrentContext())
	{
		if (const ImGuiPayload* active = ImGui::GetDragDropPayload();
		    active && active->IsDataType(editorDragSessionPayloadType) && active->DataSize == sizeof(EditorDragPayload))
		{
			EditorDragPayload payload;
			std::memcpy(&payload, active->Data, sizeof(payload));
			auto existing = ResolveEditorDragSession(payload, selectionRevision);
			if (existing && SameSources(existing.value.sources, ordered))
			{
				return Result<EditorDragPayload>::Success(payload);
			}
		}
	}

	ClearEditorDragSessions();
	UInt64 token = nextToken.fetch_add(1, std::memory_order_relaxed);
	if (!token)
	{
		token = nextToken.fetch_add(1, std::memory_order_relaxed);
	}
	EditorDragSession session{token, selectionRevision, std::move(ordered), false};
	{
		std::lock_guard lock(sessionsMutex);
		sessions[token] = std::move(session);
	}
	return Result<EditorDragPayload>::Success({token, selectionRevision});
}
} // namespace

Result<EditorDragPayload> BeginEditorDragSession(const Vector<EditorSelectionEntry>& sources, UInt64 selectionRevision)
{
	Vector<EditorDragSource> ordered;
	ordered.reserve(sources.size());
	for (const auto& source : sources)
	{
		ordered.push_back({source, {}, {}});
	}
	return BeginEditorDragSources(std::move(ordered), selectionRevision);
}

Result<EditorDragPayload> BeginEditorDragSession(const EditorSelectionContext& selection,
                                                 const EditorSelectionEntry& clicked)
{
	const bool clickedSelected = selection.Contains(clicked);
	const bool addToExistingSelection = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
	Vector<EditorSelectionEntry> sources =
	    clickedSelected || addToExistingSelection ? selection.entries : Vector<EditorSelectionEntry>{};
	if (std::none_of(sources.begin(), sources.end(),
	                 [&](const EditorSelectionEntry& entry) { return entry.SameIdentity(clicked); }))
	{
		sources.push_back(clicked);
	}
	return BeginEditorDragSession(sources, selection.revision);
}

Result<EditorDragPayload> BeginEditorComponentDragSession(const EditorSelectionEntry& owner, ResourceId componentId,
                                                          String componentType, UInt64 selectionRevision)
{
	return BeginEditorDragSources({EditorDragSource{owner, componentId, std::move(componentType)}}, selectionRevision);
}

Result<EditorDragSession> ResolveEditorDragSession(const EditorDragPayload& payload,
                                                   std::optional<UInt64> expectedRevision)
{
	if (!payload.token)
	{
		return Result<EditorDragSession>::Failure("The drag-session token is empty");
	}
	std::lock_guard lock(sessionsMutex);
	const auto found = sessions.find(payload.token);
	if (found == sessions.end())
	{
		return Result<EditorDragSession>::Failure("The drag session has expired");
	}
	if (payload.selectionRevision != found->second.selectionRevision ||
	    (expectedRevision && *expectedRevision != found->second.selectionRevision))
	{
		return Result<EditorDragSession>::Failure("The drag source selection changed during the drag");
	}
	return Result<EditorDragSession>::Success(found->second);
}

Result<EditorDragSession> DecodeEditorDragPayload(const char* type, const void* data, Size dataSize,
                                                  std::optional<UInt64> expectedRevision)
{
	if (!type || !data)
	{
		return Result<EditorDragSession>::Failure("The drag payload is empty");
	}
	if (std::strcmp(type, editorDragSessionPayloadType) == 0)
	{
		if (dataSize != sizeof(EditorDragPayload))
		{
			return Result<EditorDragSession>::Failure("The drag-session payload has an invalid size");
		}
		EditorDragPayload payload;
		std::memcpy(&payload, data, sizeof(payload));
		return ResolveEditorDragSession(payload, expectedRevision);
	}
	if (std::strcmp(type, legacyAssetDragPayloadType) == 0)
	{
		if (dataSize != sizeof(AssetReference))
		{
			return Result<EditorDragSession>::Failure("The legacy asset payload has an invalid size");
		}
		AssetReference reference;
		std::memcpy(&reference, data, sizeof(reference));
		if (!reference.guid)
		{
			return Result<EditorDragSession>::Failure("The legacy asset payload has no GUID");
		}
		const UInt64 revision = expectedRevision.value_or(0);
		return Result<EditorDragSession>::Success({0, revision, {{AssetEntry(reference), {}, {}}}, true});
	}
	if (std::strcmp(type, legacyGameObjectDragPayloadType) == 0)
	{
		if (dataSize != sizeof(GameObjectDragPayload))
		{
			return Result<EditorDragSession>::Failure("The legacy GameObject payload has an invalid size");
		}
		GameObjectDragPayload reference;
		std::memcpy(&reference, data, sizeof(reference));
		if (!reference.documentId || !reference.objectId)
		{
			return Result<EditorDragSession>::Failure("The legacy GameObject payload has no stable identity");
		}
		EditorSelectionEntry entry;
		entry.kind = EditorSelectionKind::GameObject;
		entry.documentId = reference.documentId;
		entry.objectId = reference.objectId;
		return Result<EditorDragSession>::Success({0, expectedRevision.value_or(0), {{entry, {}, {}}}, true});
	}
	return Result<EditorDragSession>::Failure("The drag payload type is not supported");
}

bool ClaimEditorDragSessionDelivery(const EditorDragPayload& payload)
{
	if (!payload.token)
	{
		return false;
	}
	std::lock_guard lock(sessionsMutex);
	const auto found = sessions.find(payload.token);
	if (found == sessions.end() || found->second.selectionRevision != payload.selectionRevision ||
	    found->second.deliveryClaimed)
	{
		return false;
	}
	found->second.deliveryClaimed = true;
	return true;
}

bool ClaimEditorDragDropDelivery(const ImGuiPayload* payload)
{
	if (!payload || !payload->IsDelivery())
	{
		return false;
	}
	if (payload->IsDataType(editorDragSessionPayloadType))
	{
		if (!payload->Data || payload->DataSize != static_cast<int>(sizeof(EditorDragPayload)))
		{
			return false;
		}
		EditorDragPayload sessionPayload;
		std::memcpy(&sessionPayload, payload->Data, sizeof(sessionPayload));
		return ClaimEditorDragSessionDelivery(sessionPayload);
	}
	return payload->IsDataType(legacyAssetDragPayloadType) || payload->IsDataType(legacyGameObjectDragPayloadType);
}

Result<AssetReference> SingleAssetDragReference(const EditorDragSession& session)
{
	if (session.sources.size() != 1 || session.sources.front().identity.kind != EditorSelectionKind::Asset)
	{
		return Result<AssetReference>::Failure("This field accepts exactly one asset reference");
	}
	const auto& entry = session.sources.front().identity;
	if (!entry.assetGuid)
	{
		return Result<AssetReference>::Failure("The dragged asset no longer has a GUID");
	}
	return Result<AssetReference>::Success({entry.assetGuid, entry.assetLocalFileId});
}

Result<GameObjectDragPayload> SingleGameObjectDragReference(const EditorDragSession& session)
{
	if (session.sources.size() != 1 || session.sources.front().identity.kind != EditorSelectionKind::GameObject ||
	    session.sources.front().componentId)
	{
		return Result<GameObjectDragPayload>::Failure("This field accepts exactly one GameObject reference");
	}
	const auto& entry = session.sources.front().identity;
	if (!entry.documentId || !entry.objectId)
	{
		return Result<GameObjectDragPayload>::Failure("The dragged GameObject no longer has a stable identity");
	}
	return Result<GameObjectDragPayload>::Success({entry.documentId, entry.objectId});
}

Result<ComponentDragReference> SingleComponentDragReference(const EditorDragSession& session)
{
	if (session.sources.size() != 1 || session.sources.front().identity.kind != EditorSelectionKind::GameObject ||
	    !session.sources.front().componentId || session.sources.front().componentType.empty())
	{
		return Result<ComponentDragReference>::Failure("This field requires one identified component source");
	}
	const auto& source = session.sources.front();
	const auto& owner = source.identity;
	return Result<ComponentDragReference>::Success(
	    {{owner.documentId, owner.objectId}, source.componentId, source.componentType});
}

const ImGuiPayload* AcceptEditorDragDropPayload()
{
	if (const auto* payload = ImGui::AcceptDragDropPayload(editorDragSessionPayloadType))
	{
		return ClaimEditorDragDropDelivery(payload) ? payload : nullptr;
	}
	if (const auto* payload = ImGui::AcceptDragDropPayload(legacyAssetDragPayloadType))
	{
		return ClaimEditorDragDropDelivery(payload) ? payload : nullptr;
	}
	const auto* payload = ImGui::AcceptDragDropPayload(legacyGameObjectDragPayloadType);
	return ClaimEditorDragDropDelivery(payload) ? payload : nullptr;
}

void EndEditorDragSession(UInt64 token)
{
	if (!token)
	{
		return;
	}
	std::lock_guard lock(sessionsMutex);
	sessions.erase(token);
}

void ClearEditorDragSessions()
{
	std::lock_guard lock(sessionsMutex);
	sessions.clear();
}

void PruneEditorDragSessions()
{
	if (!ImGui::GetCurrentContext())
	{
		ClearEditorDragSessions();
		return;
	}
	const ImGuiPayload* payload = ImGui::GetDragDropPayload();
	EditorDragPayload active{};
	if (payload && payload->IsDataType(editorDragSessionPayloadType) && payload->Data &&
	    payload->DataSize == static_cast<int>(sizeof(EditorDragPayload)))
	{
		std::memcpy(&active, payload->Data, sizeof(active));
	}
	std::lock_guard lock(sessionsMutex);
	for (auto session = sessions.begin(); session != sessions.end();)
	{
		if (!active.token || session->first != active.token ||
		    session->second.selectionRevision != active.selectionRevision)
		{
			session = sessions.erase(session);
		}
		else
		{
			++session;
		}
	}
}
} // namespace Ursa::Editor
