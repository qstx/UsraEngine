#include <Workspace/EditorDragSession.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

EditorSelectionEntry Asset(ResourceId guid, UInt64 localFileId)
{
	EditorSelectionEntry entry;
	entry.kind = EditorSelectionKind::Asset;
	entry.assetGuid = guid;
	entry.assetLocalFileId = localFileId;
	entry.assetRole = localFileId ? EditorAssetSelectionRole::ImportedObject : EditorAssetSelectionRole::Source;
	return entry;
}

void TestSessionOrderRevisionAndExpiry()
{
	ClearEditorDragSessions();
	const auto first = Asset(ResourceId::Generate(), 0);
	const auto second = Asset(ResourceId::Generate(), 17);
	Vector<EditorSelectionEntry> sources{first, second};
	const auto payload = Take(BeginEditorDragSession(sources, 41));
	auto staleDelivery = payload;
	++staleDelivery.selectionRevision;
	Expect(!ClaimEditorDragSessionDelivery(staleDelivery), "A stale payload claimed the active drag delivery");
	Expect(ClaimEditorDragSessionDelivery(payload), "A valid drag delivery could not be claimed");
	Expect(!ClaimEditorDragSessionDelivery(payload), "A drag delivery was claimed more than once");
	const auto resolved = Take(ResolveEditorDragSession(payload, 41));
	Expect(resolved.sources.size() == 2, "The drag session lost a selected source");
	Expect(resolved.sources[0].identity.SameIdentity(first) && resolved.sources[1].identity.SameIdentity(second),
	       "The drag session changed its source order or stable identities");
	Expect(resolved.selectionRevision == 41, "The drag session lost its selection revision");
	Expect(!ResolveEditorDragSession(payload, 42), "A drag session accepted a changed source revision");
	auto stalePayload = payload;
	++stalePayload.selectionRevision;
	Expect(!ResolveEditorDragSession(stalePayload), "A payload with a forged revision resolved successfully");
	PruneEditorDragSessions();
	Expect(!ResolveEditorDragSession(payload), "An expired drag token resolved after its session ended");
	const auto explicitlyEnded = Take(BeginEditorDragSession({first}, 42));
	EndEditorDragSession(explicitlyEnded.token);
	Expect(!ResolveEditorDragSession(explicitlyEnded), "An ended drag token remained resolvable");
	Expect(!BeginEditorDragSession({}, 0), "An empty selection created a drag session");
	Expect(!BeginEditorDragSession({first, first}, 0), "A drag session retained duplicate source identities");
}

void TestSessionPayloadAndSingleItemValidation()
{
	ClearEditorDragSessions();
	const auto first = Asset(ResourceId::Generate(), 0);
	const auto second = Asset(ResourceId::Generate(), 0);
	const auto payload = Take(BeginEditorDragSession({first, second}, 9));
	ImGuiPayload preview;
	std::strncpy(preview.DataType, editorDragSessionPayloadType, sizeof(preview.DataType) - 1);
	preview.Data = const_cast<EditorDragPayload*>(&payload);
	preview.DataSize = sizeof(payload);
	preview.DataFrameCount = 0;
	preview.Preview = true;
	Expect(!ClaimEditorDragDropDelivery(&preview), "A drag preview claimed its delivery");
	ImGuiPayload delivery = preview;
	delivery.Preview = false;
	delivery.Delivery = true;
	Expect(ClaimEditorDragDropDelivery(&delivery), "A delivered ImGui payload could not claim its drag session");
	Expect(!ClaimEditorDragDropDelivery(&delivery), "The same ImGui delivery payload was claimed twice");
	const auto decoded = Take(DecodeEditorDragPayload(editorDragSessionPayloadType, &payload, sizeof(payload), 9));
	Expect(decoded.sources.size() == 2, "The ImGui session payload did not resolve the source set");
	Expect(!SingleAssetDragReference(decoded), "A single-value asset field accepted a multi-item session");
	Expect(!SingleGameObjectDragReference(decoded), "A single-value GameObject field accepted a multi-item session");
	Expect(!SingleComponentDragReference(decoded), "A single-value component field accepted a multi-item session");
	EditorSelectionEntry firstObject;
	firstObject.kind = EditorSelectionKind::GameObject;
	firstObject.documentId = ResourceId::Generate();
	firstObject.objectId = ResourceId::Generate();
	EditorSelectionEntry secondObject = firstObject;
	secondObject.objectId = ResourceId::Generate();
	const auto objectPayload = Take(BeginEditorDragSession({firstObject, secondObject}, 10));
	const auto objectSession = Take(ResolveEditorDragSession(objectPayload, 10));
	Expect(!SingleGameObjectDragReference(objectSession),
	       "A GameObject reference field accepted a multi-object session");
	EditorDragSession componentSession;
	componentSession.sources = {{firstObject, ResourceId::Generate(), "ursa.Camera"},
	                            {secondObject, ResourceId::Generate(), "ursa.Camera"}};
	Expect(!SingleComponentDragReference(componentSession),
	       "A component reference field accepted a multi-component session");
	Expect(!DecodeEditorDragPayload(editorDragSessionPayloadType, &payload, sizeof(UInt64)),
	       "A malformed drag-session payload size was accepted");
	ClearEditorDragSessions();
	Expect(!DecodeEditorDragPayload(editorDragSessionPayloadType, &payload, sizeof(payload)),
	       "A cleared session token remained resolvable");
}

void TestLegacySingleAssetCompatibility()
{
	const AssetReference legacy{ResourceId::Generate(), 73};
	const auto decoded = Take(DecodeEditorDragPayload(legacyAssetDragPayloadType, &legacy, sizeof(legacy), 5));
	Expect(decoded.legacySingleItem && decoded.sources.size() == 1,
	       "The legacy single-asset payload was not normalized to one source");
	const auto reference = Take(SingleAssetDragReference(decoded));
	Expect(reference.guid == legacy.guid && reference.localFileId == legacy.localFileId,
	       "The legacy asset GUID or local file ID changed during migration");
	Expect(!DecodeEditorDragPayload(legacyAssetDragPayloadType, &legacy, sizeof(UInt64)),
	       "A truncated legacy asset payload was accepted");
}

void TestLegacyGameObjectCompatibility()
{
	const GameObjectDragPayload legacy{ResourceId::Generate(), ResourceId::Generate()};
	const auto decoded = Take(DecodeEditorDragPayload(legacyGameObjectDragPayloadType, &legacy, sizeof(legacy)));
	const auto object = Take(SingleGameObjectDragReference(decoded));
	Expect(decoded.legacySingleItem && object.documentId == legacy.documentId && object.objectId == legacy.objectId,
	       "The legacy GameObject payload changed during session migration");
}

void TestComponentDragIdentity()
{
	ClearEditorDragSessions();
	EditorSelectionEntry owner;
	owner.kind = EditorSelectionKind::GameObject;
	owner.documentId = ResourceId::Generate();
	owner.objectId = ResourceId::Generate();
	const ResourceId componentId = ResourceId::Generate();
	const auto payload = Take(BeginEditorComponentDragSession(owner, componentId, "ursa.MeshRenderer", 12));
	const auto session = Take(ResolveEditorDragSession(payload, 12));
	const auto component = Take(SingleComponentDragReference(session));
	Expect(component.owner.documentId == owner.documentId && component.owner.objectId == owner.objectId &&
	           component.componentId == componentId && component.componentType == "ursa.MeshRenderer",
	       "The component drag did not preserve owner and component identity");
	Expect(!SingleGameObjectDragReference(session), "A component drag was mistaken for a GameObject-only drag");
}

void TestClickedSourceDoesNotChangeSelection()
{
	ClearEditorDragSessions();
	const auto primary = Asset(ResourceId::Generate(), 0);
	const auto secondary = Asset(ResourceId::Generate(), 0);
	const auto unselected = Asset(ResourceId::Generate(), 0);
	EditorSelectionContext selection;
	Expect(selection.SelectEntries({primary, secondary}, 0), "Could not establish the current Project selection");
	const auto before = selection;
	const auto selectedDrag = Take(BeginEditorDragSession(selection, primary));
	const auto selectedSources = Take(ResolveEditorDragSession(selectedDrag, before.revision));
	Expect(selectedSources.sources.size() == 2 && selectedSources.sources[0].identity.SameIdentity(primary) &&
	           selectedSources.sources[1].identity.SameIdentity(secondary),
	       "Dragging a selected Project item did not preserve the selected source set");
	const auto unselectedDrag = Take(BeginEditorDragSession(selection, unselected));
	const auto unselectedSources = Take(ResolveEditorDragSession(unselectedDrag, before.revision));
	Expect(unselectedSources.sources.size() == 1 && unselectedSources.sources.front().identity.SameIdentity(unselected),
	       "Dragging an unselected Project item incorrectly used the previous selection set");
	Expect(selection.revision == before.revision && selection.entries.size() == before.entries.size() &&
	           selection.entries[0].SameIdentity(primary) && selection.entries[1].SameIdentity(secondary),
	       "Starting a Project drag changed the Inspector selection target set");
}
} // namespace

int main()
{
	try
	{
		TestSessionOrderRevisionAndExpiry();
		TestSessionPayloadAndSingleItemValidation();
		TestLegacySingleAssetCompatibility();
		TestLegacyGameObjectCompatibility();
		TestComponentDragIdentity();
		TestClickedSourceDoesNotChangeSelection();
		std::cout << "Editor drag session identity, revision and legacy compatibility tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		ClearEditorDragSessions();
		std::cerr << error.what() << '\n';
		return 1;
	}
}
