#include <Assets/AssetDatabase.hpp>
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
		const auto copiedPath = Take(VirtualPath::Parse("prj:Copy.ursaasset"));
		Done(database->Copy(destination, copiedPath));
		const auto copiedGuid = Take(database->Metadata(copiedPath)).guid;
		Expect(copiedGuid != guid &&
		           Take(LoadScriptableAssetSource(files, copiedPath, copiedGuid)).SourceGuid() == copiedGuid,
		       "Copying a ScriptableAsset duplicated its persistent identity");
		std::cout << "ScriptableAsset source metadata/move/copy tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
