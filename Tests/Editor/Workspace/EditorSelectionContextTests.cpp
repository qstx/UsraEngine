#include <Workspace/EditorSelectionContext.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		const ResourceId document = ResourceId::Generate();
		const ResourceId first = ResourceId::Generate();
		const ResourceId second = ResourceId::Generate();
		EditorSelectionEntry firstEntry;
		firstEntry.kind = EditorSelectionKind::GameObject;
		firstEntry.documentId = document;
		firstEntry.objectId = first;
		EditorSelectionEntry secondEntry = firstEntry;
		secondEntry.objectId = second;

		EditorSelectionContext selection;
		selection.ReconcileEntries({});
		selection.ReconcileEntries({});
		Expect(selection.revision == 0 && !selection.rangeAnchor,
		       "Refreshing an empty selection changed its revision without a user selection");
		PendingEditorSelectionClick initialClick;
		initialClick.Begin(firstEntry, {firstEntry, secondEntry}, false, false, selection.revision);
		selection.ReconcileEntries({});
		initialClick.Finish(selection, true);
		Expect(selection.objectId == first, "An idle refresh cancelled the first hierarchy click");
		selection = {};
		Expect(selection.SelectEntries({firstEntry, secondEntry, firstEntry}, 2), "Cannot publish selection set");
		Expect(selection.entries.size() == 2 && selection.primaryIndex == 0,
		       "Selection did not deduplicate stable object identities or keep the requested primary");
		Expect(selection.kind == EditorSelectionKind::GameObject && selection.documentId == document &&
		           selection.objectId == first,
		       "Legacy single-selection fields do not reflect the primary entry");
		Expect(selection.revision == 1 && selection.rangeAnchor && selection.rangeAnchor->objectId == first,
		       "Selection revision or default range anchor was not initialized");

		Expect(selection.RemoveEntry(firstEntry), "Cannot remove the primary selection");
		Expect(selection.entries.size() == 1 && selection.primaryIndex == 0 && selection.objectId == second,
		       "Removing the primary did not promote the last remaining entry");
		Expect(selection.revision == 2, "Removing an entry did not advance the selection revision");

		EditorSelectionEntry thirdEntry = secondEntry;
		thirdEntry.objectId = ResourceId::Generate();
		const Vector<EditorSelectionEntry> hierarchyRows{firstEntry, secondEntry, thirdEntry};
		Expect(selection.SelectClicked(firstEntry, hierarchyRows, false, false) &&
		           selection.SelectClicked(thirdEntry, hierarchyRows, true, false) && selection.entries.size() == 2,
		       "Control-click did not extend the ordered selection");
		Expect(selection.SelectClicked(secondEntry, hierarchyRows, false, true) && selection.entries.size() == 2 &&
		           selection.entries[0].objectId == second && selection.entries[1].objectId == thirdEntry.objectId,
		       "Shift-click did not select the visible range from the stable anchor");
		const Vector<EditorSelectionEntry> filteredRows{firstEntry, thirdEntry};
		Expect(selection.SelectClicked(firstEntry, filteredRows, false, true) && selection.entries.size() == 2 &&
		           selection.entries[0].objectId == first && selection.entries[1].objectId == thirdEntry.objectId,
		       "Shift range included a row hidden by a filter or collapsed branch");

		PendingEditorSelectionClick pendingClick;
		pendingClick.Begin(secondEntry, hierarchyRows, false, false, selection.revision);
		pendingClick.ObserveDrag(true);
		pendingClick.Finish(selection, true);
		Expect(selection.entries.size() == 2 && selection.entries[0].objectId == first &&
		           selection.entries[1].objectId == thirdEntry.objectId,
		       "Dragging a row published its deferred click selection");
		pendingClick.Begin(secondEntry, hierarchyRows, false, false, selection.revision);
		pendingClick.Finish(selection, true);
		Expect(selection.entries.size() == 1 && selection.objectId == second,
		       "A completed row click did not publish its deferred selection");

		const auto beforeRejectedSelection = selection;
		selection.beforeChange = [](const EditorSelectionContext&) { return false; };
		Expect(!selection.SelectGameObject(document, first), "Selection gate did not reject a proposal");
		Expect(selection.objectId == beforeRejectedSelection.objectId &&
		           selection.entries.size() == beforeRejectedSelection.entries.size() &&
		           selection.entries[0].SameIdentity(beforeRejectedSelection.entries[0]) &&
		           selection.revision == beforeRejectedSelection.revision,
		       "Rejected selection changed the published set or revision");

		EditorSelectionContext legacy;
		Expect(legacy.SelectGameObject(document, first) && legacy.kind == EditorSelectionKind::GameObject &&
		           legacy.objectId == first && legacy.entries.size() == 1,
		       "Legacy single-object entry point did not publish a compatible one-entry set");
		Expect(legacy.Clear() && legacy.kind == EditorSelectionKind::None && legacy.entries.empty(),
		       "Legacy clear did not clear the shared selection");

		EditorSelectionEntry editEntry = firstEntry;
		EditorSelectionEntry playEntry = editEntry;
		playEntry.playSession = true;
		Expect(!editEntry.SameIdentity(playEntry), "Play and Edit selections shared one stable target identity");
		EditorSelectionContext playSelection;
		Expect(playSelection.SelectEntries({editEntry}, 0), "Cannot seed Edit-mode selection identity");
		playSelection.ReconcileEntries({playEntry});
		Expect(playSelection.PrimaryEntry()->playSession && playSelection.revision == 2,
		       "Entering Play did not invalidate and rebind the active selection identity");

		selection.beforeChange = {};
		{
			EditorSelectionContext missingSelection;
			auto missingEntry = firstEntry;
			missingEntry.missing = true;
			missingSelection.ReconcileEntries({missingEntry});
			const auto missingRevision = missingSelection.revision;
			missingSelection.ReconcileEntries({missingEntry});
			Expect(missingSelection.revision == missingRevision && !missingSelection.rangeAnchor,
			       "An unchanged missing selection kept advancing its revision");
		}
		EditorSelectionEntry assetEntry;
		assetEntry.kind = EditorSelectionKind::Asset;
		assetEntry.assetPath = VirtualPath::Parse("prj:Texture.png").value;
		assetEntry.assetGuid = ResourceId::Generate();
		Expect(selection.SelectClicked(assetEntry, {assetEntry}, true, false) && selection.entries.size() == 1 &&
		           selection.kind == EditorSelectionKind::Asset,
		       "Ctrl-click across Project and Hierarchy mixed two selection domains");

		std::cout << "Editor selection context test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
