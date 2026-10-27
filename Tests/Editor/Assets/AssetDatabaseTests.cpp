#include <Assets/AssetDatabase.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceId.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

class FailingMemoryProvider final : public VirtualFileProvider
{
public:
	String Identity() const override
	{
		return memory.Identity();
	}

	String SourceLabel() const override
	{
		return memory.SourceLabel();
	}

	Result<bool> Exists(const String& path) const override
	{
		return memory.Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return memory.Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return memory.ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return memory.List();
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		if (writesBeforeFailure > 0 && --writesBeforeFailure == 0)
		{
			writesBeforeFailure = -1;
			return Result<void>::Failure("injected atomic write failure");
		}
		return memory.WriteAtomic(path, bytes, createDirectories);
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		return memory.Append(path, bytes, createDirectories);
	}

	Result<void> CreateDirectory(const String& path) override
	{
		(void)path;
		return {};
	}

	Result<void> RemoveDirectory(const String& path, bool removeEmptyChildren) override
	{
		(void)path;
		(void)removeEmptyChildren;
		return {};
	}

	Result<void> Remove(const String& path) override
	{
		if (removesBeforeFailure > 0 && --removesBeforeFailure == 0)
		{
			removesBeforeFailure = -1;
			return Result<void>::Failure("injected remove failure");
		}
		return memory.Remove(path);
	}

	Result<void> CheckWritable(const String& path) const override
	{
		return memory.CheckWritable(path);
	}

	Result<void> Rename(const String& source, const String& destination) override
	{
		return memory.Rename(source, destination);
	}

	Result<std::shared_ptr<VirtualFileLease>> AcquireExclusiveLease(const String& path) override
	{
		return memory.AcquireExclusiveLease(path);
	}

	void FailWriteOn(Ursa::Size operation)
	{
		writesBeforeFailure = static_cast<int>(operation);
	}

	void FailRemoveOn(Ursa::Size operation)
	{
		removesBeforeFailure = static_cast<int>(operation);
	}

private:
	MemoryFileProvider memory;
	int writesBeforeFailure = -1;
	int removesBeforeFailure = -1;
};

class FailingDirectoryProvider final : public VirtualFileProvider
{
public:
	explicit FailingDirectoryProvider(std::shared_ptr<DirectoryFileProvider> provider) : provider(std::move(provider))
	{
	}

	String Identity() const override
	{
		return provider->Identity();
	}

	String SourceLabel() const override
	{
		return provider->SourceLabel();
	}

	Result<bool> Exists(const String& path) const override
	{
		return provider->Exists(path);
	}

	Result<UInt64> Size(const String& path) const override
	{
		return provider->Size(path);
	}

	Result<Vector<UInt8>> ReadRange(const String& path, UInt64 offset, UInt64 size) const override
	{
		return provider->ReadRange(path, offset, size);
	}

	Result<Vector<String>> List() const override
	{
		return provider->List();
	}

	Result<void> WriteAtomic(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		if (writesBeforeFailure > 0 && --writesBeforeFailure == 0)
		{
			writesBeforeFailure = -1;
			return Result<void>::Failure("injected directory-provider write failure");
		}
		return provider->WriteAtomic(path, bytes, createDirectories);
	}

	Result<void> Append(const String& path, const Vector<UInt8>& bytes, bool createDirectories) override
	{
		return provider->Append(path, bytes, createDirectories);
	}

	Result<void> CreateDirectory(const String& path) override
	{
		return provider->CreateDirectory(path);
	}

	Result<void> RemoveDirectory(const String& path, bool removeEmptyChildren) override
	{
		if (failRemoveDirectory)
		{
			failRemoveDirectory = false;
			return Result<void>::Failure("injected directory removal failure");
		}
		return provider->RemoveDirectory(path, removeEmptyChildren);
	}

	std::filesystem::path DirectoryRoot() const override
	{
		return provider->DirectoryRoot();
	}

	Result<void> Remove(const String& path) override
	{
		if (removesBeforeFailure > 0 && --removesBeforeFailure == 0)
		{
			removesBeforeFailure = -1;
			return Result<void>::Failure("injected directory-provider remove failure");
		}
		return provider->Remove(path);
	}

	Result<void> CheckWritable(const String& path) const override
	{
		return provider->CheckWritable(path);
	}

	Result<void> Rename(const String& source, const String& destination) override
	{
		return provider->Rename(source, destination);
	}

	Result<std::shared_ptr<VirtualFileLease>> AcquireExclusiveLease(const String& path) override
	{
		return provider->AcquireExclusiveLease(path);
	}

	void FailWriteOn(Ursa::Size operation)
	{
		writesBeforeFailure = static_cast<int>(operation);
	}

	void FailRemoveOn(Ursa::Size operation)
	{
		removesBeforeFailure = static_cast<int>(operation);
	}

	void FailNextDirectoryRemoval()
	{
		failRemoveDirectory = true;
	}

private:
	std::shared_ptr<DirectoryFileProvider> provider;
	int writesBeforeFailure = -1;
	int removesBeforeFailure = -1;
	bool failRemoveDirectory = false;
};

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
	auto emptyFolder = database.CreateFolder(Path("prj:Models/Empty"));
	Expect(static_cast<bool>(emptyFolder) && std::filesystem::is_directory(assetsRoot / "Models" / "Empty") &&
	           TakeMetadata(database, "prj:Models/Empty").folder,
	       "AssetDatabase did not create an indexed empty project folder");
	const ResourceId emptyFolderGuid = TakeMetadata(database, "prj:Models/Empty").guid;
	Expect(static_cast<bool>(database.Move(Path("prj:Models/Empty"), Path("prj:Models/RenamedEmpty"))) &&
	           !std::filesystem::exists(assetsRoot / "Models" / "Empty") &&
	           std::filesystem::is_directory(assetsRoot / "Models" / "RenamedEmpty") &&
	           TakeMetadata(database, "prj:Models/RenamedEmpty").guid == emptyFolderGuid,
	       "Renaming an empty folder did not preserve its GUID and directory");

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

void TestNativeDuplicateRepairCompensation()
{
	auto provider = std::make_shared<FailingMemoryProvider>();
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.MountProvider(projectScheme, provider, MountAccess::ReadWrite)),
	       "Cannot mount duplicate native asset fixture");
	const auto id = ResourceId::Generate();
	const auto content =
	    nlohmann::json{{"format", "UrsaMaterial"}, {"version", 1}, {"id", ToStdString(id.ToString())}}.dump();
	const auto metadata = MetadataDocument(id, false).dump();
	const Vector<UInt8> sourceBytes = Bytes(content.c_str());
	const Vector<UInt8> metadataBytes = Bytes(metadata.c_str());
	Expect(static_cast<bool>(filesystem.WriteAtomic(Path("prj:Z.ursamat"), sourceBytes)) &&
	           static_cast<bool>(filesystem.WriteAtomic(Path("prj:Z.ursamat.meta"), metadataBytes)),
	       "Cannot seed original native asset");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "Cannot index original native asset");
	Expect(static_cast<bool>(filesystem.WriteAtomic(Path("prj:A.ursamat"), sourceBytes)) &&
	           static_cast<bool>(filesystem.WriteAtomic(Path("prj:A.ursamat.meta"), metadataBytes)),
	       "Cannot seed conflicting native asset");
	provider->FailWriteOn(2);
	const auto failed = database.value->Refresh();
	Expect(!failed && filesystem.Read(Path("prj:A.ursamat")).value == sourceBytes &&
	           filesystem.Read(Path("prj:A.ursamat.meta")).value == metadataBytes &&
	           TakeMetadata(*database.value, "prj:Z.ursamat").guid == id,
	       "Failed metadata rewrite did not restore the copied resource and preserve the index");
	Expect(static_cast<bool>(database.value->Refresh()), "Native duplicate repair did not succeed on retry");
	const auto repaired = TakeMetadata(*database.value, "prj:A.ursamat").guid;
	const auto header = ReadResourceHeader(filesystem, Path("prj:A.ursamat"));
	Expect(repaired != id && header && header.value.id == repaired &&
	           TakeMetadata(*database.value, "prj:Z.ursamat").guid == id,
	       "Retry did not preserve the original GUID and synchronize the repaired header");
}

void TestShaderDuplicateRepairCompensation()
{
	auto provider = std::make_shared<FailingMemoryProvider>();
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.MountProvider(projectScheme, provider, MountAccess::ReadWrite)),
	       "Cannot mount duplicate Shader fixture");
	Shader::ShaderAsset shader;
	shader.id = ResourceId::Generate();
	shader.name = "Tests/ShaderCopy";
	shader.sourcePath = "prj:source.ursashader";
	shader.sourceHash = "fixture";
	shader.compilerVersion = "fixture";
	const auto originalPath = Path("prj:Z.ursashaderasset");
	const auto copyPath = Path("prj:A.ursashaderasset");
	Expect(static_cast<bool>(Shader::SaveShaderAssetResource(filesystem, originalPath, shader)),
	       "Cannot create compiled Shader fixture");
	const auto metadata = MetadataDocument(shader.id, false).dump();
	const auto metadataBytes = Bytes(metadata.c_str());
	Expect(static_cast<bool>(filesystem.WriteAtomic(Path("prj:Z.ursashaderasset.meta"), metadataBytes)),
	       "Cannot write compiled Shader metadata");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()), "Cannot index original compiled Shader");
	const auto original = filesystem.Read(originalPath).value;
	Expect(static_cast<bool>(filesystem.WriteAtomic(copyPath, original)) &&
	           static_cast<bool>(filesystem.WriteAtomic(Path("prj:A.ursashaderasset.meta"), metadataBytes)),
	       "Cannot seed duplicate compiled Shader");
	provider->FailWriteOn(2);
	const auto failed = database.value->Refresh();
	Expect(!failed && filesystem.Read(copyPath).value == original &&
	           filesystem.Read(Path("prj:A.ursashaderasset.meta")).value == metadataBytes &&
	           TakeMetadata(*database.value, "prj:Z.ursashaderasset").guid == shader.id,
	       "Failed Shader metadata rewrite did not restore its file or preserve the original identity");
	Expect(static_cast<bool>(database.value->Refresh()), "Shader duplicate repair did not succeed on retry");
	const auto repaired = TakeMetadata(*database.value, "prj:A.ursashaderasset").guid;
	const auto loaded = Shader::LoadShaderAssetResource(filesystem, copyPath);
	Expect(repaired != shader.id && loaded && loaded.value.id == repaired &&
	           TakeMetadata(*database.value, "prj:Z.ursashaderasset").guid == shader.id,
	       "Retried Shader repair did not load under its new identity");
}

void TestBatchMoveAndRemoveCompensation()
{
	auto provider = std::make_shared<FailingMemoryProvider>();
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.MountProvider(projectScheme, provider, MountAccess::ReadWrite)) &&
	           static_cast<bool>(filesystem.MountProvider(editorLibraryScheme, provider, MountAccess::ReadWrite)),
	       "Cannot mount the fault-injection asset fixture");
	Expect(static_cast<bool>(filesystem.WriteAtomic(Path("prj:A.txt"), Bytes("source-a"))) &&
	           static_cast<bool>(filesystem.WriteAtomic(Path("prj:B.txt"), Bytes("source-b"))),
	       "Cannot seed batch transaction assets");
	auto opened = AssetDatabase::Open(filesystem);
	Expect(opened && static_cast<bool>(opened.value->Refresh()), "Cannot initialize batch transaction database");
	AssetDatabase& database = *opened.value;
	const ResourceId aGuid = TakeMetadata(database, "prj:A.txt").guid;
	const ResourceId bGuid = TakeMetadata(database, "prj:B.txt").guid;
	auto heldLease = filesystem.AcquireExclusiveLease(Path("lib:Import.lock"));
	Expect(static_cast<bool>(heldLease), "Cannot hold the project-wide asset write lease");
	Expect(!database.MoveBatch({{Path("prj:A.txt"), Path("prj:Moved/A.txt")}}) &&
	           filesystem.ReadText(Path("prj:A.txt")).value == "source-a",
	       "Asset batch mutation ignored an active import/write lease");
	heldLease.value.reset();

	provider->FailWriteOn(2);
	const auto failedMove = database.MoveBatch(
	    {{Path("prj:A.txt"), Path("prj:Moved/A.txt")}, {Path("prj:B.txt"), Path("prj:Moved/B.txt")}});
	Expect(!failedMove, "Injected second destination write failure was ignored");
	Expect(filesystem.ReadText(Path("prj:A.txt")).value == "source-a" &&
	           filesystem.ReadText(Path("prj:B.txt")).value == "source-b" &&
	           !filesystem.Exists(Path("prj:Moved/A.txt")).value && !filesystem.Exists(Path("prj:Moved/B.txt")).value &&
	           !filesystem.Exists(Path("prj:Moved")).value,
	       "Failed batch move did not restore source files and destination directories");
	Expect(TakeMetadata(database, "prj:A.txt").guid == aGuid && TakeMetadata(database, "prj:B.txt").guid == bGuid,
	       "Failed batch move changed indexed source metadata");
	const auto assetsAfterFailedMove = database.Assets();
	Expect(std::none_of(assetsAfterFailedMove.begin(), assetsAfterFailedMove.end(),
	                    [](const AssetRecord& asset) { return asset.path.relative == "Moved"; }),
	       "Failed batch move left a destination folder in the AssetDatabase index");

	provider->FailRemoveOn(2);
	const auto failedRemove = database.RemoveBatch({Path("prj:A.txt"), Path("prj:B.txt")});
	Expect(!failedRemove, "Injected second source removal failure was ignored");
	Expect(filesystem.ReadText(Path("prj:A.txt")).value == "source-a" &&
	           filesystem.ReadText(Path("prj:B.txt")).value == "source-b" && database.Contains(aGuid) &&
	           database.Contains(bGuid),
	       "Failed batch remove did not restore files and AssetDatabase index entries");

	auto committedMove = database.MoveBatch(
	    {{Path("prj:A.txt"), Path("prj:Moved/A.txt")}, {Path("prj:B.txt"), Path("prj:Moved/B.txt")}});
	if (!committedMove)
	{
		throw std::runtime_error("AssetDatabase could not commit a multi-source move batch: " +
		                         ToStdString(committedMove.error));
	}
	Expect(!filesystem.Exists(Path("prj:A.txt")).value && !filesystem.Exists(Path("prj:B.txt")).value &&
	           TakeMetadata(database, "prj:Moved/A.txt").guid == aGuid &&
	           TakeMetadata(database, "prj:Moved/B.txt").guid == bGuid,
	       "Successful batch move did not retain source identities at the destination");
	Expect(static_cast<bool>(database.RemoveBatch({Path("prj:Moved/A.txt"), Path("prj:Moved/B.txt")})) &&
	           !database.Contains(aGuid) && !database.Contains(bGuid),
	       "AssetDatabase could not commit a multi-source remove batch");
}

void TestDirectoryBatchCompensation(const std::filesystem::path& root)
{
	std::filesystem::remove_all(root);
	WriteSource(root / "Foo" / "Nested" / "mesh.fbx", "mesh-source");
	WriteSource(root / "Foo" / "Nested" / "material.mat", "material-source");
	auto directory = DirectoryFileProvider::Open(root);
	Expect(static_cast<bool>(directory), "Cannot open directory batch transaction fixture");
	auto provider = std::make_shared<FailingDirectoryProvider>(directory.value);
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.MountProvider(projectScheme, provider, MountAccess::ReadWrite)),
	       "Cannot mount directory batch transaction fixture");
	auto opened = AssetDatabase::Open(filesystem);
	Expect(opened && static_cast<bool>(opened.value->Refresh()), "Cannot initialize directory batch database");
	AssetDatabase& database = *opened.value;
	const auto sourceFiles = filesystem.List(projectScheme);
	Expect(static_cast<bool>(sourceFiles), "Cannot enumerate directory batch source files");
	Vector<std::pair<VirtualPath, Vector<UInt8>>> snapshot;
	for (const VirtualFileEntry& entry : sourceFiles.value)
	{
		snapshot.emplace_back(entry.path, filesystem.Read(entry.path).value);
	}
	const auto expectRestored = [&]()
	{
		for (const auto& entry : snapshot)
		{
			auto bytes = filesystem.Read(entry.first);
			if (!bytes || bytes.value != entry.second)
			{
				return false;
			}
		}
		return std::filesystem::is_directory(root / "Foo") && std::filesystem::is_directory(root / "Foo" / "Nested") &&
		       !std::filesystem::exists(root / "Moved");
	};
	const Vector<AssetMoveRequest> move{{Path("prj:Foo"), Path("prj:Moved/Foo")}};
	provider->FailWriteOn(2);
	Expect(!database.MoveBatch(move) && expectRestored(),
	       "A mid-stage directory move failure did not restore source, sidecars, and destination directories");
	provider->FailNextDirectoryRemoval();
	Expect(!database.MoveBatch(move) && expectRestored(),
	       "A late directory move failure did not compensate source removal and staged destinations");
	provider->FailRemoveOn(2);
	Expect(!database.RemoveBatch(Vector<VirtualPath>{Path("prj:Foo")}) && expectRestored(),
	       "A mid-delete file failure did not restore directory assets and sidecars");
	provider->FailNextDirectoryRemoval();
	Expect(!database.RemoveBatch(Vector<VirtualPath>{Path("prj:Foo")}) && expectRestored(),
	       "A directory deletion failure did not restore source files and nested directories");
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
	const nlohmann::json identities = {{"Version", 1u},
	                                   {"Types", nlohmann::json::array({{{"TypeName", "PlayerMover"},
	                                                                     {"Id", "legacy.player"},
	                                                                     {"Version", 1u},
	                                                                     {"LocalFileId", 42u},
	                                                                     {"Primary", true}}})}};
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
	Expect(static_cast<bool>(database.UpdateScriptIdentities(findScript("prj:Scripts/PlayerMover.cs"), identities)),
	       "Could not persist script type identities");
	Expect(static_cast<bool>(database.SetImporterSettings(Path("prj:Scripts/PlayerMover.cs"), "test.script", 1,
	                                                      nlohmann::json::object())) &&
	           TakeMetadata(database, "prj:Scripts/PlayerMover.cs").scriptTypes == identities,
	       "Changing importer settings discarded script identities");

	Expect(
	    static_cast<bool>(database.Move(Path("prj:Scripts/PlayerMover.cs"), Path("prj:Scripts/Editor/PlayerMover.cs"))),
	    "Could not move a game script into the Editor group");
	auto movedPlayer = findScript("prj:Scripts/Editor/PlayerMover.cs");
	Expect(movedPlayer.guid == playerGuid && movedPlayer.group == ScriptAssemblyGroup::Editor &&
	           movedPlayer.identities == identities,
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
	Expect(copiedPlayer.guid != playerGuid && copiedPlayer.group == ScriptAssemblyGroup::Game &&
	           copiedPlayer.identities.empty(),
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
	           database.PublishImport(source, "ursa.Model", 1, "vulkan", settings, dependencies, firstArtifacts, 17)),
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
	                                                       updatedArtifacts, 17)),
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
		TestBatchMoveAndRemoveCompensation();
		TestDirectoryBatchCompensation(root / "DirectoryTransactions" / "Assets");
		TestScriptDiscoveryAndGrouping(root / "Scripts");
		TestDuplicateGuidRepair(root / "Duplicate");
		TestNativeDuplicateRepairCompensation();
		TestShaderDuplicateRepairCompensation();
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
