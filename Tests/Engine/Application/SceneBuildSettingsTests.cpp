#include <Application/SceneBuildSettings.hpp>
#include <Serialization/JsonResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* path)
{
	return Take(VirtualPath::Parse(path));
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem files;
		Done(files.MountProvider("settings", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
		Expect(!Take(ReadSceneBuildSettings(files)).configured, "A missing BuildSettings file created a default Scene");
		const auto first = ResourceId::Generate(), second = ResourceId::Generate(), third = ResourceId::Generate(),
		           catalogGuid = ResourceId::Generate();
		VirtualPath firstPath = Path("prj:Tests/Menu.ursascene");
		const VirtualPath secondPath = Path("prj:Other/Menu.ursascene");
		const auto thirdPath = Path("prj:Tests/Third.ursascene"), catalogPath = Path("prj:Catalog.ursaasset");
		const auto makeScene = [&](const VirtualPath& path, ResourceId id)
		{
			Done(WriteJsonResource(files, path,
			                       {{"format", "UrsaScene"}, {"version", 1u}, {"id", id.ToString().c_str()}}));
		};
		makeScene(firstPath, first);
		makeScene(secondPath, second);
		ResourceSession::AssetPathResolver resolve = [&](ResourceId guid, UInt64 local) -> Result<VirtualPath>
		{
			if (local != 0)
			{
				return Result<VirtualPath>::Failure("Not a file-level Scene");
			}
			if (guid == first)
			{
				return Result<VirtualPath>::Success(firstPath);
			}
			if (guid == second)
			{
				return Result<VirtualPath>::Success(secondPath);
			}
			if (guid == third)
			{
				return Result<VirtualPath>::Success(thirdPath);
			}
			if (guid == catalogGuid)
			{
				return Result<VirtualPath>::Success(catalogPath);
			}
			return Result<VirtualPath>::Failure("Missing Scene identity");
		};
		nlohmann::json document{
		    {"format", "UrsaBuildSettings"},
		    {"version", 1u},
		    {"scenes",
		     nlohmann::json::array({{{"enabled", true}, {"scene", AssetReferenceToJson({first, 0})}},
		                            {{"enabled", false}, {"scene", AssetReferenceToJson({ResourceId::Generate(), 0})}},
		                            {{"enabled", true}, {"scene", AssetReferenceToJson({second, 0})}}})}};
		const auto source = Path(sceneBuildSettingsSource);
		Done(WriteJsonResource(files, source, document));
		auto settings = Take(ReadSceneBuildSettings(files, resolve));
		Expect(settings.scenes.size() == 2 && Take(settings.Resolve(0)).reference.guid == first &&
		           Take(settings.Resolve(1)).reference.guid == second,
		       "Enabled scene order did not define contiguous Build Indices");
		Expect(!settings.Resolve(-1) && !settings.Resolve(2), "Out-of-range Build Index was accepted");
		String diagnostic;
		Expect(Take(settings.Resolve("Menu", &diagnostic)).reference.guid == first && !diagnostic.empty(),
		       "Duplicate simple names did not use first match and diagnose");
		Expect(Take(settings.Resolve("Assets/Other/Menu.ursascene")).reference.guid == second &&
		           Take(settings.Resolve("prj:Other/Menu")).reference.guid == second,
		       "Full path scene lookup was ambiguous");
		firstPath = Path("prj:Renamed/Start.ursascene");
		makeScene(firstPath, first);
		settings = Take(ReadSceneBuildSettings(files, resolve));
		Expect(Take(settings.Resolve(AssetReference{first, 0})).name == "Start",
		       "Stable scene identity did not survive a path rename");
		Done(files.WriteAtomic(Path(sceneBuildSettingsRuntime), Take(EncodeSceneBuildSettings(settings))));
		auto runtime = Take(ReadSceneBuildSettings(files, {}, true));
		Expect(Take(runtime.Resolve(0)).path == firstPath, "Runtime Scene list lost the resolved path");
		const auto valid = document;
		document["scenes"][0]["scene"] = AssetReferenceToJson({ResourceId::Generate(), 0});
		Done(WriteJsonResource(files, source, document));
		Expect(!ReadSceneBuildSettings(files, resolve), "Missing enabled Scene was accepted");
		document = valid;
		Done(WriteJsonResource(files, firstPath,
		                       {{"format", "UrsaMaterial"}, {"version", 1u}, {"id", first.ToString().c_str()}}));
		Done(WriteJsonResource(files, source, document));
		Expect(!ReadSceneBuildSettings(files, resolve), "Non-Scene build reference was accepted");
		makeScene(firstPath, first);
		document["scenes"][2]["scene"] = AssetReferenceToJson({first, 0});
		Done(WriteJsonResource(files, source, document));
		Expect(!ReadSceneBuildSettings(files, resolve), "Duplicate enabled scene identity was accepted");
		document = valid;
		makeScene(thirdPath, third);
		document["scenes"].push_back({{"enabled", true}, {"scene", AssetReferenceToJson({third, 0})}});
		document["sceneCatalog"] = AssetReferenceToJson({catalogGuid, 1001});
		nlohmann::json catalog = {{"format", "UrsaScriptableAsset"},
		                          {"version", 1},
		                          {"localFileId", 1001},
		                          {"payload",
		                           {{"Fields",
		                             {{"menuScene", AssetReferenceToJson({first, 0})},
		                              {"ids", {"gui", "third"}},
		                              {"titles", {"GUI", "Third"}},
		                              {"descriptions", {"description", "description"}},
		                              {"scenes", nlohmann::json::array({AssetReferenceToJson({second, 0}),
		                                                                AssetReferenceToJson({third, 0})})}}}}}};
		Done(WriteJsonResource(files, catalogPath, catalog));
		Done(WriteJsonResource(files, source, document));
		Expect(Take(ReadSceneBuildSettings(files, resolve)).scenes.size() == 3,
		       "A valid project scene catalog was rejected");
		const auto validCatalog = catalog;
		catalog["payload"]["Fields"]["ids"][1] = "gui";
		Done(WriteJsonResource(files, catalogPath, catalog));
		Expect(!ReadSceneBuildSettings(files, resolve), "Duplicate catalog ID was published");
		catalog = validCatalog;
		catalog["payload"]["Fields"]["scenes"][0] = AssetReferenceToJson({first, 0});
		Done(WriteJsonResource(files, catalogPath, catalog));
		Expect(!ReadSceneBuildSettings(files, resolve), "Catalog and enabled build Scene mismatch was published");
		std::cout << "BuildSettings stable references, enabled indices, names, rename and invalid targets passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
