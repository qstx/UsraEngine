#include <MeshFixtures.hpp>
#include <ResourceFixtures.hpp>
#include <Assets/SceneDraft.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Core/StringInterop.hpp>
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
		std::filesystem::create_directories(root / "Assets");
		auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		done(material->SetBaseColor({0.7f, 0.2f, 0.1f, 1.f}));
		auto workspace = Take(Editor::ProjectWorkspace::Open(root));
		done(workspace->SetRuntimeContentView(workspace->Index().Filesystem()));
		auto documents = Take(Editor::EditorDocuments::Create(*workspace));
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		const auto scenePath = root / "Assets" / "level.ursascene";
		auto sceneDocument = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                   ResourceKind::Scene, registry, documents->Session()));
		const auto sceneId = sceneDocument->Id();
		auto draft = sceneDocument->Payload<Editor::SceneDraft>();
		Scene& scene = draft->GetScene();
		const auto first = scene.RootGameObjects().front();
		const auto child = Take(scene.CreateGameObject(first));
		const auto last = Take(scene.CreateGameObject());
		done(scene.SetGameObjectName(child, "Nested box"));
		done(scene.SetGameObjectName(last, "Last"));
		done(scene.SetActive(child, false));
		LocalTransform pose;
		pose.position = {1, 2, 3};
		done(scene.SetLocalTransform(child, pose));
		Take(Ursa::Test::AddRenderer(scene, child));
		auto* sceneRenderer = child->FindComponent<MeshRendererComponent>();
		Expect(sceneRenderer != nullptr, "Scene mesh renderer was not created");
		sceneRenderer->SetCastShadows(false);
		sceneRenderer->SetReceiveShadows(false);
		Take(scene.AddComponent<DirectionalLightComponent>(last));
		auto* sceneLight = last->FindComponent<DirectionalLightComponent>();
		Expect(sceneLight != nullptr, "Scene directional light was not created");
		done(sceneLight->SetCastsShadows(false));
		done(sceneLight->SetShadowStrength(0.4f));
		done(sceneLight->SetShadowBias(0.02f));
		done(sceneLight->SetShadowNormalBias(0.2f));
		done(sceneLight->SetSoftShadows(true));
		Expect(!sceneLight->SetShadowStrength(1.1f) && !sceneLight->SetShadowBias(-0.01f) &&
		           sceneLight->ShadowStrength() == 0.4f && sceneLight->ShadowBias() == 0.02f,
		       "Directional light Inspector setters accepted invalid shadow values or changed the stored settings");
		done(sceneRenderer->SetSharedMaterial(material));
		const auto childId = Take(scene.PersistentId(child));
		const auto lastId = Take(scene.PersistentId(last));
		done(documents->RequestOpen(std::move(sceneDocument)));
		done(documents->SaveCurrent());
		auto reopened = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                               ResourceKind::Scene, registry, documents->Session()));
		Expect(reopened->Id() == sceneId, "Scene resource ID changed after reopen");
		auto loaded = reopened->Payload<Editor::SceneDraft>();
		const auto loadedChild = Take(loaded->GetScene().GameObjectById(childId));
		Expect(Take(loaded->GetScene().GameObjectName(loadedChild)) == "Nested box" &&
		           !Take(loaded->GetScene().ActiveSelf(loadedChild)) &&
		           Take(loaded->GetScene().GetLocalTransform(loadedChild)).position.x == 1.f,
		       "Scene hierarchy or transform did not round-trip");
		Expect(Take(loaded->GetScene().PersistentId(loaded->GetScene().RootGameObjects().back())) == lastId,
		       "Scene root order or ID changed");
		const auto* box = loaded->GetScene().FindComponent<MeshRendererComponent>(loadedChild);
		const auto loadedLast = Take(loaded->GetScene().GameObjectById(lastId));
		const auto* loadedLight = loaded->GetScene().FindComponent<DirectionalLightComponent>(loadedLast);
		Expect(box && !box->CastShadows() && !box->ReceiveShadows() && box->SharedMaterial() &&
		           box->SharedMaterial()->Parameters().baseColor.red > 0.6f && loadedLight &&
		           !loadedLight->CastsShadows() && loadedLight->ShadowStrength() == 0.4f &&
		           loadedLight->ShadowBias() == 0.02f && loadedLight->ShadowNormalBias() == 0.2f &&
		           loadedLight->SoftShadows(),
		       "Scene material reference did not round-trip");

		const auto prefabPath = root / "Assets" / "tile.ursaprefab";
		auto prefabDocument = Take(Editor::NewSceneDocument(Take(VirtualPath::Parse("prj:tile.ursaprefab")),
		                                                    ResourceKind::Prefab, registry, documents->Session()));
		const auto prefabId = prefabDocument->Id();
		auto prefabDraft = prefabDocument->Payload<Editor::SceneDraft>();
		const auto prefabRoot = prefabDraft->GetPrefabAsset()->Root();
		auto prefabLight = Take(prefabRoot->AddComponent<DirectionalLightComponent>());
		done(prefabLight->SetCastsShadows(false));
		done(prefabLight->SetShadowStrength(0.65f));
		done(prefabLight->SetShadowBias(0.015f));
		done(prefabLight->SetShadowNormalBias(0.12f));
		done(prefabLight->SetSoftShadows(true));
		const auto previewRoot = prefabDraft->GetScene().RootGameObjects().front();
		Expect(prefabRoot->Id() != previewRoot->Id() && !prefabRoot->Parent(),
		       "Prefab editing graph was attached to its preview Scene");
		Expect(Take(prefabDraft->PreviewGameObject(prefabRoot->Id())) == previewRoot,
		       "Prefab preview did not retain a source GameObject mapping");
		auto previewTransform = Take(previewRoot->GetLocalTransform());
		previewTransform.position.x = 2.f;
		done(previewRoot->SetLocalTransform(previewTransform));
		done(prefabDraft->SyncPrefabTransform(prefabRoot->Id()));
		Expect(Take(prefabRoot->GetLocalTransform()).position.x == 2.f,
		       "Prefab viewport transform was not copied back to its detached GameObject");
		const auto prefabChild = GameObject::Create("Prefab child");
		done(prefabChild->SetParent(prefabRoot));
		auto prefabBox = Take(Ursa::Test::AddRenderer(prefabChild));
		Expect(prefabBox->TryGetScene() == nullptr, "Prefab component unexpectedly attached to its preview Scene");
		prefabBox->SetCastShadows(false);
		prefabBox->SetReceiveShadows(false);
		done(prefabBox->SetSharedMaterial(material));
		const auto prefabChildId = prefabChild->Id();
		done(documents->RequestOpen(std::move(prefabDocument)));
		done(documents->SaveCurrent());
		auto reopenedPrefab = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:tile.ursaprefab")),
		                                                     ResourceKind::Prefab, registry, documents->Session()));
		Expect(reopenedPrefab->Id() == prefabId, "Prefab resource ID changed after reopen");
		auto prefabLoaded = reopenedPrefab->Payload<Editor::SceneDraft>();
		const auto loadedPrefabRoot = prefabLoaded->GetPrefabAsset()->Root();
		const auto loadedPrefabChild = loadedPrefabRoot->Children().front();
		const auto* loadedPrefabLight = loadedPrefabRoot->FindComponent<DirectionalLightComponent>();
		const auto* loadedPrefabRenderer = loadedPrefabChild->FindComponent<MeshRendererComponent>();
		Expect(loadedPrefabChild->Id() == prefabChildId && Take(loadedPrefabChild->Name()) == "Prefab child" &&
		           loadedPrefabRenderer && !loadedPrefabRenderer->CastShadows() &&
		           !loadedPrefabRenderer->ReceiveShadows() && loadedPrefabRenderer->SharedMaterial() &&
		           loadedPrefabLight && !loadedPrefabLight->CastsShadows() &&
		           loadedPrefabLight->ShadowStrength() == 0.65f && loadedPrefabLight->ShadowBias() == 0.015f &&
		           loadedPrefabLight->ShadowNormalBias() == 0.12f && loadedPrefabLight->SoftShadows(),
		       "Prefab subtree or material reference did not round-trip");
		auto linkedDocument = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                     ResourceKind::Scene, registry, documents->Session()));
		auto linkedDraft = linkedDocument->Payload<Editor::SceneDraft>();
		const auto firstInstance =
		    Take(linkedDraft->PlacePrefab(prefabId, childId, Take(VirtualPath::Parse("prj:Scene.ursascene")), sceneId));
		const auto secondInstance =
		    Take(linkedDraft->PlacePrefab(prefabId, childId, Take(VirtualPath::Parse("prj:Scene.ursascene")), sceneId));
		const auto firstScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(firstInstance.ToString()).at(prefabChildId.ToString())));
		const auto secondScoped = Take(ResourceId::Parse(
		    linkedDraft->LinkedScene()->scopedIds.at(secondInstance.ToString()).at(prefabChildId.ToString())));
		const auto firstNode = Take(linkedDraft->GetScene().GameObjectById(firstScoped));
		const auto secondNode = Take(linkedDraft->GetScene().GameObjectById(secondScoped));
		done(linkedDraft->GetScene().SetGameObjectName(firstNode, "Instance override"));
		Take(linkedDraft->GetScene().CreateGameObject(firstNode));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabChildId, "/name", "Instance override"));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabBox->PersistentId(), "/data/castShadows", true));
		done(linkedDraft->SetInstanceProperty(firstInstance, prefabBox->PersistentId(), "/data/receiveShadows", true));
		Expect(Take(linkedDraft->GetScene().GameObjectName(secondNode)) == "Prefab child",
		       "Editing first placement changed the second placement");
		const auto sourceBefore = Take(ReadJsonResource(prefabPath, ResourceKind::Prefab));
		linkedDocument->MarkDirty();
		done(documents->RequestOpen(std::move(linkedDocument)));
		done(documents->Current()->Dirty() ? Result<void>{} : Result<void>::Failure("Expected dirty scene"));
		done(documents->SaveCurrent());
		auto linkedReopened = Take(Editor::OpenSceneDocument(Take(VirtualPath::Parse("prj:level.ursascene")),
		                                                     ResourceKind::Scene, registry, documents->Session()));
		auto reloadedDraft = linkedReopened->Payload<Editor::SceneDraft>();
		const auto reloadedFirst = Take(reloadedDraft->GetScene().GameObjectById(firstScoped));
		const auto reloadedSecond = Take(reloadedDraft->GetScene().GameObjectById(secondScoped));
		const auto* reloadedFirstRenderer = reloadedFirst->FindComponent<MeshRendererComponent>();
		const auto* reloadedSecondRenderer = reloadedSecond->FindComponent<MeshRendererComponent>();
		Expect(Take(reloadedDraft->GetScene().GameObjectName(reloadedFirst)) == "Instance override" &&
		           Take(reloadedDraft->GetScene().GameObjectName(reloadedSecond)) == "Prefab child" &&
		           reloadedFirstRenderer && reloadedFirstRenderer->CastShadows() &&
		           reloadedFirstRenderer->ReceiveShadows() && reloadedSecondRenderer &&
		           !reloadedSecondRenderer->CastShadows() && !reloadedSecondRenderer->ReceiveShadows() &&
		           Take(reloadedDraft->GetScene().ChildrenOf(reloadedFirst)).size() == 1,
		       "Prefab property or structure overrides did not round-trip");
		Expect(Take(ReadJsonResource(prefabPath, ResourceKind::Prefab)) == sourceBefore,
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
