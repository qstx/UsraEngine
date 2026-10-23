#include <Assets/AssetImportRules.hpp>
#include <Assets/ManagedImportRequirements.hpp>
#include <Assets/AssetDatabase.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakBuilder.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

ImportScriptType Type(UInt64 id, const char* name = "Tests.Importer")
{
	return {Take(ResourceId::Parse("12345678-1234-4234-8234-123456789abc")), id, name};
}

void Registration()
{
	auto native = Take(AssetImportRules::Create());
	Expect(Take(native.FindImporter("PNG"))->identity == "ursa.Texture" &&
	           Take(native.FindImporter(".obj"))->identity == "ursa.Model" &&
	           Take(native.FindImporter(".GLTF"))->identity == "ursa.Model" &&
	           Take(native.FindImporter(".GlB"))->identity == "ursa.Model" &&
	           Take(native.FindImporter(".unknown"))->identity == "ursa.Default",
	       "Native importer resolution changed or did not normalize glTF/GLB extensions");
	const ScriptedImporterDescriptor shape{Type(1), 2, {"shape", ".SHAPEDATA"}};
	auto rules = Take(AssetImportRules::Create({shape}, {}, "code-v1", 7));
	const auto stableId = Take(shape.type.StableIdentity());
	Expect(Take(rules.FindImporter(".Shape", stableId))->version == 2 && rules.Generation() == 7,
	       "Scripted importer registration lost normalized extension or version");
	Expect(!native.FindImporter(".shape", stableId), "Missing scripted importer fell back to a default copy");
	Expect(!AssetImportRules::Create({shape}, {}, "", 1) && !AssetImportRules::Create({}, {}, "", 0),
	       "Incomplete snapshot identity was accepted");
	Expect(!AssetImportRules::Create({{Type(1), 1, {"../bad"}}}, {}, "code"), "Invalid extension was registered");
	auto collision = AssetImportRules::Create({shape, {Type(2, "Tests.Other"), 1, {"shape"}}}, {}, "code");
	Expect(!collision && collision.error.find("Tests.Importer") != String::npos &&
	           collision.error.find("Tests.Other") != String::npos,
	       "Duplicate extension diagnostic did not identify both importers");
	Expect(!AssetImportRules::Create({{Type(1), 1, {"PNG"}}}, {}, "code"),
	       "Scripted importer replaced a native importer");
	for (const String extension : {".ursamat", ".ursaprefab", ".ursascene", ".ursamesh", ".ursatex", ".ursashaderasset",
	                               ".ursamodel", ".gltf", ".glb", ".fbx", ".obj", ".cs", ".dll", ".meta"})
	{
		Expect(!AssetImportRules::Create({{Type(1), 1, {extension}}}, {}, "code"),
		       "Scripted importer replaced a reserved native source format");
	}
	auto renamed = shape;
	renamed.type.clrTypeName = "Renamed.Importer";
	auto renamedRules = Take(AssetImportRules::Create({renamed}, {}, "code-v1", 8));
	Expect(Take(renamedRules.FindImporter("shape", stableId))->identity == stableId &&
	           Take(rules.FingerprintFor("shape", "prj")) == Take(renamedRules.FingerprintFor("shape", "prj")),
	       "CLR name or session generation changed persistent rule identity");
}

void Processing()
{
	Vector<AssetPostprocessorDescriptor> processors{{Type(3), 0, 10, AssetPostprocessorTarget::Texture},
	                                                {Type(2), 2, -10, AssetPostprocessorTarget::TextureAndModel},
	                                                {Type(1), 1, 10, AssetPostprocessorTarget::Texture}};
	auto first = Take(AssetImportRules::Create({}, processors, "code-v1"));
	std::reverse(processors.begin(), processors.end());
	auto second = Take(AssetImportRules::Create({}, processors, "code-v1", 2));
	const auto chain = second.PostprocessorsFor("ursa.Texture", "prj");
	Expect(chain.size() == 3 && chain[0].type.localFileId == 2 && chain[1].type.localFileId == 1 &&
	           chain[2].type.localFileId == 3 && second.PostprocessorsFor("ursa.Model", "prj").size() == 1,
	       "Postprocessor priority, stable tie break or targets are wrong");
	Expect(second.PostprocessorsFor("ursa.Texture", "ursa").empty(), "Project rules modified Engine sources");
	Expect(Take(first.FingerprintFor("png", "prj")) == Take(second.FingerprintFor("png", "prj")),
	       "Rule discovery order changed the fingerprint");
	auto recompiled = Take(AssetImportRules::Create({}, processors, "code-v2"));
	Expect(Take(first.FingerprintFor("png", "prj")) != Take(recompiled.FingerprintFor("png", "prj")) &&
	           Take(first.FingerprintFor("png", "ursa")) == Take(recompiled.FingerprintFor("png", "ursa")) &&
	           Take(first.FingerprintFor("ursashader", "prj")) == Take(recompiled.FingerprintFor("ursashader", "prj")),
	       "Code changes missed affected rules or invalidated unrelated native imports");
	processors[0].version++;
	auto versioned = Take(AssetImportRules::Create({}, processors, "code-v1"));
	Expect(Take(first.FingerprintFor("png", "prj")) != Take(versioned.FingerprintFor("png", "prj")),
	       "Postprocessor version was not part of rule identity");
	processors.push_back(processors[0]);
	Expect(!AssetImportRules::Create({}, processors, "code-v1"), "Duplicate postprocessor type was accepted");
}

void MainObjectDirectory()
{
	VirtualFileSystem files;
	auto sources = std::make_shared<MemoryFileProvider>();
	Done(files.MountProvider("prj", sources, MountAccess::ReadWrite));
	Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto source = Take(VirtualPath::Parse("prj:Example.shape"));
	Done(files.WriteAtomic(source, {1, 2, 3}));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	const auto guid = Take(database->Metadata(source)).guid;
	const auto mainId = Take(StableImportObjectId("example.importer", "main"));
	const auto childId = Take(StableImportObjectId("example.importer", "mesh"));
	Expect(mainId != 0 && childId != 0 && mainId != childId &&
	           mainId == Take(StableImportObjectId("example.importer", "main")) &&
	           mainId != Take(StableImportObjectId("example.other", "main")),
	       "Output identity was not stable or importer scoped");
	Expect(!StableImportObjectId("example.importer", ""), "Empty output key was accepted");
	Vector<ImportedArtifact> artifacts{{childId, "Mesh", {4}, "mesh"}, {mainId, "Prefab", {5}, "main"}};
	Done(
	    database->PublishImport(source, "example.importer", 1, "cpu", nlohmann::json::object(), {}, artifacts, mainId));
	auto reopened = Take(AssetDatabase::Open(files));
	Done(reopened->Refresh());
	const auto status = Take(reopened->CurrentImport(source));
	Expect(status.upToDate && status.mainObjectLocalFileId == mainId && status.artifacts.size() == 2,
	       "Main object directory did not survive a cache reopen");
	Expect(status.artifacts[0].outputKey == "mesh" && status.artifacts[1].outputKey == "main" &&
	           Take(reopened->Resolve(guid)) == Take(reopened->Resolve(guid, mainId)) &&
	           Take(reopened->SourcePath(guid)) == source,
	       "Output keys or main alias were lost");
	const auto records = reopened->ImportedArtifacts();
	Expect(records.size() == 2 && !records[0].isMainObject && records[1].isMainObject,
	       "Directory main object was inferred from output order or zero ID");
	Expect(!reopened->PublishImport(source, "example.importer", 1, "cpu", nlohmann::json::object(), {}, artifacts, 42),
	       "Missing main object was accepted on a cache hit");
	Expect(Take(reopened->CurrentImport(source)).mainObjectLocalFileId == mainId,
	       "Invalid output modified the published directory");
	auto duplicate = artifacts;
	duplicate.push_back(artifacts[0]);
	Expect(
	    !reopened->PublishImport(source, "example.importer", 1, "cpu", nlohmann::json::object(), {}, duplicate, mainId),
	    "Duplicate object key or ID was published on a cache hit");
	auto mismatched = artifacts;
	mismatched[0].localFileId++;
	Expect(!reopened->PublishImport(source, "example.importer", 1, "cpu", nlohmann::json::object(), {}, mismatched,
	                                mainId),
	       "Output ID was not checked against its stable key");
	std::reverse(artifacts.begin(), artifacts.end());
	Done(files.WriteAtomic(source, {1, 2, 4}));
	Done(
	    reopened->PublishImport(source, "example.importer", 1, "cpu", nlohmann::json::object(), {}, artifacts, mainId));
	Expect(Take(reopened->Resolve(guid)) == Take(reopened->Resolve(guid, mainId)) && reopened->Resolve(guid, childId),
	       "Reordering outputs changed their identities");
	const auto renamed = Take(VirtualPath::Parse("prj:Renamed.shape"));
	Done(reopened->Move(source, renamed));
	Done(reopened->PublishImport(renamed, "example.importer", 1, "cpu", nlohmann::json::object(), {}, artifacts,
	                             mainId));
	Expect(Take(reopened->Metadata(renamed)).guid == guid && reopened->Resolve(guid, childId),
	       "Moving the source changed output identity");
	VirtualFileSystem rebuiltFiles;
	Done(rebuiltFiles.MountProvider("prj", sources, MountAccess::ReadWrite));
	Done(rebuiltFiles.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	auto rebuilt = Take(AssetDatabase::Open(rebuiltFiles));
	Done(rebuilt->Refresh());
	Done(
	    rebuilt->PublishImport(renamed, "example.importer", 1, "cpu", nlohmann::json::object(), {}, artifacts, mainId));
	Expect(Take(rebuilt->Metadata(renamed)).guid == guid && rebuilt->Resolve(guid, mainId) &&
	           rebuilt->Resolve(guid, childId),
	       "Rebuilding Library changed imported identities");
	Done(rebuiltFiles.WriteAtomic(renamed, {1, 2, 5}));
	artifacts.resize(1);
	Done(
	    rebuilt->PublishImport(renamed, "example.importer", 1, "cpu", nlohmann::json::object(), {}, artifacts, mainId));
	Expect(!rebuilt->Resolve(guid, childId) && rebuilt->Resolve(guid), "Deleted sub-asset still resolved");
}

void NativeCapability()
{
	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("settings", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	Done(CheckNativeImportCapability(*database));
	const auto source = Take(VirtualPath::Parse("prj:Editor/Rules.cs"));
	Done(files.WriteAtomic(source, {1, 2, 3}));
	Done(database->Refresh());
	Expect(!CheckNativeImportCapability(*database), "Unknown Editor rules were accepted by a native-only importer");
	const auto native = Take(AssetImportRules::Create());
	Done(SaveManagedImportRequirements(*database, native));
	Done(CheckNativeImportCapability(*database));
	Done(files.WriteAtomic(source, {1, 2, 4}));
	Expect(!CheckNativeImportCapability(*database), "Changed scripts reused an old no-rules capability declaration");
	Done(files.WriteAtomic(source, {1, 2, 3}));
	const auto type = ImportScriptType{Take(database->Metadata(source)).guid, 1, "Tests.Rules"};
	const auto processors =
	    Take(AssetImportRules::Create({}, {{type, 1, 0, AssetPostprocessorTarget::Texture}}, "code"));
	Done(SaveManagedImportRequirements(*database, processors));
	Expect(!CheckNativeImportCapability(*database), "A required texture processor was skipped without .NET");
	const auto importer = Take(AssetImportRules::Create({{type, 1, {"shape"}}}, {}, "code"));
	Done(SaveManagedImportRequirements(*database, importer));
	Expect(!CheckNativeImportCapability(*database), "A required custom importer was skipped without .NET");
	Done(SaveManagedImportRequirements(*database, native));
	const auto asset = Take(VirtualPath::Parse("prj:Example.shape"));
	Done(files.WriteAtomic(asset, {1}));
	Done(database->Refresh());
	Done(database->SetImporterSettings(asset, Take(type.StableIdentity()), 1, nlohmann::json::object()));
	Expect(!CheckNativeImportCapability(*database), "Required importer metadata fell back to a native copy");
}

void RuntimeMainAlias()
{
	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto guid = ResourceId::Generate();
	const auto mainId = Take(StableImportObjectId("runtime.fixture", "main"));
	const auto childId = Take(StableImportObjectId("runtime.fixture", "child"));
	MeshData data;
	data.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	data.indices = {0, 1, 2};
	const auto mesh = Take(Mesh::Create(std::move(data)));
	const auto mainPath = Take(VirtualPath::Parse("prj:__UrsaCooked/main.ursamesh"));
	const auto childPath = Take(VirtualPath::Parse("prj:__UrsaCooked/child.ursamesh"));
	Done(files.WriteAtomic(mainPath, Take(EncodeMeshResource(guid, *mesh, mainId))));
	Done(files.WriteAtomic(childPath, Take(EncodeMeshResource(guid, *mesh, childId))));
	nlohmann::json catalog{{"format", "UrsaAssetCatalog"},
	                       {"version", 2u},
	                       {"assets", nlohmann::json::array({{{"guid", ToStdString(guid.ToString())},
	                                                          {"localFileId", childId},
	                                                          {"kind", "Mesh"},
	                                                          {"isMainObject", false},
	                                                          {"path", ToStdString(childPath.ToString())}},
	                                                         {{"guid", ToStdString(guid.ToString())},
	                                                          {"localFileId", mainId},
	                                                          {"kind", "Mesh"},
	                                                          {"isMainObject", true},
	                                                          {"path", ToStdString(mainPath.ToString())}}})}};
	const auto catalogPath = Take(VirtualPath::Parse("prj:__UrsaCooked/AssetCatalog.ursacatalog"));
	const auto writeCatalog = [&]
	{
		const auto text = catalog.dump();
		Done(files.WriteAtomic(catalogPath, Vector<UInt8>(text.data(), text.data() + text.size())));
	};
	writeCatalog();
	const auto verify = [&](const VirtualFileSystem& content)
	{
		auto resources = Take(ResourceSession::Open(content));
		Expect(Take(resources->ResolveAsset(guid)) == mainPath &&
		           Take(resources->ResolveAsset(guid, childId)) == childPath,
		       "Runtime catalog inferred main resource from enumeration order");
		const auto main = Take(resources->LoadMesh(guid, mainId));
		const auto alias = Take(resources->LoadMesh(guid, 0));
		Expect(main == alias && alias->AssetIdentity().localFileId == mainId &&
		           Take(alias->GetData()).positions.size() == 3 && Take(resources->LoadMesh(guid, childId)) != main,
		       "Main alias did not load the canonical Mesh or flattened a sub-asset");
	};
	verify(files);
	const auto temp =
	    std::filesystem::temp_directory_path() / ("UrsaImportRules-" + ToStdString(ResourceId::Generate().ToString()));
	std::filesystem::create_directories(temp);
	try
	{
		VirtualFileSystem output;
		Done(output.Mount("out", temp, MountAccess::ReadWrite));
		Done(PakBuilder::Build(files, {"prj"}, output, Take(VirtualPath::Parse("out:Assets.pak"))));
		auto package = Take(PakArchive::Open(temp / "Assets.pak"));
		VirtualFileSystem packaged;
		Done(package->Mount(packaged));
		verify(packaged);
	}
	catch (...)
	{
		std::filesystem::remove_all(temp);
		throw;
	}
	std::filesystem::remove_all(temp);
	catalog["assets"][0]["isMainObject"] = true;
	writeCatalog();
	Expect(!ResourceSession::Open(files), "Duplicate main object in runtime catalog was accepted");
	catalog["assets"][0]["isMainObject"] = false;
	catalog["assets"][1]["isMainObject"] = false;
	writeCatalog();
	Expect(!ResourceSession::Open(files), "Missing main object in runtime catalog was accepted");
}
} // namespace

int main()
{
	try
	{
		Registration();
		Processing();
		NativeCapability();
		MainObjectDirectory();
		RuntimeMainAlias();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
