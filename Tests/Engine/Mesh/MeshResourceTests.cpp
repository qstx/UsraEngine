#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Serialization/MeshResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

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

MeshPtr Triangle()
{
	MeshData data;
	data.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	data.indices = {0, 1, 2};
	return Take(Mesh::Create(std::move(data)));
}

void Resources(VirtualFileSystem& files)
{
	SerializationContext context;
	for (bool binary : {false, true})
	{
		const auto path = Take(VirtualPath::Parse(binary ? "prj:Binary.ursamesh" : "prj:Json.ursamesh"));
		const auto id = ResourceId::Generate();
		auto mesh = Triangle();
		Done(SaveMeshResource(files, path, id, *mesh, binary));
		auto loaded = Take(LoadMeshResource(files, path, context));
		Expect(loaded.id == id && loaded.mesh->AssetIdentity().guid == id &&
		           loaded.mesh->Snapshot()->data.positions.size() == 3,
		       "Mesh asset roundtrip failed");
		Expect(!loaded.mesh->Destroy(), "Persistent Mesh could be destroyed");
		auto bytes = Take(files.Read(path));
		if (binary)
		{
			bytes[28] ^= 1;
		}
		else
		{
			bytes[0] = '!';
		}
		Done(files.WriteAtomic(path, bytes));
		Expect(!LoadMeshResource(files, path, context), "Damaged Mesh was accepted");
	}
	const auto oversized = Take(VirtualPath::Parse("prj:Oversized.ursamesh"));
	Done(SaveMeshResource(files, oversized, ResourceId::Generate(), *Triangle(), false));
	auto jsonBytes = Take(files.Read(oversized));
	auto document = nlohmann::json::parse(jsonBytes.begin(), jsonBytes.end());
	document["data"]["vertexCount"] = Mesh::maxDataBytes;
	const auto invalid = document.dump();
	Done(files.WriteAtomic(oversized, Vector<UInt8>(invalid.data(), invalid.data() + invalid.size())));
	Expect(!LoadMeshResource(files, oversized, context), "Oversized decoded Mesh was accepted");
	const auto wrong = Take(VirtualPath::Parse("prj:Wrong.ursamat"));
	const auto wrongText = nlohmann::json{{"format", "UrsaMaterial"},
	                                      {"version", UInt32{1}},
	                                      {"id", ToStdString(ResourceId::Generate().ToString())}}
	                           .dump();
	Done(files.WriteAtomic(wrong, Vector<UInt8>(wrongText.data(), wrongText.data() + wrongText.size())));
	Expect(!LoadMeshResource(files, wrong, context), "Wrong resource kind was accepted as Mesh");
	const auto source = Take(VirtualPath::Parse("prj:Shared.ursamesh"));
	const auto id = ResourceId::Generate();
	Done(SaveMeshResource(files, source, id, *Triangle()));
	const auto metadata = Take(VirtualPath::Parse("prj:Shared.ursamesh.meta"));
	const String text = "{\"guid\":\"" + id.ToString() + "\"}";
	Done(files.WriteAtomic(metadata, Vector<UInt8>(text.data(), text.data() + text.size())));
	auto session = Take(ResourceSession::Open(files));
	auto loaded = Take(session->LoadMesh(id, 0));
	Expect(Take(session->LoadMesh(id, 0)) == loaded, "Session did not reuse Mesh identity");
	auto independent = Take(loaded->Clone());
	const auto materialId = ResourceId::Generate();
	const auto materialPath = Take(VirtualPath::Parse("prj:Isolation.ursamat"));
	MaterialInstance authoredMaterial(MaterialType::Unlit);
	Done(authoredMaterial.SetBaseColor({0.2f, 0.3f, 0.4f, 1}));
	Done(SaveMaterialResource(files, materialPath, materialId, authoredMaterial, {}));
	const auto materialMetadata = nlohmann::json{{"guid", ToStdString(materialId.ToString())}}.dump();
	Done(files.WriteAtomic(Take(VirtualPath::Parse(materialPath.ToString() + ".meta")),
	                       Vector<UInt8>(materialMetadata.data(), materialMetadata.data() + materialMetadata.size())));
	Done(session->Index().Rebuild());
	auto sharedMaterial = Take(session->LoadMaterial(materialId));
	auto isolated = Take(session->Isolated());
	auto isolatedMaterial = Take(isolated->LoadMaterial(materialId));
	Done(isolatedMaterial->SetBaseColor({0.9f, 0.2f, 0.1f, 1}));
	Expect(isolatedMaterial != sharedMaterial && sharedMaterial->Parameters().baseColor.red == 0.2f,
	       "Run shared Material edit leaked into the editing resource session");
	auto isolatedMesh = Take(isolated->LoadMesh(id, 0));
	Expect(isolatedMesh != loaded && isolatedMesh->AssetIdentity().guid == id,
	       "Isolated session shared mutable asset Mesh");
	Done(isolatedMesh->SetVertices({{2, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
	Expect(loaded->Snapshot()->data.positions[0].x == 0, "Isolated shared Mesh leaked into edit resources");
	auto changed = Triangle();
	Done(changed->SetVertices({{3, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
	Done(SaveMeshResource(files, source, id, *changed));
	const auto beforeVersion = loaded->Version();
	Done(session->ReloadImportedResources(files));
	Expect(Take(session->LoadMesh(id, 0)) == loaded && loaded->Version() == beforeVersion + 1 &&
	           loaded->Snapshot()->data.positions[0].x == 3,
	       "Reload did not update existing Mesh identity/version");
	Expect(independent->Snapshot()->data.positions[0].x == 0, "Reload overwrote independent Mesh clone");
	Expect(!session->LoadMesh(id, 1), "Standalone Mesh accepted sub-asset ID");
	Expect(!LoadMeshResource(files, Take(VirtualPath::Parse("prj:Unknown.ursamat")), context),
	       "Wrong Mesh kind accepted");
}

void Documents(VirtualFileSystem& files)
{
	auto session = Take(ResourceSession::Open(files));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	Scene scene;
	auto root = Take(scene.CreateGameObject("Root"));
	auto child = Take(scene.CreateGameObject("Child", root));
	auto independent = Take(scene.CreateGameObject("Independent", root));
	auto mesh = Triangle();
	auto material = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets, MaterialType::Unlit);
	Done(material->SetBaseColor({0.2f, 0.8f, 0.5f, 1}));
	for (const auto& object : {root, child, independent})
	{
		auto* filter = Take(object->AddComponent<MeshFilterComponent>());
		Done(filter->SetSharedMesh(object == independent ? Take(mesh->Clone()) : mesh));
		auto* renderer = Take(object->AddComponent<MeshRendererComponent>());
		Done(renderer->SetSharedMaterials({material, nullptr}));
	}
	const auto id = ResourceId::Generate();
	auto document = Take(EncodeSceneDocument("Test.ursascene", id, scene, registry));
	Expect(document["localResources"].size() == 3, "Document resource table did not deduplicate identities");
	Expect(Take(EncodeSceneDocument("Test.ursascene", id, scene, registry))["localResources"] ==
	           document["localResources"],
	       "Repeated save changed local IDs");
	ComponentLoadContext context(*session);
	auto restored = Take(DecodeSceneDocument("Test.ursascene", document, registry, context));
	auto restoredRoot = Take(context.GameObjectById(root->Id()));
	auto restoredChild = Take(context.GameObjectById(child->Id()));
	auto restoredIndependent = Take(context.GameObjectById(independent->Id()));
	auto restoredMesh = restoredRoot->FindComponent<MeshFilterComponent>()->SharedMesh();
	Expect(restoredMesh == restoredChild->FindComponent<MeshFilterComponent>()->SharedMesh() &&
	           restoredMesh != restoredIndependent->FindComponent<MeshFilterComponent>()->SharedMesh(),
	       "Document restored incorrect Mesh sharing");
	Expect(restoredMesh != mesh && restoredMesh->LocalIdentity() == mesh->LocalIdentity(),
	       "Document reused live object or changed its persistent local ID");
	Expect(restoredRoot->FindComponent<MeshRendererComponent>()->SharedMaterial() ==
	           restoredChild->FindComponent<MeshRendererComponent>()->SharedMaterial(),
	       "Material sharing was lost");
	auto broken = document;
	broken["gameObjects"][0]["components"][0]["data"]["mesh"]["id"] = ToStdString(ResourceId::Generate().ToString());
	Expect(!DecodeSceneDocument("Test.ursascene", broken, registry, context) && !context.GameObjectById(root->Id()),
	       "Unresolved local reference published a partial graph");
	broken = document;
	broken["localResources"][0]["type"] = "ursa.Unknown";
	Expect(!DecodeSceneDocument("Test.ursascene", broken, registry, context), "Wrong local resource kind accepted");
	broken = document;
	for (auto& row : broken["localResources"])
	{
		if (row["type"] == "ursa.Mesh")
		{
			row["data"]["skeleton"] = {{"guid", row["id"]}, {"localFileId", 1u}};
			break;
		}
	}
	Expect(!DecodeSceneDocument("Test.ursascene", broken, registry, context) && !context.GameObjectById(root->Id()),
	       "Cyclic or wrong-kind resource dependency published a partial graph");
	auto prefab = Take(CapturePrefabAsset(ResourceId::Generate(), scene, root, registry, context));
	Scene instances;
	auto instanceA = Take(prefab.Instantiate(instances, registry, context));
	auto instanceB = Take(prefab.Instantiate(instances, registry, context));
	auto meshA = instanceA->FindComponent<MeshFilterComponent>()->SharedMesh();
	auto meshB = instanceB->FindComponent<MeshFilterComponent>()->SharedMesh();
	Expect(meshA != meshB && meshA->LocalIdentity() != meshB->LocalIdentity(),
	       "Prefab instances shared mutable local Mesh");
	Expect(meshA == instanceA->Children().front()->FindComponent<MeshFilterComponent>()->SharedMesh(),
	       "Prefab instance lost internal sharing");
	Expect(
	    Take(EncodeSceneDocument("Instances.ursascene", ResourceId::Generate(), instances, registry))["localResources"]
	            .size() == 6,
	    "Prefab resource identities collide on scene save");
	Expect(!instances.AttachGameObject(prefab.Root()), "Prefab source attached to Scene without instantiation");
}

void LinkedResources(VirtualFileSystem& files)
{
	auto session = Take(ResourceSession::Open(files));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	Scene sourceScene;
	auto root = Take(sourceScene.CreateGameObject("Prefab"));
	auto* filter = Take(root->AddComponent<MeshFilterComponent>());
	auto* renderer = Take(root->AddComponent<MeshRendererComponent>());
	auto mesh = Triangle();
	auto material = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets, MaterialType::Unlit);
	Done(filter->SetSharedMesh(mesh));
	Done(renderer->SetSharedMaterial(material));
	const auto sourcePath = Take(VirtualPath::Parse("prj:MeshPrefab.ursaprefab"));
	const auto prefabId = ResourceId::Generate();
	Done(SavePrefabResource(files, sourcePath, prefabId, sourceScene, root, registry));
	const auto sidecar = Take(VirtualPath::Parse(sourcePath.ToString() + ".meta"));
	const String metadata = "{\"guid\":\"" + prefabId.ToString() + "\"}";
	Done(files.WriteAtomic(sidecar, Vector<UInt8>(metadata.begin(), metadata.end())));
	session = Take(ResourceSession::Open(files));
	Scene empty;
	auto document = Take(EncodeSceneDocument("Linked.ursascene", ResourceId::Generate(), empty, registry));
	const auto instanceA = ResourceId::Generate();
	const auto instanceB = ResourceId::Generate();
	Done(AddPrefabInstance(document, prefabId, instanceA));
	Done(AddPrefabInstance(document, prefabId, instanceB));
	ComponentLoadContext context(*session);
	auto linked = Take(DecodeLinkedSceneDocument("Linked.ursascene", document, registry, context));
	auto objects = linked.scene->RootGameObjects();
	auto meshA = objects[0]->FindComponent<MeshFilterComponent>()->SharedMesh();
	auto meshB = objects[1]->FindComponent<MeshFilterComponent>()->SharedMesh();
	Expect(meshA != meshB && meshA->LocalIdentity() != meshB->LocalIdentity(),
	       "Linked Prefabs shared mutable geometry");
	Done(meshA->SetVertices({{8, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
	auto localMaterial = material->Clone();
	Done(localMaterial->SetBaseColor({0.8f, 0.2f, 0.1f, 1}));
	Done(objects[0]->FindComponent<MeshRendererComponent>()->SetMaterials({localMaterial, nullptr}));
	const auto linkedPath = Take(VirtualPath::Parse("prj:Linked.ursascene"));
	Done(SaveLinkedSceneResource(files, linkedPath, linked, registry));
	Expect(linked.document["localResources"].empty() &&
	           linked.document["instances"][0]["overrides"].contains("localResources"),
	       "Prefab local changes were flattened into the scene resource table");
	Done(mesh->SetVertices({{4, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
	Done(SavePrefabResource(files, sourcePath, prefabId, sourceScene, root, registry));
	auto reopened = Take(LoadLinkedSceneResource(files, linkedPath, registry, context));
	objects = reopened.scene->RootGameObjects();
	Expect(objects[0]->FindComponent<MeshFilterComponent>()->SharedMesh()->Snapshot()->data.positions[0].x == 8 &&
	           objects[1]->FindComponent<MeshFilterComponent>()->SharedMesh()->Snapshot()->data.positions[0].x == 4,
	       "Prefab source update erased an override or failed to reach an unmodified instance");
	auto slots = objects[0]->FindComponent<MeshRendererComponent>()->SharedMaterials();
	Expect(slots.size() == 2 && slots[0]->Parameters().baseColor.red == 0.8f && !slots[1],
	       "Prefab slot override was not restored");
	Done(ApplyPrefabInstance(files, sourcePath, reopened, instanceA, registry, context));
	objects = reopened.scene->RootGameObjects();
	Expect(objects[1]->FindComponent<MeshFilterComponent>()->SharedMesh()->Snapshot()->data.positions[0].x == 8 &&
	           objects[1]->FindComponent<MeshRendererComponent>()->SharedMaterials().size() == 2,
	       "Apply did not propagate geometry and material slots to another instance");
	Done(
	    objects[0]->FindComponent<MeshFilterComponent>()->SharedMesh()->SetVertices({{9, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
	Done(RevertPrefabInstance(files, sourcePath, reopened, instanceA, registry, context));
	Expect(reopened.scene->RootGameObjects()[0]
	               ->FindComponent<MeshFilterComponent>()
	               ->SharedMesh()
	               ->Snapshot()
	               ->data.positions[0]
	               .x == 8,
	       "Revert did not restore current Prefab geometry");
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::current_path() / ("MeshResources-" + ToStdString(Ursa::ResourceId::Generate().ToString()));
	std::filesystem::create_directory(root);
	int result = 0;
	try
	{
		VirtualFileSystem files;
		Done(files.Mount("prj", root, MountAccess::ReadWrite));
		Resources(files);
		Documents(files);
		LinkedResources(files);
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		result = 1;
	}
	std::filesystem::remove_all(root);
	return result;
}
