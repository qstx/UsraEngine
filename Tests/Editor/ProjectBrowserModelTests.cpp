#include <Assets/ProjectBrowserModel.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
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
		Expect(model.CurrentFolder() == "Textures" && model.SelectedAsset().scheme.empty(),
		       "Browser selection was not cleared after its asset was renamed");

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
