#include <Assets/MeshComponentMigration.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
using Json = nlohmann::json;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Metadata(VirtualFileSystem& files, const VirtualPath& path, ResourceId id)
{
	const auto text = Json{{"guid", ToStdString(id.ToString())}}.dump();
	Done(files.WriteAtomic(Take(VirtualPath::Parse(path.ToString() + ".meta")),
	                       Vector<UInt8>(text.data(), text.data() + text.size())));
}

void Run()
{
	VirtualFileSystem files;
	Done(files.Mount("ursa", std::filesystem::u8path(URSA_TEST_BUILTIN_SOURCE), MountAccess::ReadOnly));
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto materialId = ResourceId::Generate();
	const auto materialPath = Take(VirtualPath::Parse("prj:TintSource.ursamat"));
	MaterialInstance material(MaterialType::Unlit);
	Done(material.SetBaseColor({0.8f, 0.4f, 0.2f, 0.5f}));
	Done(SaveMaterialResource(files, materialPath, materialId, material, {}));
	Metadata(files, materialPath, materialId);
	auto resources = Take(ResourceSession::Open(files));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	Scene empty;
	const auto id = ResourceId::Generate();
	auto source = Take(EncodeSceneDocument("Legacy.ursascene", id, empty, registry));
	auto root = GameObject::Create("Legacy");
	ArchiveWriter writer;
	Done(root->Serialize(writer));
	auto node = Take(ArchiveToJson(writer));
	node["parentId"] = nullptr;
	const auto rendererId = ResourceId::Generate();
	const auto secondRendererId = ResourceId::Generate();
	node["components"] = Json::array(
	    {{{"id", ToStdString(rendererId.ToString())},
	      {"type", "ursa.BoxRender"},
	      {"version", 1u},
	      {"enabled", true},
	      {"data", {{"color", {0.5f, 0.25f, 0.75f, 0.8f}}, {"material", AssetReferenceToJson({materialId, 0})}}}},
	     {{"id", ToStdString(secondRendererId.ToString())},
	      {"type", "ursa.BoxRender"},
	      {"version", 1u},
	      {"enabled", false},
	      {"data", {{"color", {1, 1, 1, 1}}, {"material", nullptr}}}}});
	source["gameObjects"].push_back(node);
	auto converted = Take(MeshComponentMigration::Convert(source, *resources));
	Expect(converted["gameObjects"].size() == 2 &&
	           converted["gameObjects"][0]["components"][1]["id"] == ToStdString(rendererId.ToString()) &&
	           converted["gameObjects"][1]["components"][1]["id"] == ToStdString(secondRendererId.ToString()),
	       "Migration lost original component IDs or additional Renderer");
	ComponentLoadContext context(*resources);
	auto restored = Take(DecodeSceneDocument("Converted.ursascene", converted, registry, context));
	auto first = restored.scene->RootGameObjects().front();
	auto* renderer = first->FindComponent<MeshRendererComponent>();
	const auto color = renderer->SharedMaterial()->Parameters().baseColor;
	Expect(std::abs(color.red - 0.4f) < 0.0001f && std::abs(color.green - 0.1f) < 0.0001f &&
	           std::abs(color.blue - 0.15f) < 0.0001f && std::abs(color.alpha - 0.4f) < 0.0001f &&
	           first->FindComponent<MeshFilterComponent>(),
	       "Migration did not multiply old color by the original material");
	Expect(Take(MeshComponentMigration::Convert(converted, *resources)) == converted,
	       "Document migration was not idempotent");
	Json prefab = source;
	prefab["format"] = "UrsaPrefab";
	prefab.erase("sceneGraphVersion");
	prefab["prefabGraphVersion"] = prefabGraphFormatVersion;
	prefab["id"] = ToStdString(ResourceId::Generate().ToString());
	Map<String, Json> prefabs{{ToUrsaString(prefab["id"].get<std::string>()), prefab}};
	auto linked = Take(EncodeSceneDocument("Linked.ursascene", ResourceId::Generate(), empty, registry));
	linked["instances"] = Json::array(
	    {{{"id", ToStdString(ResourceId::Generate().ToString())},
	      {"prefab", prefab["id"]},
	      {"parentId", nullptr},
	      {"overrides",
	       {{"properties", {{ToStdString(rendererId.ToString()), {{"/data/color/0", 0.9f}, {"/enabled", false}}}}}}}}});
	auto linkedConversion = Take(MeshComponentMigration::Convert(linked, *resources, prefabs));
	const auto& patch = linkedConversion["instances"][0]["overrides"];
	Expect(patch["localResources"].size() == 1 &&
	           !patch["properties"][ToStdString(rendererId.ToString())].contains("/data/color/0") &&
	           patch["properties"][ToStdString(rendererId.ToString())]["/enabled"] == false,
	       "Migration dropped or left an old Prefab property override");
	auto structure = linked;
	const auto addedObjectId = ResourceId::Generate();
	const auto addedComponentId = ResourceId::Generate();
	Json added = node;
	added["id"] = ToStdString(addedObjectId.ToString());
	added["parentId"] = prefab["gameObjects"][0]["id"];
	added["components"] = Json::array();
	auto addition = node["components"][0];
	addition["id"] = ToStdString(addedComponentId.ToString());
	structure["instances"][0]["overrides"]["addedGameObjects"] = Json::array({added});
	structure["instances"][0]["overrides"]["addedComponents"] =
	    Json::array({{{"owner", added["id"]}, {"component", addition}}});
	const auto structureConversion = Take(MeshComponentMigration::Convert(structure, *resources, prefabs));
	const auto& structurePatch = structureConversion["instances"][0]["overrides"];
	Expect(structurePatch["addedGameObjects"].size() == 1 && structurePatch["addedComponents"].size() == 2,
	       "Structural override did not create a paired Filter/Renderer");
	bool retainedId = false;
	for (const auto& record : structurePatch["addedComponents"])
	{
		retainedId |= record["component"]["id"] == ToStdString(addedComponentId.ToString()) &&
		              record["component"]["type"] == "ursa.MeshRenderer";
	}
	Expect(retainedId, "Structural override conversion changed the old component reference identity");
	const auto path = Take(VirtualPath::Parse("prj:Legacy.ursascene"));
	Done(WriteJsonResource(files, path, source));
	Metadata(files, path, id);
	const auto original = Take(files.Read(path));
	Expect(Take(MeshComponentMigration::Migrate(files, *resources, true)).documents == 1 &&
	           Take(files.Read(path)) == original,
	       "Dry run modified an authored scene");
	auto bad = source;
	bad["gameObjects"][0]["components"][0]["data"]["color"] = {1, 2};
	Done(WriteJsonResource(files, path, bad));
	const auto broken = Take(files.Read(path));
	Expect(!MeshComponentMigration::Migrate(files, *resources) && Take(files.Read(path)) == broken,
	       "Failed migration overwrote the original file");
	Done(files.WriteAtomic(path, original));
	auto report = Take(MeshComponentMigration::Migrate(files, *resources));
	Expect(report.documents == 1 && !report.backupRoot.empty() &&
	           Take(files.Read(Take(VirtualPath::Parse(report.backupRoot + "/Legacy.ursascene")))) == original,
	       "Migration did not retain recoverable original bytes");
	Expect(Take(MeshComponentMigration::Migrate(files, *resources)).documents == 0,
	       "Repeated project migration rewrote current files");
}
} // namespace

int main()
{
	try
	{
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
