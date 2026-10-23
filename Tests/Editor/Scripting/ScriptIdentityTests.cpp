#include <Assets/ScriptBuildService.hpp>
#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
using Json = nlohmann::json;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

VirtualPath Path(const String& value)
{
	return Take(VirtualPath::Parse(value));
}

void Write(VirtualFileSystem& filesystem, const String& path, const String& text)
{
	Done(filesystem.WriteAtomic(Path(path), Vector<UInt8>(text.begin(), text.end())));
}

ScriptTypeDescriptor Find(const ScriptBuildService& service, const char* name)
{
	for (const auto& type : service.ScriptTypes())
	{
		if (type.clrTypeName == name)
		{
			return type;
		}
	}
	throw std::runtime_error(std::string("Missing script type: ") + name);
}

void SetIdentity(AssetDatabase& database, const VirtualPath& path, const String& typeName, const String& id,
				 Ursa::UInt32 version, Ursa::UInt64 localFileId)
{
	for (const ScriptSourceAsset& source : database.ScriptSources())
	{
		if (source.path != path) continue;
		auto identities = source.identities;
		if (!identities.contains("Version")) identities["Version"] = 1;
		if (!identities.contains("Types")) identities["Types"] = Json::array();
		identities["Types"].push_back({{"TypeName", ToStdString(typeName)}, {"Id", ToStdString(id)},
										   {"Version", version}, {"LocalFileId", localFileId}, {"Primary", true}});
		Done(database.UpdateScriptIdentities(source, identities));
		return;
	}
	throw std::runtime_error("Missing script source for identity fixture");
}
} // namespace

int main(int argumentCount, char** arguments)
{
	try
	{
		Expect(argumentCount == 6, "ScriptIdentityTests requires the configured script compiler inputs");
		const auto root = std::filesystem::temp_directory_path() /
		                  ("ScriptIdentityTests-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root / "Assets");
		std::filesystem::create_directories(root / "Assets" / "Editor");
		std::filesystem::create_directories(root / "Library");
		VirtualFileSystem filesystem;
		Done(filesystem.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite));
		Done(filesystem.Mount(editorLibraryScheme, root / "Library", MountAccess::ReadWrite));
		Write(filesystem, "prj:Mover.cs",
		      "using Ursa; namespace Old; "
		      "public sealed class Mover : Behaviour { public int value = 7; }");
		Write(filesystem, "prj:Plain.cs", "public sealed class Plain : Ursa.Behaviour { public int value = 3; }");
		Write(filesystem, "prj:Settings.cs",
		      "[Ursa.CreateAssetMenu(MenuName=\"Test/Settings\")] "
		      "public sealed class Settings : Ursa.ScriptableObject { public int value = 5; }");
		Write(filesystem, "prj:Editor/FormatImporter.cs",
		      "using Ursa.Editor; [ScriptedImporter(2, \"ursatestformat\")] "
		      "public sealed class FormatImporter : ScriptedImporter { public override void "
		      "OnImportAsset(AssetImportContext context) {} }");
		Write(filesystem, "prj:Editor/TextureRules.cs",
		      "public sealed class TextureRules : Ursa.Editor.AssetPostprocessor { public override void "
		      "OnPreprocessTexture() {} }");
		auto database = Take(AssetDatabase::Open(filesystem));
		Done(database->Refresh());
		SetIdentity(*database, Path("prj:Mover.cs"), "Old.Mover", "legacy.mover", 2, 41001);
		const auto openService = [&]()
		{
			return Take(
			    ScriptBuildService::Open(filesystem, *database, std::filesystem::u8path(arguments[1]),
			                             std::filesystem::u8path(arguments[2]), std::filesystem::u8path(arguments[3]),
			                             std::filesystem::u8path(arguments[4]), std::filesystem::u8path(arguments[5])));
		};
		auto service = openService();
		Take(service->GenerateProjects());
		const auto legacy = Find(*service, "Old.Mover, Ursa.Game");
		const auto plain = Find(*service, "Plain, Ursa.Game");
		const auto settings = Find(*service, "Settings, Ursa.Game");
		const auto importer = Find(*service, "FormatImporter, Ursa.EditorScripts");
		const auto processor = Find(*service, "TextureRules, Ursa.EditorScripts");
		Expect(processor.kind == ScriptTypeKind::AssetPostprocessor && processor.group == ScriptAssemblyGroup::Editor,
		       "AssetPostprocessor was not discovered as a stable Editor type");
		Expect(importer.kind == ScriptTypeKind::ScriptedImporter && importer.group == ScriptAssemblyGroup::Editor,
		       "ScriptedImporter was not discovered as a stable Editor type");
		Expect(legacy.scriptType == "legacy.mover" && legacy.version == 2 &&
		           settings.kind == ScriptTypeKind::ScriptableObject &&
		           settings.createAssetMenuName == "Test/Settings" &&
		           !Take(database->Metadata(Path("prj:Plain.cs"))).scriptTypes.empty(),
		       "Automatic discovery did not persist identities or retain legacy IDs/versions");
		const auto originalMetadata = Take(filesystem.ReadText(Path("prj:Plain.cs.meta")));
		Take(service->GenerateProjects());
		Expect(Take(filesystem.ReadText(Path("prj:Plain.cs.meta"))) == originalMetadata,
		       "Repeated type discovery changed script metadata");

		Done(database->Move(Path("prj:Mover.cs"), Path("prj:RenamedMover.cs")));
		Done(database->Move(Path("prj:Editor/FormatImporter.cs"), Path("prj:Editor/RenamedFormat.cs")));
		Done(database->Move(Path("prj:Editor/TextureRules.cs"), Path("prj:Editor/RenamedRules.cs")));
		Write(filesystem, "prj:Editor/RenamedRules.cs",
		      "public sealed class RenamedRules : Ursa.Editor.AssetPostprocessor { public override uint GetVersion() "
		      "=> 4; public override void OnPreprocessTexture() {} }");
		Write(filesystem, "prj:Editor/RenamedFormat.cs",
		      "using Ursa.Editor; [ScriptedImporter(3, \"ursatestformat\")] "
		      "public sealed class RenamedFormatImporter : ScriptedImporter { public override void "
		      "OnImportAsset(AssetImportContext context) {} }");
		Write(filesystem, "prj:RenamedMover.cs",
		      "namespace New; public sealed class RenamedMover : Ursa.Behaviour { public int value = 7; }");
		Write(filesystem, "prj:Plain.cs", "namespace New; public sealed class RenamedPlain : Ursa.Behaviour {}");
		Write(filesystem, "prj:Settings.cs",
		      "[Ursa.CreateAssetMenu(MenuName=\"Test/Settings\")] "
		      "public sealed class RenamedSettings : Ursa.ScriptableObject { public int value = 5; }");
		Take(service->GenerateProjects());
		const auto renamed = Find(*service, "New.RenamedMover, Ursa.Game");
		const auto renamedPlain = Find(*service, "New.RenamedPlain, Ursa.Game");
		const auto renamedSettings = Find(*service, "RenamedSettings, Ursa.Game");
		const auto renamedImporter = Find(*service, "RenamedFormatImporter, Ursa.EditorScripts");
		const auto renamedProcessor = Find(*service, "RenamedRules, Ursa.EditorScripts");
		Expect(renamedProcessor.sourceGuid == processor.sourceGuid &&
		           renamedProcessor.localFileId == processor.localFileId &&
		           renamedProcessor.scriptType == processor.scriptType &&
		           renamedProcessor.kind == ScriptTypeKind::AssetPostprocessor,
		       "Postprocessor rename or rule version changed its stable script identity");
		Expect(renamedImporter.sourceGuid == importer.sourceGuid &&
		           renamedImporter.localFileId == importer.localFileId &&
		           renamedImporter.scriptType == importer.scriptType &&
		           renamedImporter.kind == ScriptTypeKind::ScriptedImporter,
		       "Importer class/file rename or importer version changed the stable script identity");
		Expect(renamed.scriptType == legacy.scriptType && renamed.sourceGuid == legacy.sourceGuid &&
		           renamed.localFileId == legacy.localFileId && renamed.version == legacy.version &&
		           renamedPlain.scriptType == plain.scriptType && renamedPlain.localFileId == plain.localFileId &&
		           renamedSettings.scriptType == settings.scriptType &&
		           renamedSettings.localFileId == settings.localFileId,
		       "Removing attributes or renaming file/class/namespace changed a persisted script identity");
		service.reset();
		database.reset();
		std::filesystem::remove_all(root / "Library");
		std::filesystem::create_directories(root / "Library");
		database = Take(AssetDatabase::Open(filesystem));
		service = openService();
		Take(service->GenerateProjects());
		Expect(Find(*service, "New.RenamedMover, Ursa.Game").scriptType == legacy.scriptType,
		       "Clearing Library lost the legacy identity retained in .meta");

		Done(database->Copy(Path("prj:Plain.cs"), Path("prj:Copy.cs")));
		Write(filesystem, "prj:Copy.cs", "public sealed class Copy : Ursa.Behaviour {}");
		Take(service->GenerateProjects());
		const auto copy = Find(*service, "Copy, Ursa.Game");
		Expect(copy.sourceGuid != plain.sourceGuid && copy.scriptType != plain.scriptType &&
		           copy.localFileId != plain.localFileId,
		       "Copied script reused the original type identity");
		const auto built = Take(service->Build(ScriptAssemblyGroup::Game));
		Expect(built.succeeded, "Attribute-free Game/ScriptableObject scripts did not compile");
		const auto fingerprint = Take(service->SourceFingerprint());
		auto source = database->ScriptSources().front();
		for (const auto& candidate : database->ScriptSources())
		{
			if (candidate.path == Path("prj:Copy.cs"))
			{
				source = candidate;
			}
		}
		auto editedIdentity = source.identities;
		editedIdentity["Types"][0]["Version"] = 2u;
		Done(database->UpdateScriptIdentities(source, editedIdentity));
		Expect(!database->UpdateScriptIdentities(source, source.identities) &&
		           Take(service->SourceFingerprint()) != fingerprint &&
		           !Take(service->IsBuildCurrent(ScriptAssemblyGroup::Game)),
		       "Metadata edits did not invalidate the formal build or stale metadata overwrote a newer revision");

		const auto metadataBeforeError = Take(filesystem.ReadText(Path("prj:Copy.cs.meta")));
		Write(filesystem, "prj:Copy.cs", "public sealed class Copy : MissingType {}");
		Expect(!service->GenerateProjects() &&
		           Take(filesystem.ReadText(Path("prj:Copy.cs.meta"))) == metadataBeforeError,
		       "Semantic failure rewrote persistent type metadata");
		Write(filesystem, "prj:Copy.cs", "public sealed class Copy : Ursa.Behaviour {}");
		Take(service->GenerateProjects());
		const auto beforeDuplicate = Take(filesystem.ReadText(Path("prj:Copy.cs.meta")));
		auto duplicate = Json::parse(ToStdString(beforeDuplicate));
		duplicate["scriptTypes"]["Types"].push_back(duplicate["scriptTypes"]["Types"][0]);
		Write(filesystem, "prj:Copy.cs.meta", ToUrsaString(duplicate.dump()));
		Expect(!service->GenerateProjects() && !service->LastDiagnostics().empty(),
		       "Duplicate metadata identities were accepted without a diagnostic");
		Write(filesystem, "prj:Copy.cs.meta", beforeDuplicate);
		Take(service->GenerateProjects());
		std::cout << "Metadata identities: stable rename, Library recovery, copy, stale writes and "
		             "errors passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
