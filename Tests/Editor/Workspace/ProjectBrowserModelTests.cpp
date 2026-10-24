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

int main()
{
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaProjectBrowserModelTests-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
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
		Expect(selection.assetMissing && selection.assetLocalFileId == meshA && selection.assetGuid == textureGuid,
		       "Deleted object silently rebound to its same-name neighbour");
		outputs.insert(outputs.begin() + 1, {meshA, "Mesh", {2}, "mesh-a", "Torso"});
		Expect(bool(database->PublishImport(moved, "test", 1, "cpu", nlohmann::json::object(), {}, outputs, mainId)) &&
		           bool(model.Refresh(*workspace.value)),
		       "Cannot restore selected output");
		model.ReconcileSelection(selection);
		Expect(!selection.assetMissing && selection.assetLocalFileId == meshA,
		       "Restoring the same stable output failed to recover its selection");
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
		Expect(!selection.assetMissing && selection.assetLocalFileId == meshA &&
		           database->ImportedObject(textureGuid).value.localFileId == meshB,
		       "Main-object change or directory order rebound a concrete selection");
		const auto choices = AssetObjectChoices(*workspace.value);
		Expect(std::count_if(choices.begin(), choices.end(), [&](const auto& object)
		                     { return object.guid == textureGuid && object.isMainObject; }) == 1 &&
		           std::none_of(choices.begin(), choices.end(),
		                        [&](const auto& object)
		                        {
			                        return object.guid == textureGuid &&
			                               (object.localFileId == meshA || object.localFileId == nodeId);
		                        }),
		       "Chooser duplicated the main alias or included hidden outputs");
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
		Expect(selection.assetMissing && selection.assetGuid == textureGuid && selection.assetLocalFileId == meshA,
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
