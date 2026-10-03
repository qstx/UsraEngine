#include <assets/SceneDraft.hpp>
#include <serialization/MaterialResource.hpp>
#include <serialization/JsonResource.hpp>
#include <core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::current_path() / ("scene_draft_" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		std::filesystem::create_directories(root / "assets");
		auto material = std::make_shared<MaterialInstance>(MaterialType::unlit);
		done(material->SetBaseColor({0.7f, 0.2f, 0.1f, 1.f}));
		const auto materialId = ResourceId::Generate();
		done(SaveMaterialResource(root / "assets" / "box.ursamat", materialId, *material, {}));
		auto workspace = Take(Editor::ProjectWorkspace::Open(root));
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		const auto scenePath = root / "assets" / "level.ursascene";
		auto sceneDocument = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                   ResourceKind::scene, registry, documents->Session()));
		const auto sceneId = sceneDocument->Id();
		auto draft = sceneDocument->Payload<Editor::SceneDraft>();
		Scene& scene = draft->GetScene();
		const auto first = scene.RootNodes().front();
		const auto child = Take(scene.CreateNode(first));
		const auto last = Take(scene.CreateNode());
		done(scene.SetNodeLabel(child, "Nested box"));
		done(scene.SetNodeLabel(last, "Last"));
		done(scene.SetEnabled(child, false));
		LocalTransform pose;
		pose.position = {1, 2, 3};
		done(scene.SetLocalTransform(child, pose));
		Take(scene.AddComponent<BoxRenderComponent>(child));
		Take(scene.AddComponent<DirectionalLightComponent>(last));
		done(draft->AssignMaterial(child, materialId, documents->Session()));
		const auto childId = Take(scene.NodePersistentId(child));
		const auto lastId = Take(scene.NodePersistentId(last));
		done(documents->RequestOpen(std::move(sceneDocument)));
		done(documents->SaveCurrent());
		auto reopened = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                               ResourceKind::scene, registry, documents->Session()));
		Expect(reopened->Id() == sceneId, "Scene resource ID changed after reopen");
		auto loaded = reopened->Payload<Editor::SceneDraft>();
		const auto loadedChild = Take(loaded->GetScene().NodeByPersistentId(childId));
		Expect(Take(loaded->GetScene().NodeLabel(loadedChild)) == "Nested box" &&
		           !Take(loaded->GetScene().IsEnabled(loadedChild)) &&
		           Take(loaded->GetScene().GetLocalTransform(loadedChild)).position.x == 1.f,
		       "Scene hierarchy or transform did not round-trip");
		Expect(Take(loaded->GetScene().NodePersistentId(loaded->GetScene().RootNodes().back())) == lastId,
		       "Scene root order or ID changed");
		const auto* box = loaded->GetScene().FindComponent<BoxRenderComponent>(loadedChild);
		Expect(box && box->material && box->material->Parameters().baseColor.red > 0.6f,
		       "Scene material reference did not round-trip");

		const auto prefabPath = root / "assets" / "tile.ursaprefab";
		auto prefabDocument = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:tile.ursaprefab")),
		                                                    ResourceKind::prefab, registry, documents->Session()));
		const auto prefabId = prefabDocument->Id();
		auto prefabDraft = prefabDocument->Payload<Editor::SceneDraft>();
		const auto prefabRoot = prefabDraft->GetScene().RootNodes().front();
		const auto prefabChild = Take(prefabDraft->GetScene().CreateNode(prefabRoot));
		done(prefabDraft->GetScene().SetNodeLabel(prefabChild, "Prefab child"));
		Take(prefabDraft->GetScene().AddComponent<BoxRenderComponent>(prefabChild));
		done(prefabDraft->AssignMaterial(prefabChild, materialId, documents->Session()));
		const auto prefabChildId = Take(prefabDraft->GetScene().NodePersistentId(prefabChild));
		done(documents->RequestOpen(std::move(prefabDocument)));
		done(documents->SaveCurrent());
		auto reopenedPrefab = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:tile.ursaprefab")),
		                                                     ResourceKind::prefab, registry, documents->Session()));
		Expect(reopenedPrefab->Id() == prefabId, "Prefab resource ID changed after reopen");
		auto prefabLoaded = reopenedPrefab->Payload<Editor::SceneDraft>();
		const auto loadedPrefabChild = Take(prefabLoaded->GetScene().NodeByPersistentId(prefabChildId));
		Expect(Take(prefabLoaded->GetScene().NodeLabel(loadedPrefabChild)) == "Prefab child" &&
		           prefabLoaded->GetScene().FindComponent<BoxRenderComponent>(loadedPrefabChild)->material,
		       "Prefab subtree or material reference did not round-trip");
		auto linkedDocument = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                     ResourceKind::scene, registry, documents->Session()));
		auto linkedDraft = linkedDocument->Payload<Editor::SceneDraft>();
		const auto parentNode = Take(linkedDraft->GetScene().NodeByPersistentId(childId));
		const auto firstInstance = Take(linkedDraft->PlacePrefab(prefabId, parentNode, scenePath, sceneId));
		const auto secondInstance = Take(linkedDraft->PlacePrefab(prefabId, parentNode, scenePath, sceneId));
		const auto firstScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(firstInstance.ToString()).at(prefabChildId.ToString())));
		const auto secondScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(secondInstance.ToString()).at(prefabChildId.ToString())));
		const auto firstNode = Take(linkedDraft->GetScene().NodeByPersistentId(firstScoped));
		const auto secondNode = Take(linkedDraft->GetScene().NodeByPersistentId(secondScoped));
		done(linkedDraft->GetScene().SetNodeLabel(firstNode, "Instance override"));
		Take(linkedDraft->GetScene().CreateNode(firstNode));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabChildId, "/label", "Instance override"));
		Expect(Take(linkedDraft->GetScene().NodeLabel(secondNode)) == "Prefab child",
		       "Editing first placement changed the second placement");
		const auto sourceBefore = Take(ReadJsonResource(prefabPath, ResourceKind::prefab));
		linkedDocument->MarkDirty();
		done(documents->RequestOpen(std::move(linkedDocument)));
		done(documents->Current()->Dirty() ? Result<void>{} : Result<void>::Failure("Expected dirty scene"));
		done(documents->SaveCurrent());
		auto linkedReopened = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                     ResourceKind::scene, registry, documents->Session()));
		auto reloadedDraft = linkedReopened->Payload<Editor::SceneDraft>();
		const auto reloadedFirst = Take(reloadedDraft->GetScene().NodeByPersistentId(firstScoped));
		const auto reloadedSecond = Take(reloadedDraft->GetScene().NodeByPersistentId(secondScoped));
		Expect(Take(reloadedDraft->GetScene().NodeLabel(reloadedFirst)) == "Instance override" &&
		           Take(reloadedDraft->GetScene().NodeLabel(reloadedSecond)) == "Prefab child" &&
		           Take(reloadedDraft->GetScene().ChildrenOf(reloadedFirst)).size() == 1,
		       "Prefab property or structure overrides did not round-trip");
		Expect(Take(ReadJsonResource(prefabPath, ResourceKind::prefab)) == sourceBefore,
		       "Saving an instance modified the Prefab source");
		std::filesystem::remove_all(root);
		std::cout << "Editor scene and Prefab document test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::filesystem::remove_all(root);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
