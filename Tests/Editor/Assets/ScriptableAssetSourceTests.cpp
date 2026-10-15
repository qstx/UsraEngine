#include <Assets/AssetDatabase.hpp>
#include <Core/StringInterop.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

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
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		const auto source = Take(VirtualPath::Parse("prj:Settings.ursaasset"));
		ScriptableAsset asset;
		const nlohmann::json payload = {
		    {"ScriptType", "test.Settings"}, {"Version", 1}, {"Fields", {{"exposure", 2.5}, {"unknown", {1, 2, 3}}}}};
		Done(asset.SetDescription(ResourceId::Generate(), 1, "test.Settings", 1, payload));
		Done(SaveScriptableAssetSource(files, source, asset));
		auto database = Take(Editor::AssetDatabase::Open(files));
		Done(database->Refresh());
		const auto guid = Take(database->Metadata(source)).guid;
		Expect(bool(guid) && Take(files.Exists(Take(VirtualPath::Parse("prj:Settings.ursaasset.meta")))),
		       "ScriptableAsset discovery did not create its metadata");
		auto loaded = Take(LoadScriptableAssetSource(files, source, guid));
		Expect(loaded.SourceGuid() == guid && loaded.Payload() == payload,
		       "ScriptableAsset source did not use its actual metadata identity");
		const auto destination = Take(VirtualPath::Parse("prj:Moved.ursaasset"));
		Done(database->Move(source, destination));
		Expect(Take(database->Metadata(destination)).guid == guid && Take(database->Resolve(guid)) == destination,
		       "Moving a ScriptableAsset changed its GUID");
		const auto reopened = Take(LoadScriptableAssetSource(files, destination, guid));
		Expect(reopened.Payload() == payload, "Moving a source lost unknown fields");
		ScriptableAsset resourceBindings;
		const auto materialGuid = ResourceId::Generate();
		const auto meshGuid = ResourceId::Generate();
		auto bindingPayload = payload;
		for (const auto& entry : {std::make_pair("Material", materialGuid), std::make_pair("Mesh", meshGuid)})
		{
			bindingPayload["Fields"][entry.first] = {
			    {"$ursaReference",
			     {{"Kind", entry.first},
			      {"Resource",
			       {{"kind", "asset"},
			        {"asset", {{"guid", ToStdString(entry.second.ToString())}, {"localFileId", 0u}}}}}}}};
		}
		Done(resourceBindings.SetDescription(guid, 1, "test.Settings", 1, bindingPayload));
		const auto resourceDependencies = Take(ScriptableAssetDependencies(resourceBindings));
		Expect(resourceDependencies.size() == 2 && resourceDependencies[0].guid == materialGuid &&
		           resourceDependencies[1].guid == meshGuid,
		       "Persistent Material/Mesh bindings did not publish their asset dependencies");
		const auto copiedPath = Take(VirtualPath::Parse("prj:Copy.ursaasset"));
		Done(database->Copy(destination, copiedPath));
		const auto copiedGuid = Take(database->Metadata(copiedPath)).guid;
		Expect(copiedGuid != guid &&
		           Take(LoadScriptableAssetSource(files, copiedPath, copiedGuid)).SourceGuid() == copiedGuid,
		       "Copying a ScriptableAsset duplicated its persistent identity");
		const auto defaultsPath = Take(VirtualPath::Parse("prj:Defaults.ursaasset"));
		Done(files.WriteAtomic(defaultsPath, {}));
		Done(database->Refresh());
		ScriptTypeDescriptor settingsType;
		settingsType.sourceGuid = ResourceId::Generate();
		settingsType.scriptType = "test.DefaultSettings";
		settingsType.localFileId = 42;
		settingsType.version = 3;
		settingsType.kind = ScriptTypeKind::ScriptableObject;
		settingsType.group = ScriptAssemblyGroup::Game;
		settingsType.fields.push_back({"strength", "FloatingPoint", "Unsupported", "System.Single", "2.5"});
		settingsType.fields.push_back({"tint", "Vector4", "Unsupported", "Ursa.Vector4", "[1,0.5,0.25,1]"});
		settingsType.fields.push_back({"shared", "ScriptableObjectReference", "Unsupported", "test.Settings", "null"});
		const auto defaultsGuid = Take(database->Metadata(defaultsPath)).guid;
		auto created = Take(CreateScriptableAssetWithDefaults(defaultsGuid, settingsType));
		Expect(created.Payload().at("Fields").at("strength") == 2.5 &&
		           created.Payload().at("Fields").at("tint") == nlohmann::json::array({1.0, 0.5, 0.25, 1.0}) &&
		           created.Payload().at("FieldTypes").at("strength").at("Kind") == "FloatingPoint",
		       "CreateAssetMenu defaults or versioned field schema were not initialized");
		Done(SaveScriptableAssetSource(files, defaultsPath, created));
		auto reopenedDefaults = Take(LoadScriptableAssetSource(files, defaultsPath, defaultsGuid));
		Expect(reopenedDefaults.Payload() == created.Payload(),
		       "A newly created ScriptableAsset did not save and reopen its default fields");
		nlohmann::json editedPayload = reopenedDefaults.Payload();
		editedPayload["Fields"]["strength"] = 3.75;
		editedPayload["Fields"]["shared"] = nlohmann::json::object(
		    {{"$ursaReference",
		      nlohmann::json::object({{"Kind", "ScriptableObject"},
		                              {"Id", ""},
		                              {"Resource", nlohmann::json::object({{"guid", ToStdString(guid.ToString())},
		                                                                   {"localFileId", 1u}})}})}});
		ScriptableAsset edited;
		Done(edited.SetDescription(defaultsGuid, settingsType.localFileId, settingsType.scriptType,
		                           settingsType.version, std::move(editedPayload)));
		Done(SaveScriptableAssetSource(files, defaultsPath, edited));
		auto savedEdit = Take(LoadScriptableAssetSource(files, defaultsPath, defaultsGuid));
		Expect(savedEdit.Payload().at("Fields").at("strength") == 3.75 &&
		           savedEdit.Payload().at("Fields").at("shared").at("$ursaReference").at("Resource").at("guid") ==
		               ToStdString(guid.ToString()),
		       "An edited ScriptableAsset field or selected resource reference was not saved and restored");
		const auto invalidSourceBytes = Take(files.Read(defaultsPath));
		nlohmann::json invalidPayload = savedEdit.Payload();
		invalidPayload["Fields"]["shared"] = nlohmann::json::object(
		    {{"$ursaReference", nlohmann::json::object({{"Kind", "GameObject"}, {"Id", "temporary"}})}});
		ScriptableAsset invalid;
		Expect(!invalid.SetDescription(defaultsGuid, settingsType.localFileId, settingsType.scriptType,
		                               settingsType.version, std::move(invalidPayload)) &&
		           Take(files.Read(defaultsPath)) == invalidSourceBytes,
		       "An invalid scene reference altered the saved source instead of preserving its data");
		std::cout << "ScriptableAsset source metadata/move/copy tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
