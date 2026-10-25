#include <Assets/ProjectBrowserModel.hpp>
#include <Assets/AssetImportRules.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
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

void WriteFile(const std::filesystem::path& path, const char* contents)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file << contents;
	if (!file)
	{
		throw std::runtime_error("Cannot write Project Browser fixture asset");
	}
}

} // namespace

namespace
{
EditorSelectionEntry SourceEntry(const ProjectBrowserEntry& asset)
{
	EditorSelectionEntry entry;
	entry.kind = EditorSelectionKind::Asset;
	entry.assetPath = asset.path;
	entry.assetGuid = asset.guid;
	entry.assetRole = EditorAssetSelectionRole::Source;
	return entry;
}

void TestMoveDropPlanning(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	const auto assets = root / "Assets";
	WriteFile(assets / "A" / "Same.txt", "A");
	WriteFile(assets / "A" / "Already.txt", "A target conflict");
	WriteFile(assets / "B" / "Same.txt", "B");
	WriteFile(assets / "B" / "Already.txt", "B source conflict");
	WriteFile(assets / "Container" / "Child.txt", "child");
	auto workspace = ProjectWorkspace::Open(root);
	Expect(bool(workspace), "Cannot open Move drop planning workspace");
	Expect(bool(workspace.value->Database()->CreateFolder(VirtualPath::Parse("prj:Destination").value)) &&
	           bool(workspace.value->Database()->CreateFolder(VirtualPath::Parse("prj:Container/Nested").value)) &&
	           bool(workspace.value->Database()->CreateFolder(VirtualPath::Parse("prj:Destination/Nested").value)) &&
	           bool(workspace.value->Database()->Move(VirtualPath::Parse("prj:A/Already.txt").value,
	                                                  VirtualPath::Parse("prj:Destination/Already.txt").value)) &&
	           bool(workspace.value->Refresh()),
	       "Cannot prepare Project Move drop destinations");
	ProjectBrowserModel model;
	Expect(bool(model.Refresh(*workspace.value)), "Cannot refresh Move drop planning model");
	const auto* folderA = model.Find(VirtualPath::Parse("prj:A").value);
	const auto* fileA = model.Find(VirtualPath::Parse("prj:A/Same.txt").value);
	const auto* fileB = model.Find(VirtualPath::Parse("prj:B/Same.txt").value);
	const auto* container = model.Find(VirtualPath::Parse("prj:Container").value);
	const auto* child = model.Find(VirtualPath::Parse("prj:Container/Child.txt").value);
	Expect(folderA && fileA && fileB && container && child, "Move drop fixture entries are missing");
	const ResourceId childGuid = child->guid;
	Expect(!PlanProjectSourceDelete(*workspace.value->Database(), {}, false),
	       "Project Delete accepted an empty selection");
	Expect(!PlanProjectSourceDelete(*workspace.value->Database(), {SourceEntry(*fileA)}, true),
	       "Read-only Project accepted source deletion");
	EditorSelectionEntry importedDelete = SourceEntry(*fileA);
	importedDelete.assetRole = EditorAssetSelectionRole::ImportedObject;
	importedDelete.assetLocalFileId = 7;
	Expect(!PlanProjectSourceDelete(*workspace.value->Database(), {importedDelete}, false),
	       "Project Delete accepted an imported sub-asset");
	EditorSelectionEntry staleDelete = SourceEntry(*fileA);
	staleDelete.assetPath = VirtualPath::Parse("prj:B/Same.txt").value;
	Expect(!PlanProjectSourceDelete(*workspace.value->Database(), {staleDelete}, false),
	       "Project Delete accepted a stale GUID/path pair");
	auto deleteParentAndChild =
	    PlanProjectSourceDelete(*workspace.value->Database(), {SourceEntry(*fileA), SourceEntry(*folderA)}, false);
	Expect(deleteParentAndChild && deleteParentAndChild.value.size() == 1 &&
	           deleteParentAndChild.value.front() == folderA->path,
	       "Project Delete did not normalize selected descendants beneath a folder");
	auto independentDelete =
	    PlanProjectSourceDelete(*workspace.value->Database(), {SourceEntry(*fileA), SourceEntry(*fileB)}, false);
	Expect(independentDelete && independentDelete.value.size() == 2,
	       "Project Delete rejected independent writable sources");
	Expect(!PlanProjectSourceReimport(*workspace.value->Database(), {}, false) &&
	           !PlanProjectSourceReimport(*workspace.value->Database(), {SourceEntry(*fileA)}, true),
	       "Project Reimport accepted an empty or read-only source selection");
	EditorSelectionEntry importedReimport = SourceEntry(*fileA);
	importedReimport.assetRole = EditorAssetSelectionRole::ImportedObject;
	importedReimport.assetLocalFileId = 7;
	Expect(!PlanProjectSourceReimport(*workspace.value->Database(), {importedReimport}, false),
	       "Project Reimport accepted an imported sub-asset");
	auto folderReimport =
	    PlanProjectSourceReimport(*workspace.value->Database(), {SourceEntry(*folderA), SourceEntry(*fileA)}, false);
	Expect(folderReimport && folderReimport.value.size() == 1 && folderReimport.value.front() == fileA->path,
	       "Project Reimport did not expand folders and deduplicate selected descendants");
	EditorDragSession parentAndChild;
	parentAndChild.sources = {{SourceEntry(*fileA), {}, {}}, {SourceEntry(*folderA), {}, {}}};
	auto normalized = PlanProjectFolderDrop(*workspace.value->Database(), parentAndChild, "Destination", false);
	if (!normalized)
	{
		throw std::runtime_error("Project Move plan rejected a selected parent/child group: " +
		                         ToStdString(normalized.error));
	}
	Expect(normalized && normalized.value.size() == 1 &&
	           normalized.value.front().source == VirtualPath::Parse("prj:A").value &&
	           normalized.value.front().destination == VirtualPath::Parse("prj:Destination/A").value,
	       "Project Move did not discard selected descendants while preserving the selected folder");
	Expect(!PlanProjectFolderDrop(*workspace.value->Database(),
	                              EditorDragSession{1, 0, {{SourceEntry(*container), {}, {}}}}, "Container/Nested",
	                              false),
	       "Project Move accepted a folder drop into its own descendant");
	Expect(!PlanProjectFolderDrop(*workspace.value->Database(),
	                              EditorDragSession{1, 0, {{SourceEntry(*fileA), {}, {}}}}, "Destination", true),
	       "Read-only Project accepted a Move drop");
	EditorSelectionEntry imported = SourceEntry(*fileA);
	imported.assetRole = EditorAssetSelectionRole::ImportedObject;
	imported.assetLocalFileId = 42;
	Expect(!PlanProjectFolderDrop(*workspace.value->Database(), EditorDragSession{1, 0, {{imported, {}, {}}}},
	                              "Destination", false),
	       "Project Move accepted an imported child instead of its source asset");
	EditorDragSession duplicateNames;
	duplicateNames.sources = {{SourceEntry(*fileA), {}, {}}, {SourceEntry(*fileB), {}, {}}};
	Expect(!PlanProjectFolderDrop(*workspace.value->Database(), duplicateNames, "", false),
	       "Project Move accepted colliding destination names from separate roots");
	EditorDragSession existingTarget;
	existingTarget.sources = {{SourceEntry(*model.Find(VirtualPath::Parse("prj:B/Already.txt").value)), {}, {}}};
	Expect(!PlanProjectFolderDrop(*workspace.value->Database(), existingTarget, "Destination", false),
	       "Project Move accepted a file whose destination already exists");

	EditorSelectionContext selection;
	Expect(selection.SelectAsset(child->path, 0, {}, child->guid), "Cannot seed moved child selection");
	EditorDragSession moveContainer;
	moveContainer.sources = {{SourceEntry(*container), {}, {}}};
	auto plan = PlanProjectFolderDrop(*workspace.value->Database(), moveContainer, "Destination", false);
	Expect(plan && plan.value.size() == 1 && bool(workspace.value->Database()->MoveBatch(plan.value)) &&
	           bool(workspace.value->Refresh()) && bool(model.Refresh(*workspace.value)),
	       "Project Move batch could not move a folder from the browser plan");
	model.ReconcileSelection(selection);
	Expect(selection.assetGuid == childGuid && !selection.assetMissing &&
	           selection.assetPath == VirtualPath::Parse("prj:Destination/Container/Child.txt").value,
	       "Project Move did not restore the selected asset by GUID after moving its parent folder");
	std::filesystem::remove_all(root);
}
} // namespace

int main()
{
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaProjectBrowserModelTests-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		TestMoveDropPlanning(root / "MoveDrop");
		const auto assets = root / "Assets";
		WriteFile(assets / "Textures" / "Albedo.png", "test texture");
		auto workspace = ProjectWorkspace::Open(root);
		Expect(static_cast<bool>(workspace), "Cannot open a Project workspace for browser model");
		auto created = workspace.value->Database()->CreateFolder(VirtualPath::Parse("prj:Textures/Empty").value);
		Expect(static_cast<bool>(created), "Cannot create an empty folder for Project Browser");

		ProjectBrowserModel model;
		Expect(static_cast<bool>(model.Refresh(*workspace.value)), "Cannot build Project Browser model");
		const auto rootFolders = model.ChildFolders({});
		Expect(rootFolders.size() == 1 && rootFolders.front().name == "Textures",
		       "Root folder tree does not show the nested asset directory");
		const auto textureFolders = model.ChildFolders("Textures");
		Expect(textureFolders.size() == 1 && textureFolders.front().name == "Empty",
		       "Project Browser omitted an empty folder");
		const auto textureAssets = model.ChildAssets("Textures");
		Expect(textureAssets.size() == 1 && textureAssets.front().name == "Albedo.png",
		       "Project Browser did not show only direct files in the selected folder");
		const auto searchedAssets = model.ChildAssets({}, "ALBEDO");
		Expect(searchedAssets.size() == 1 && searchedAssets.front().path.relative == "Textures/Albedo.png",
		       "Project search lost the matching asset's relative folder path");

		model.SelectAsset(textureAssets.front().path);
		Expect(static_cast<bool>(model.NavigateTo("Textures/Empty")), "Cannot select a nested folder");
		Expect(static_cast<bool>(workspace.value->Refresh()), "Cannot refresh Project workspace");
		Expect(static_cast<bool>(model.Refresh(*workspace.value)), "Cannot refresh Project Browser model");
		Expect(model.CurrentFolder() == "Textures/Empty" && model.SelectedAsset() == textureAssets.front().path,
		       "Project Browser refresh did not preserve a valid folder and asset selection");
		auto removedFolder = workspace.value->Database()->Remove(VirtualPath::Parse("prj:Textures/Empty").value);
		Expect(static_cast<bool>(removedFolder), "Cannot remove the empty folder fixture");
		Expect(static_cast<bool>(workspace.value->Refresh()), "Cannot refresh after removing an empty folder");
		Expect(static_cast<bool>(model.Refresh(*workspace.value)) && model.CurrentFolder() == "Textures",
		       "Project Browser did not return to the nearest existing parent folder");

		const ResourceId textureGuid = textureAssets.front().guid;
		auto renamed = workspace.value->Database()->Move(textureAssets.front().path,
		                                                 VirtualPath::Parse("prj:Textures/Renamed.png").value);
		Expect(static_cast<bool>(renamed), "Cannot rename Project asset through the AssetDatabase");
		Expect(static_cast<bool>(workspace.value->Refresh()), "Cannot refresh renamed Project asset");
		Expect(static_cast<bool>(model.Refresh(*workspace.value)), "Cannot refresh after Project asset rename");
		const auto renamedAssets = model.ChildAssets("Textures");
		Expect(renamedAssets.size() == 1 && renamedAssets.front().name == "Renamed.png" &&
		           renamedAssets.front().guid == textureGuid,
		       "Project rename did not preserve the asset GUID in the browser");
		Expect(model.CurrentFolder() == "Textures" && model.SelectedAsset() == renamedAssets.front().path,
		       "Browser did not preserve source identity after its asset was renamed");

		const auto source = renamedAssets.front().path;
		const auto mainId = StableImportObjectId("test", "main").value;
		const auto meshA = StableImportObjectId("test", "mesh-a").value;
		const auto meshB = StableImportObjectId("test", "mesh-b").value;
		const auto nodeId = StableImportObjectId("test", "node").value;
		Vector<ImportedArtifact> outputs{{mainId, "Prefab", {1}, "main", "Root"},
		                                 {meshA, "Mesh", {2}, "mesh-a", "Body"},
		                                 {meshB, "Mesh", {3}, "mesh-b", "Body"},
		                                 {nodeId, "ModelNode", {4}, "node", "Private joint", false}};
		auto* database = workspace.value->Database();
		Expect(bool(database->PublishImport(source, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, mainId)),
		       "Cannot publish browser presentation fixture");
		Expect(bool(model.Refresh(*workspace.value)), "Cannot refresh presentation fixture");
		const auto* entry = model.Find(textureGuid);
		Expect(entry && entry->VisibleChildren().size() == 2 && entry->importedObjects.size() == 4,
		       "Browser duplicated main object or displayed internal nodes");
		const auto children = entry->VisibleChildren();
		Expect(children[0].Label() != children[1].Label() && children[0].displayName == "Body",
		       "Same-name outputs are not distinguishable without changing author names");
		Expect(model.ChildAssets({}, "BODY").size() == 1 && model.ChildAssets({}, "Private joint").empty(),
		       "Browser did not search child names or leaked hidden node names");
		const auto other = VirtualPath::Parse("prj:Other.shape").value;
		WriteFile(assets / "Other.shape", "other source");
		Expect(
		    bool(database->Refresh()) &&
		        bool(database->PublishImport(other, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, mainId)) &&
		        bool(model.Refresh(*workspace.value)),
		    "Cannot prepare another source with same-named children");
		const auto sameNameSources = model.ChildAssets({}, "Body");
		Expect(sameNameSources.size() == 2 && sameNameSources[0].guid != sameNameSources[1].guid &&
		           sameNameSources[0].path != sameNameSources[1].path,
		       "Child search did not distinguish matching source paths");
		Expect(bool(database->Remove(other)) && bool(model.Refresh(*workspace.value)), "Cannot remove search fixture");
		EditorSelectionContext selection;
		selection.SelectAsset(source, meshA, "Mesh", textureGuid);
		model.ReconcileSelection(selection);
		Expect(!selection.assetMissing && selection.assetRole == EditorAssetSelectionRole::ImportedObject,
		       "Precise imported selection was not reconciled");
		const auto selectedMeshA = *selection.PrimaryEntry();
		auto selectedMeshB = selectedMeshA;
		selectedMeshB.assetLocalFileId = meshB;
		Expect(selection.SelectEntries({selectedMeshA, selectedMeshB}, 0),
		       "Cannot select multiple imported children from one source");
		selection.beforeChange = [](const auto&) { return false; };
		Expect(!selection.SelectAsset(source) && selection.assetLocalFileId == meshA,
		       "Rejected selection switched from the selected object");
		selection.beforeChange = {};
		const auto moved = VirtualPath::Parse("prj:Moved.png").value;
		Expect(bool(database->Move(source, moved)) && bool(model.Refresh(*workspace.value)),
		       "Cannot move selected source");
		model.ReconcileSelection(selection);
		Expect(!selection.assetMissing && selection.assetPath == moved && selection.assetLocalFileId == meshA,
		       "Moving the source lost the selected child identity");
		outputs.erase(outputs.begin() + 1);
		Expect(bool(database->PublishImport(moved, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, mainId)) &&
		           bool(model.Refresh(*workspace.value)),
		       "Cannot remove selected output");
		model.ReconcileSelection(selection);
		Expect(selection.entries.size() == 2 && selection.entries[0].assetMissing &&
		           selection.entries[0].assetLocalFileId == meshA && !selection.entries[1].assetMissing &&
		           selection.PrimaryEntry()->assetLocalFileId == meshB && !selection.assetMissing,
		       "Deleted child rebound by name or displaced another valid selected child");
		outputs.insert(outputs.begin() + 1, {meshA, "Mesh", {2}, "mesh-a", "Torso"});
		Expect(bool(database->PublishImport(moved, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, mainId)) &&
		           bool(model.Refresh(*workspace.value)),
		       "Cannot restore selected output");
		model.ReconcileSelection(selection);
		Expect(!selection.entries[0].assetMissing && selection.entries[0].assetLocalFileId == meshA &&
		           selection.PrimaryEntry()->assetLocalFileId == meshB,
		       "Restoring the same stable output failed to recover its original selection entry");
		outputs[1].visibleInProject = false;
		Expect(bool(database->PublishImport(moved, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, mainId)) &&
		           bool(model.Refresh(*workspace.value)),
		       "Cannot hide selected output");
		model.ReconcileSelection(selection);
		Expect(!selection.assetMissing && model.Find(textureGuid)->VisibleChildren().size() == 1,
		       "Hiding an object removed its identity or kept it in the visible list");
		std::reverse(outputs.begin(), outputs.end());
		Expect(bool(database->PublishImport(moved, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, meshB)) &&
		           bool(model.Refresh(*workspace.value)),
		       "Cannot reorder outputs or change the main object");
		model.ReconcileSelection(selection);
		Expect(!selection.assetMissing && selection.assetLocalFileId == meshB &&
		           database->ImportedObject(textureGuid).value.localFileId == meshB,
		       "Main-object change or directory order rebound a concrete selection");
		const auto choices = AssetObjectChoices(*workspace.value);
		const auto textureChoices = QueryAssetObjectChoices(*workspace.value, [&](const ImportedArtifactRecord& object)
		                                                    { return object.guid == textureGuid; });
		Expect(std::count_if(choices.begin(), choices.end(), [&](const auto& object)
		                     { return object.guid == textureGuid && object.isMainObject; }) == 1 &&
		           std::none_of(choices.begin(), choices.end(),
		                        [&](const auto& object)
		                        {
			                        return object.guid == textureGuid &&
			                               (object.localFileId == meshA || object.localFileId == nodeId);
		                        }),
		       "Chooser duplicated the main alias or included hidden outputs");
		Expect(std::count_if(textureChoices.begin(), textureChoices.end(), [&](const auto& object)
		                     { return object.guid == textureGuid && object.isMainObject; }) == 1 &&
		           std::none_of(textureChoices.begin(), textureChoices.end(), [&](const auto& object)
		                        { return object.guid == textureGuid && object.localFileId == meshA; }),
		       "Filtered reference query did not preserve primary alias de-duplication");
		const auto parent = VirtualPath::Parse("prj:Container").value;
		const auto nested = VirtualPath::Parse("prj:Container/Moved.png").value;
		Expect(bool(database->CreateFolder(parent)) && bool(database->Move(moved, nested)) &&
		           bool(database->Move(parent, VirtualPath::Parse("prj:RenamedContainer").value)) &&
		           bool(model.Refresh(*workspace.value)),
		       "Cannot move a selected source's parent folder");
		model.ReconcileSelection(selection);
		Expect(selection.assetPath == VirtualPath::Parse("prj:RenamedContainer/Moved.png").value &&
		           !selection.assetMissing,
		       "Parent-folder move lost the selected source GUID");
		Expect(bool(database->Remove(selection.assetPath)) && bool(model.Refresh(*workspace.value)),
		       "Cannot delete selected source");
		model.ReconcileSelection(selection);
		Expect(selection.assetMissing && selection.assetGuid == textureGuid && selection.assetLocalFileId == meshB &&
		           selection.entries.size() == 2 && selection.entries[0].assetMissing &&
		           selection.entries[1].assetMissing,
		       "Deleting a source discarded its missing exact selection");
		selection.SelectGameObject(ResourceId::Generate(), ResourceId::Generate());
		Expect(!selection.assetGuid && !selection.assetMissing && selection.kind == EditorSelectionKind::GameObject,
		       "GameObject selection retained a missing asset identity");

		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << " [fixture " << root.string() << "]" << std::endl;
		std::filesystem::remove_all(root);
		return 1;
	}
}
