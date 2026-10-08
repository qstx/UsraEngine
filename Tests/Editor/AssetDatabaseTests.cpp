#include <Assets/AssetDatabase.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <chrono>
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

VirtualPath Path(const char* value)
{
	auto parsed = VirtualPath::Parse(value);
	if (!parsed)
	{
		throw std::runtime_error(ToStdString(parsed.error));
	}
	return std::move(parsed.value);
}

void WriteSource(const std::filesystem::path& path, const char* text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << text;
	if (!output)
	{
		throw std::runtime_error("Cannot create the AssetDatabase fixture");
	}
}

nlohmann::json MetadataDocument(ResourceId guid, bool folder)
{
	return {
	    {"version", assetMetadataVersion}, {"guid", ToStdString(guid.ToString())}, {"folder", folder}, {"importer", ""},
	    {"importerVersion", 0u},           {"settings", nlohmann::json::object()}};
}

void WriteMetadataFile(const std::filesystem::path& path, ResourceId guid, bool folder)
{
	WriteSource(path, MetadataDocument(guid, folder).dump(2).c_str());
}

Vector<Ursa::UInt8> Bytes(const char* text)
{
	Vector<Ursa::UInt8> result;
	while (*text != '\0')
	{
		result.push_back(static_cast<Ursa::UInt8>(*text++));
	}
	return result;
}

AssetMetadata TakeMetadata(AssetDatabase& database, const char* path)
{
	auto metadata = database.Metadata(Path(path));
	if (!metadata)
	{
		throw std::runtime_error(ToStdString(metadata.error));
	}
	return std::move(metadata.value);
}

void TestCreateMoveCopyAndRemove(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	const auto assetsRoot = root / "Assets";
	WriteSource(assetsRoot / "Models" / "Hero" / "mesh.fbx", "fbx-source");
	WriteSource(assetsRoot / "Models" / "Hero" / "Textures" / "Albedo.png", "png-source");

	VirtualFileSystem filesystem;
	auto mounted = filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite);
	Expect(static_cast<bool>(mounted), "Cannot mount the writable project fixture");
	auto opened = AssetDatabase::Open(filesystem);
	Expect(static_cast<bool>(opened), "Cannot open the AssetDatabase");
	AssetDatabase& database = *opened.value;
	auto refreshed = database.Refresh();
	Expect(static_cast<bool>(refreshed),
	       ("AssetDatabase did not create missing metadata: " + ToStdString(refreshed.error)).c_str());

	const ResourceId folderGuid = TakeMetadata(database, "prj:Models").guid;
	const ResourceId fileGuid = TakeMetadata(database, "prj:Models/Hero/mesh.fbx").guid;
	const ResourceId nestedFolderGuid = TakeMetadata(database, "prj:Models/Hero/Textures").guid;
	Expect(TakeMetadata(database, "prj:Models").folder && TakeMetadata(database, "prj:Models/Hero/Textures").folder &&
	           !TakeMetadata(database, "prj:Models/Hero/mesh.fbx").folder,
	       "File and folder metadata kinds were not recorded");
	Expect(std::filesystem::exists(assetsRoot / "Models.meta") &&
	           std::filesystem::exists(assetsRoot / "Models" / "Hero" / "Textures.meta"),
	       "Directory sidecars were not created beside their directories");

	auto copied = database.Copy(Path("prj:Models"), Path("prj:Copies/Models"));
	if (!copied)
	{
		throw std::runtime_error("AssetDatabase could not copy a folder hierarchy: " + ToStdString(copied.error));
	}
	Expect(TakeMetadata(database, "prj:Copies/Models").guid != folderGuid &&
	           TakeMetadata(database, "prj:Copies/Models/Hero/mesh.fbx").guid != fileGuid &&
	           TakeMetadata(database, "prj:Copies/Models/Hero/Textures").guid != nestedFolderGuid,
	       "Copied assets reused the original GUIDs");

	Expect(static_cast<bool>(database.Move(Path("prj:Models"), Path("prj:Moved/Models"))),
	       "AssetDatabase could not move a folder hierarchy");
	Expect(TakeMetadata(database, "prj:Moved/Models").guid == folderGuid &&
	           TakeMetadata(database, "prj:Moved/Models/Hero/mesh.fbx").guid == fileGuid &&
	           TakeMetadata(database, "prj:Moved/Models/Hero/Textures").guid == nestedFolderGuid,
	       "Moving assets changed their GUIDs");
	Expect(!filesystem.Exists(Path("prj:Models/Hero/mesh.fbx")).value &&
	           filesystem.Exists(Path("prj:Moved/Models/Hero/mesh.fbx")).value,
	       "Moving a folder left source files behind or lost destination files");

	Expect(static_cast<bool>(database.Remove(Path("prj:Copies/Models"))),
	       "AssetDatabase could not remove a copied folder hierarchy");
	Expect(!filesystem.Exists(Path("prj:Copies/Models/Hero/mesh.fbx")).value &&
	           !filesystem.Exists(Path("prj:Copies/Models/Hero/mesh.fbx.meta")).value,
	       "Removing an asset folder left its source or metadata files behind");
}

void TestDuplicateGuidRepair(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	const auto assetsRoot = root / "Assets";
	const ResourceId duplicateGuid = ResourceId::Generate();
	WriteSource(assetsRoot / "A.txt", "first");
	WriteMetadataFile(assetsRoot / "A.txt.meta", duplicateGuid, false);
	WriteSource(assetsRoot / "Z.txt", "copy");
	WriteMetadataFile(assetsRoot / "Z.txt.meta", duplicateGuid, false);

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount duplicate-GUID fixture");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "AssetDatabase rejected duplicate GUIDs");
	const ResourceId firstGuid = TakeMetadata(*database.value, "prj:A.txt").guid;
	const ResourceId copiedGuid = TakeMetadata(*database.value, "prj:Z.txt").guid;
	Expect(firstGuid == duplicateGuid && copiedGuid != duplicateGuid && copiedGuid != firstGuid,
	       "Duplicate repair changed the first asset or failed to assign a fresh GUID to the copy");
}

void TestScriptDiscoveryAndGrouping(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	const auto assetsRoot = root / "Assets";
	WriteSource(assetsRoot / "Scripts" / "PlayerMover.cs", "public sealed class PlayerMover { }");
	WriteSource(assetsRoot / "Scripts" / "Editor" / "Tools" / "SceneTool.cs", "public sealed class SceneTool { }");
	WriteSource(assetsRoot / "Scripts" / "Editorial" / "NotEditorScript.cs", "public sealed class NotEditorScript { }");
	WriteSource(assetsRoot / "EditorTools.cs", "public sealed class EditorTools { }");
	WriteSource(assetsRoot / "Scripts" / "Notes.txt", "not a C# script");

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount script discovery fixture");
	auto opened = AssetDatabase::Open(filesystem);
	Expect(opened && static_cast<bool>(opened.value->Refresh()), "AssetDatabase did not discover C# source assets");
	AssetDatabase& database = *opened.value;
	const auto scriptSources = database.ScriptSources();
	Expect(scriptSources.size() == 4, "Script discovery included non-CS files or missed C# source files");
	const ResourceId playerGuid = TakeMetadata(database, "prj:Scripts/PlayerMover.cs").guid;
	const ResourceId editorGuid = TakeMetadata(database, "prj:Scripts/Editor/Tools/SceneTool.cs").guid;
	Expect(static_cast<bool>(filesystem.Exists(Path("prj:Scripts/PlayerMover.cs.meta")).value),
	       "New C# source did not receive AssetDatabase metadata");
	const auto findScript = [&database](const char* path) -> ScriptSourceAsset
	{
		for (const ScriptSourceAsset& script : database.ScriptSources())
		{
			if (ToStdString(script.path.ToString()) == path)
			{
				return script;
			}
		}
		throw std::runtime_error(ToStdString(String("C# source was not discovered: ") + path));
	};
	Expect(findScript("prj:Scripts/PlayerMover.cs").group == ScriptAssemblyGroup::Game &&
	           findScript("prj:Scripts/Editor/Tools/SceneTool.cs").group == ScriptAssemblyGroup::Editor &&
	           findScript("prj:EditorTools.cs").group == ScriptAssemblyGroup::Game &&
	           findScript("prj:Scripts/Editorial/NotEditorScript.cs").group == ScriptAssemblyGroup::Game,
	       "Script grouping did not use the exact Editor directory segment");

	Expect(
	    static_cast<bool>(database.Move(Path("prj:Scripts/PlayerMover.cs"), Path("prj:Scripts/Editor/PlayerMover.cs"))),
	    "Could not move a game script into the Editor group");
	auto movedPlayer = findScript("prj:Scripts/Editor/PlayerMover.cs");
	Expect(movedPlayer.guid == playerGuid && movedPlayer.group == ScriptAssemblyGroup::Editor,
	       "Moving a script changed its GUID or retained its old compiler group");

	Expect(static_cast<bool>(
	           database.Move(Path("prj:Scripts/Editor/Tools/SceneTool.cs"), Path("prj:Scripts/Tools/SceneTool.cs"))),
	       "Could not move an Editor script into the game group");
	auto movedTool = findScript("prj:Scripts/Tools/SceneTool.cs");
	Expect(movedTool.guid == editorGuid && movedTool.group == ScriptAssemblyGroup::Game,
	       "Moving a script out of Editor changed its GUID or retained the Editor compiler group");

	Expect(static_cast<bool>(database.Copy(Path("prj:Scripts/Editor/PlayerMover.cs"), Path("prj:Scripts/GameCopy.cs"))),
	       "Could not copy a C# source asset");
	auto copiedPlayer = findScript("prj:Scripts/GameCopy.cs");
	Expect(copiedPlayer.guid != playerGuid && copiedPlayer.group == ScriptAssemblyGroup::Game,
	       "Copied script source reused its GUID or inherited its Editor group");
}

void TestReadOnlyAndInvalidMetadata(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	const auto readOnlyRoot = root / "ReadOnly";
	WriteSource(readOnlyRoot / "NoMeta.txt", "source");
	VirtualFileSystem readOnlyFilesystem;
	Expect(static_cast<bool>(readOnlyFilesystem.Mount(projectScheme, readOnlyRoot, MountAccess::ReadOnly)),
	       "Cannot mount read-only fixture");
	auto readOnlyDatabase = AssetDatabase::Open(readOnlyFilesystem);
	Expect(readOnlyDatabase && !readOnlyDatabase.value->Refresh(),
	       "AssetDatabase silently accepted missing metadata on a read-only mount");

	const auto invalidRoot = root / "Invalid";
	WriteSource(invalidRoot / "Broken.txt", "source");
	WriteSource(invalidRoot / "Broken.txt.meta", "{ malformed");
	VirtualFileSystem invalidFilesystem;
	Expect(static_cast<bool>(invalidFilesystem.Mount(projectScheme, invalidRoot, MountAccess::ReadWrite)),
	       "Cannot mount invalid metadata fixture");
	auto invalidDatabase = AssetDatabase::Open(invalidFilesystem);
	Expect(invalidDatabase && !invalidDatabase.value->Refresh(), "AssetDatabase silently repaired malformed metadata");
}

void TestImportCache(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	const auto assetsRoot = root / "Assets";
	const auto libraryRoot = root / "Library";
	WriteSource(assetsRoot / "Models" / "Robot.fbx", "robot-source-v1");
	WriteSource(assetsRoot / "Models" / "Robot.mtl", "robot-material-v1");
	std::filesystem::create_directories(libraryRoot);

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount import-cache source fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount import-cache Library fixture");
	auto opened = AssetDatabase::Open(filesystem);
	Expect(opened && static_cast<bool>(opened.value->Refresh()), "Cannot initialize import-cache database");
	AssetDatabase& database = *opened.value;
	const VirtualPath source = Path("prj:Models/Robot.fbx");
	const Vector<VirtualPath> dependencies{Path("prj:Models/Robot.mtl")};
	const nlohmann::json settings = {{"scale", 1.0}, {"generateNormals", true}};
	auto status = database.CheckImport(source, "ursa.Model", 1, "vulkan", settings, dependencies);
	Expect(status && !status.value.upToDate && !status.value.fingerprint.empty(),
	       "A new source unexpectedly had a reusable import cache entry");
	const ResourceId guid = TakeMetadata(database, "prj:Models/Robot.fbx").guid;
	const Vector<ImportedArtifact> firstArtifacts{{17, "mesh", Bytes("mesh-v1")},
	                                              {29, "material", Bytes("material-v1")}};
	Expect(static_cast<bool>(
	           database.PublishImport(source, "ursa.Model", 1, "vulkan", settings, dependencies, firstArtifacts)),
	       "AssetDatabase could not publish a content-addressed import");
	status = database.CheckImport(source, "ursa.Model", 1, "vulkan", settings, dependencies);
	Expect(status && status.value.upToDate && status.value.artifacts.size() == 2,
	       "A successful import was not reusable from the cache");
	auto meshPath = database.Resolve(guid, 17);
	Expect(static_cast<bool>(meshPath) && meshPath.value.scheme == editorLibraryScheme,
	       "GUID/local-file-ID resolution did not return the cached mesh artifact path");
	auto meshText = filesystem.ReadText(meshPath.value);
	Expect(meshText && ToStdString(meshText.value) == "mesh-v1",
	       "GUID/local-file-ID resolution returned incorrect mesh artifact bytes");

	auto reopened = AssetDatabase::Open(filesystem);
	Expect(reopened && static_cast<bool>(reopened.value->Refresh()), "Cannot reload persistent import cache");
	status = reopened.value->CheckImport(source, "ursa.Model", 1, "vulkan", settings, dependencies);
	Expect(status && status.value.upToDate, "Import cache did not survive an AssetDatabase reopen");

	const VirtualPath dependency = Path("prj:Models/Robot.mtl");
	Expect(static_cast<bool>(filesystem.WriteAtomic(dependency, Bytes("robot-material-v2"))),
	       "Cannot update the import dependency fixture");
	status = reopened.value->CheckImport(source, "ursa.Model", 1, "vulkan", settings, dependencies);
	Expect(status && !status.value.upToDate, "Changing a dependency did not invalidate its import artifact");
	const String changedFingerprint = status.value.fingerprint;
	const auto withoutDependency = reopened.value->CheckImport(source, "ursa.Model", 1, "vulkan", settings, {});
	Expect(withoutDependency && withoutDependency.value.fingerprint != changedFingerprint,
	       "Dependency content did not affect the import fingerprint");

	const Vector<ImportedArtifact> invalidArtifacts{{31, "mesh", Bytes("new-mesh")},
	                                                {31, "material", Bytes("duplicate-id")}};
	Expect(!reopened.value->PublishImport(source, "ursa.Model", 1, "vulkan", settings, dependencies, invalidArtifacts),
	       "Importer output with duplicate local IDs was published");
	status = reopened.value->CheckImport(source, "ursa.Model", 1, "vulkan", settings, dependencies);
	Expect(status && !status.value.upToDate && status.value.fingerprint == changedFingerprint,
	       "A failed import replaced the previous cache state");

	const Vector<ImportedArtifact> updatedArtifacts{{17, "mesh", Bytes("mesh-v2")},
	                                                {29, "material", Bytes("material-v2")}};
	Expect(static_cast<bool>(reopened.value->PublishImport(source, "ursa.Model", 1, "vulkan", settings, dependencies,
	                                                       updatedArtifacts)),
	       "AssetDatabase could not publish artifacts after a dependency change");
	status = reopened.value->CheckImport(source, "ursa.Model", 2, "vulkan", settings, dependencies);
	Expect(status && !status.value.upToDate, "Changing the importer version did not invalidate its cache");
}
} // namespace

int main()
{
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaAssetDatabaseTests-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);
		TestCreateMoveCopyAndRemove(root / "Operations");
		TestScriptDiscoveryAndGrouping(root / "Scripts");
		TestDuplicateGuidRepair(root / "Duplicate");
		TestReadOnlyAndInvalidMetadata(root / "Validation");
		TestImportCache(root / "ImportCache");
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
