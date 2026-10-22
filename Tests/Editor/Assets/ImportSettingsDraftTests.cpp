#include <Assets/ImportSettingsDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
using Json = nlohmann::json;

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

void TestDraft()
{
	VirtualFileSystem files;
	auto sources = std::make_shared<MemoryFileProvider>();
	Done(files.MountProvider("prj", sources, MountAccess::ReadWrite));
	const auto source = Take(VirtualPath::Parse("prj:Example.shape"));
	const auto sidecar = Take(VirtualPath::Parse("prj:Example.shape.meta"));
	Done(files.WriteAtomic(source, {1, 2, 3}));
	auto database = Take(AssetDatabase::Open(files));
	Done(database->Refresh());
	const auto initialBytes = Take(files.Read(sidecar));
	const auto guid = Take(database->Metadata(source)).guid;
	ImportScriptType type{ResourceId::Generate(), 5, "Tests.Importer"};
	const auto id = Take(type.StableIdentity());
	Expect(Take(ImportScriptType::FromIdentity(id)).sourceGuid == type.sourceGuid &&
	           !ImportScriptType::FromIdentity("script:broken:0") && !ImportScriptType::FromIdentity(id + "x"),
	       "Persisted script importer identity was not validated");
	AssetImporterDescriptor descriptor{id, 4, {".shape"}, type};
	const auto validate = [](const Json& settings)
	{
		return settings.contains("maxSize") && settings["maxSize"].is_number_integer() &&
		               settings["maxSize"].get<Int64>() > 0 && settings["maxSize"].get<Int64>() <= 1024
		           ? Result<void>{}
		           : Result<void>::Failure("Invalid maxSize");
	};
	auto draft = Take(ImportSettingsDraft::Open(*database, source, descriptor, {{"maxSize", 64}}, validate));
	Expect(!draft.HasModified(), "Default settings appeared as an unsaved edit");
	auto effective = draft.EffectiveSettings();
	effective["maxSize"] = 1;
	Expect(draft.PersistentSettings()["maxSize"] == 64, "Preprocessing changed persistent settings");
	Done(draft.Edit({{"maxSize", 128}}));
	Expect(draft.HasModified() && Take(files.Read(sidecar)) == initialBytes &&
	           draft.EffectiveSettings()["maxSize"] == 64,
	       "Editing settings changed the metadata or effective import baseline");
	Done(draft.Revert());
	Expect(!draft.HasModified() && draft.Settings()["maxSize"] == 64 && Take(files.Read(sidecar)) == initialBytes,
	       "Revert wrote settings instead of discarding the draft");
	Done(draft.Edit({{"maxSize", 2048}}));
	Expect(!draft.Apply() && draft.HasModified() && Take(files.Read(sidecar)) == initialBytes,
	       "Invalid settings were persisted or lost from the draft");
	Done(draft.Edit({{"maxSize", 128}}));
	Expect(Take(draft.Apply()) && !draft.HasModified() && !Take(draft.Apply()),
	       "Apply did not commit once and reset the draft baseline");
	const auto saved = Take(database->Metadata(source));
	Expect(saved.guid == guid && saved.importer == id && saved.importerVersion == 4 &&
	           saved.importSettings["maxSize"] == 128,
	       "Typed settings round trip changed importer identity or source GUID");
	descriptor.scriptType->clrTypeName = "Renamed.Importer";
	auto renamed = Take(ImportSettingsDraft::Open(*database, source, descriptor, Json::object(), validate));
	Expect(renamed.Settings()["maxSize"] == 128, "CLR rename lost the persisted settings");
	Done(renamed.Edit({{"maxSize", 512}}));
	Done(database->SetImporterSettings(source, id, 4, {{"maxSize", 256}}));
	const auto externalBytes = Take(files.Read(sidecar));
	Expect(!renamed.Apply() && renamed.HasModified() && Take(files.Read(sidecar)) == externalBytes,
	       "A stale draft overwrote externally saved settings");
	Done(renamed.Revert());
	Expect(!renamed.HasModified() && renamed.Settings()["maxSize"] == 256,
	       "Revert did not restore the latest saved settings");
	VirtualFileSystem readOnly;
	Done(readOnly.MountProvider("prj", sources, MountAccess::ReadOnly));
	auto readOnlyDatabase = Take(AssetDatabase::Open(readOnly));
	Done(readOnlyDatabase->Refresh());
	auto readOnlyDraft =
	    Take(ImportSettingsDraft::Open(*readOnlyDatabase, source, descriptor, Json::object(), validate));
	Done(readOnlyDraft.Edit({{"maxSize", 512}}));
	Expect(!readOnlyDraft.Apply() && readOnlyDraft.HasModified() && Take(files.Read(sidecar)) == externalBytes,
	       "Read-only metadata write discarded the draft or changed persistent bytes");
	Expect(!database->SetImporterSettings(source, "script:invalid", 1, Json::object()) &&
	           !database->SetImporterSettings(source, id, 0, Json::object()),
	       "Invalid importer identity/version was saved");
	const auto writeMetadata = [&](const Json& metadata)
	{
		const auto text = metadata.dump();
		Done(files.WriteAtomic(sidecar, Vector<UInt8>(text.data(), text.data() + text.size())));
	};
	const auto currentMetadata = Json::parse(externalBytes.begin(), externalBytes.end());
	auto invalid = currentMetadata;
	invalid["version"] = UInt32{0};
	writeMetadata(invalid);
	const auto invalidBytes = Take(files.Read(sidecar));
	Expect(!database->Refresh() && Take(files.Read(sidecar)) == invalidBytes,
	       "Unsupported metadata version was rewritten or silently assigned a new GUID");
	invalid = currentMetadata;
	invalid["importer"] = "script:invalid:0";
	writeMetadata(invalid);
	Expect(!database->Refresh(), "Malformed stable script importer metadata was accepted");
	writeMetadata(currentMetadata);
	Done(database->Refresh());
	Expect(Take(database->Metadata(source)).guid == guid, "Metadata recovery lost the original asset GUID");
}
} // namespace

int main()
{
	try
	{
		TestDraft();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
