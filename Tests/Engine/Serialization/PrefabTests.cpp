#include <MeshFixtures.hpp>
#include <ResourceFixtures.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/AssetReference.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class UnregisteredComponent final : public Ursa::Component
{
};

void PlacePrefab(const std::filesystem::path& root, Ursa::Scene& source, Ursa::GameObject::Ptr subtree)
{
	Ursa::ComponentRegistry registry;
	if (!Ursa::RegisterBuiltinComponents(registry))
	{
		return;
	}
	const auto prefabId = Ursa::ResourceId::Generate();
	if (!Ursa::SavePrefabResource(root / "tile.ursaprefab", prefabId, source, subtree, registry))
	{
		return;
	}
	Ursa::Scene empty;
	const auto scenePath = root / "level.ursascene";
	auto document = Ursa::EncodeSceneDocument(scenePath, Ursa::ResourceId::Generate(), empty, registry);
	if (!document)
	{
		return;
	}
	const auto instanceId = Ursa::ResourceId::Generate();
	if (!Ursa::AddPrefabInstance(document.value, prefabId, instanceId) ||
	    !Ursa::WriteJsonResource(scenePath, document.value))
	{
		return;
	}
	if (!Ursa::WriteResourceMetadataTree(root))
	{
		return;
	}
	auto resources = Ursa::ResourceSession::Open({root});
	if (!resources)
	{
		return;
	}
	Ursa::ComponentLoadContext context(*resources.value);
	auto linked = Ursa::LoadLinkedSceneResource(scenePath, registry, context);
	if (!linked)
	{
		return;
	}
	auto sourceId = source.PersistentId(subtree);
	if (!sourceId ||
	    !Ursa::SetPrefabPropertyOverride(linked.value, instanceId, sourceId.value, "/localTransform/position/0",
	                                     2.0f) ||
	    !Ursa::SaveLinkedSceneResource(scenePath, linked.value, registry))
	{
		return;
	}
	auto refreshed = resources.value->Refreshed();
	if (!refreshed)
	{
		return;
	}
	Ursa::ComponentLoadContext reloadContext(*refreshed.value);
	if (!Ursa::ReloadLinkedSceneResource(scenePath, registry, reloadContext, linked.value))
	{
		return;
	}
	if (!Ursa::ClearPrefabPropertyOverride(linked.value, instanceId, sourceId.value, "/localTransform/position/0") ||
	    !Ursa::SaveLinkedSceneResource(scenePath, linked.value, registry))
	{
		return;
	}
	Ursa::ReloadLinkedSceneResource(scenePath, registry, reloadContext, linked.value);
}

void TestInstances(const std::filesystem::path& root)
{
	Ursa::ComponentRegistry registry;
	done(Ursa::RegisterBuiltinComponents(registry));
	Ursa::Scene source;
	auto prefabRoot = Take(source.CreateGameObject());
	auto child = Take(source.CreateGameObject(prefabRoot));
	const auto boxId = Take(Ursa::Test::AddRenderer(source, child, Ursa::Color{0.2f, 0.3f, 0.4f, 1.0f}));
	const auto scriptId =
	    Take(source.AddComponent<Ursa::Scripting::ScriptBehaviourComponent>(child, Ursa::String("Demo.MoveWithInput")));
	auto* script = static_cast<Ursa::Scripting::ScriptBehaviourComponent*>(source.GetComponent(scriptId));
	nlohmann::json scriptPayload = nlohmann::json::object();
	scriptPayload["FormatVersion"] = 1u;
	scriptPayload["ScriptType"] = "demo.move-with-input";
	scriptPayload["Version"] = 1u;
	scriptPayload["Fields"] = nlohmann::json::object();
	scriptPayload["Fields"]["step"] = 2.5;
	scriptPayload["Fields"]["targetObject"]["$ursaReference"]["Kind"] = "GameObject";
	scriptPayload["Fields"]["targetObject"]["$ursaReference"]["Id"] = Ursa::ToStdString(prefabRoot->Id().ToString());
	scriptPayload["Fields"]["targetComponent"]["$ursaReference"]["Kind"] = "Component";
	scriptPayload["Fields"]["targetComponent"]["$ursaReference"]["Id"] =
	    Ursa::ToStdString(Take(source.ComponentPersistentId(boxId)).ToString());
	script->SetSerializedPayload(Ursa::ToUrsaString(scriptPayload.dump()));
	auto unrelated = Take(source.CreateGameObject());
	Take(source.AddComponent<UnregisteredComponent>(unrelated));
	const auto prefabId = Ursa::ResourceId::Generate();
	const auto prefabPath = root / "box.ursaprefab";
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, prefabRoot, registry));
	done(Ursa::WriteResourceMetadataTree(root));
	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext prefabContext(*session);
	auto direct = Take(Ursa::LoadPrefabResource(prefabPath, registry, prefabContext));
	Ursa::ComponentLoadContext assetContext(*session);
	auto asset = Take(Ursa::LoadPrefabAssetResource(prefabPath, registry, assetContext));
	const auto assetRootId = asset.Root()->Id();
	const auto assetChildren = asset.Root()->Children();
	Expect(asset.Id() == prefabId && assetRootId == prefabRoot->Id() && assetChildren.size() == 1 &&
	           assetChildren[0]->Id() == child->Id() &&
	           assetChildren[0]->FindComponent<Ursa::MeshRendererComponent>() &&
	           assetChildren[0]->FindComponent<Ursa::Scripting::ScriptBehaviourComponent>() &&
	           assetChildren[0]->FindComponent<Ursa::Scripting::ScriptBehaviourComponent>()->HasSerializedPayload() &&
	           Ursa::ToStdString(
	               assetChildren[0]->FindComponent<Ursa::Scripting::ScriptBehaviourComponent>()->SerializedPayload())
	                   .find("2.5") != std::string::npos &&
	           assetChildren[0]->FindComponent<Ursa::MeshRendererComponent>()->TryGetScene() == nullptr,
	       "PrefabAsset did not preserve detached GameObject or component identity");
	const auto copiedPrefabPath = root / "asset-copy.ursaprefab";
	done(Ursa::SavePrefabAssetResource(copiedPrefabPath, asset, registry));
	Ursa::ComponentLoadContext copiedAssetContext(*session);
	auto copiedAsset = Take(Ursa::LoadPrefabAssetResource(copiedPrefabPath, registry, copiedAssetContext));
	Expect(copiedAsset.Root()->Id() == assetRootId && copiedAsset.Root()->Children()[0]->Id() == child->Id(),
	       "PrefabAsset save/reopen changed object IDs");
	Ursa::Scene instantiatedScene;
	Ursa::ComponentLoadContext instantiateContext(*session);
	auto instance = Take(asset.Instantiate(instantiatedScene, registry, instantiateContext));
	auto secondPrefabInstance = Take(asset.Instantiate(instantiatedScene, registry, instantiateContext));
	Ursa::Scene secondInstanceScene;
	Ursa::ComponentLoadContext secondInstantiateContext(*session);
	auto instanceInSecondScene = Take(asset.Instantiate(secondInstanceScene, registry, secondInstantiateContext));
	Expect(instance->Id() != assetRootId && instance->Children()[0]->Id() != child->Id() &&
	           instantiatedScene.Contains(instance) && secondPrefabInstance->Id() != instance->Id() &&
	           secondInstanceScene.Contains(instanceInSecondScene) && instanceInSecondScene->Id() != instance->Id(),
	       "PrefabAsset instantiation did not create independent runtime GameObjects across instances and Scenes");
	auto* instanceScript = instance->Children()[0]->FindComponent<Ursa::Scripting::ScriptBehaviourComponent>();
	const auto instanceBox = instance->Children()[0]->FindComponent<Ursa::MeshRendererComponent>();
	Expect(instanceScript && instanceBox, "Instantiated Prefab lost its script or referenced native Component");
	const nlohmann::json instancePayload =
	    nlohmann::json::parse(Ursa::ToStdString(instanceScript->SerializedPayload()));
	const auto& fields = instancePayload.at("Fields");
	Expect(fields.at("targetObject").at("$ursaReference").at("Id").get<std::string>() ==
	               Ursa::ToStdString(instance->Id().ToString()) &&
	           fields.at("targetComponent").at("$ursaReference").at("Id").get<std::string>() ==
	               Ursa::ToStdString(instanceBox->PersistentId().ToString()),
	       "Prefab instantiation did not remap script object/Component references into the new instance");
	done(instantiatedScene.DestroyGameObject(instance));
	done(instantiatedScene.DestroyGameObject(secondPrefabInstance));
	done(secondInstanceScene.DestroyGameObject(instanceInSecondScene));
	std::filesystem::remove(copiedPrefabPath);
	auto legacyDocument = Take(Ursa::ReadJsonResource(prefabPath, Ursa::ResourceKind::Prefab));
	legacyDocument.erase("prefabGraphVersion");
	const auto legacyPath = root / "legacy.ursaprefab";
	done(Ursa::WriteJsonResource(legacyPath, legacyDocument));
	Ursa::ComponentLoadContext legacyContext(*session);
	const auto legacyLoad = Ursa::LoadPrefabResource(legacyPath, registry, legacyContext);
	Expect(!legacyLoad &&
	           legacyLoad.error.find("Unsupported or missing GameObject Prefab format version") != Ursa::String::npos,
	       "Legacy Prefab format was not rejected with a version diagnostic");
	Ursa::ComponentLoadContext legacyAssetContext(*session);
	const auto legacyAssetLoad = Ursa::LoadPrefabAssetResource(legacyPath, registry, legacyAssetContext);
	Expect(!legacyAssetLoad && legacyAssetLoad.error.find("Unsupported or missing GameObject Prefab format version") !=
	                               Ursa::String::npos,
	       "PrefabAsset loader did not reject the unsupported legacy graph version");
	std::filesystem::remove(legacyPath);
	Expect(direct.scene->RootGameObjects().size() == 1 &&
	           Take(direct.scene->ChildrenOf(direct.scene->RootGameObjects()[0])).size() == 1,
	       "Standalone Prefab subtree was not restored");

	Ursa::Scene plain;
	auto outside = Take(plain.CreateGameObject());
	const auto outsideId = Take(plain.PersistentId(outside));
	const auto firstPath = root / "first.ursascene";
	auto first = Take(Ursa::EncodeSceneDocument(firstPath, Ursa::ResourceId::Generate(), plain, registry));
	const auto firstInstance = Ursa::ResourceId::Generate();
	const auto secondInstance = Ursa::ResourceId::Generate();
	done(Ursa::AddPrefabInstance(first, prefabId, firstInstance, outsideId));
	done(Ursa::AddPrefabInstance(first, prefabId, secondInstance, outsideId));
	done(Ursa::WriteJsonResource(firstPath, first));
	const auto secondPath = root / "second.ursascene";
	auto second = Take(Ursa::EncodeSceneDocument(secondPath, Ursa::ResourceId::Generate(), plain, registry));
	done(Ursa::AddPrefabInstance(second, prefabId, Ursa::ResourceId::Generate(), outsideId));
	done(Ursa::WriteJsonResource(secondPath, second));
	auto refreshed = Take(session->Refreshed());
	Ursa::ComponentLoadContext plainContext(*refreshed);
	auto incompatible = Ursa::LoadSceneResource(firstPath, registry, plainContext);
	Expect(!incompatible && incompatible.error.find("loadLinkedSceneResource") != Ursa::String::npos,
	       "Plain scene loader silently discarded Prefab instances");
	Ursa::ComponentLoadContext firstContext(*refreshed);
	Ursa::ComponentLoadContext secondContext(*refreshed);
	auto firstLoaded = Take(Ursa::LoadLinkedSceneResource(firstPath, registry, firstContext));
	auto secondLoaded = Take(Ursa::LoadLinkedSceneResource(secondPath, registry, secondContext));
	auto firstRoot = Take(firstLoaded.scene->GameObjectById(outsideId));
	auto secondRoot = Take(secondLoaded.scene->GameObjectById(outsideId));
	const auto firstChildren = Take(firstLoaded.scene->ChildrenOf(firstRoot));
	const auto secondChildren = Take(secondLoaded.scene->ChildrenOf(secondRoot));
	auto firstPrefabReference =
	    Ursa::AssetReferenceFromJson(firstLoaded.document["instances"][0]["prefab"], "first.prefab");
	auto secondPrefabReference =
	    Ursa::AssetReferenceFromJson(secondLoaded.document["instances"][0]["prefab"], "second.prefab");
	Expect(firstChildren.size() == 2 && secondChildren.size() == 1 &&
	           Take(firstLoaded.scene->PersistentId(firstChildren[0])) !=
	               Take(firstLoaded.scene->PersistentId(firstChildren[1])) &&
	           firstPrefabReference && secondPrefabReference && firstPrefabReference.value.guid == prefabId &&
	           secondPrefabReference.value.guid == prefabId && firstPrefabReference.value.localFileId == 0 &&
	           secondPrefabReference.value.localFileId == 0,
	       "Prefab instances lost their independent identity or source reference");
	std::filesystem::remove(firstPath);
	std::filesystem::remove(secondPath);
	std::filesystem::remove(prefabPath);
}

void TestScriptFieldOverrideMigration(const std::filesystem::path& root)
{
	Ursa::ComponentRegistry registry;
	done(Ursa::RegisterBuiltinComponents(registry));
	done(registry.RegisterScriptFieldAlias("demo.migrating", "oldName", "newName"));
	Ursa::Scene prefabSource;
	const auto prefabRoot = Take(prefabSource.CreateGameObject());
	const auto child = Take(prefabSource.CreateGameObject(prefabRoot));
	const auto scriptId = Take(prefabSource.AddComponent<Ursa::Scripting::ScriptBehaviourComponent>(
	    child, Ursa::String("Demo.MigratingBehaviour")));
	auto* script = static_cast<Ursa::Scripting::ScriptBehaviourComponent*>(prefabSource.GetComponent(scriptId));
	nlohmann::json payload = nlohmann::json::object();
	payload["FormatVersion"] = 1u;
	payload["ScriptType"] = "demo.migrating";
	payload["Version"] = 2u;
	payload["Fields"] = {{"oldName", 2.5}};
	script->SetSerializedPayload(Ursa::ToUrsaString(payload.dump()));

	const Ursa::ResourceId prefabId = Ursa::ResourceId::Generate();
	const auto prefabPath = root / "migration.ursaprefab";
	done(Ursa::SavePrefabResource(prefabPath, prefabId, prefabSource, prefabRoot, registry));
	done(Ursa::WriteResourceMetadataTree(root));
	Ursa::Scene sceneSource;
	const Ursa::ResourceId sceneId = Ursa::ResourceId::Generate();
	const auto scenePath = root / "migration.ursascene";
	auto document = Take(Ursa::EncodeSceneDocument(scenePath, sceneId, sceneSource, registry));
	done(Ursa::WriteJsonResource(scenePath, document));
	done(Ursa::WriteResourceMetadata(scenePath));
	done(Ursa::WriteResourceMetadataTree(root));
	const Ursa::ResourceId instanceId = Ursa::ResourceId::Generate();
	done(Ursa::AddPrefabInstance(document, prefabId, instanceId));
	const std::string componentKey = Ursa::ToStdString(Take(prefabSource.ComponentPersistentId(scriptId)).ToString());
	document["instances"][0]["overrides"]["properties"][componentKey]["/data/scriptPayload/Fields/oldName"] = 9.5;
	done(Ursa::WriteJsonResource(scenePath, document));
	done(Ursa::WriteResourceMetadataTree(root));
	auto resources = Take(Ursa::ResourceSession::Open({root}));
	Expect(std::filesystem::exists(scenePath) && std::filesystem::exists(prefabPath),
	       "Script Prefab override fixtures were not written to disk");
	const auto prefabLocation = Take(resources->ResolveAsset(prefabId, 0));
	Expect(prefabLocation.FileName() == "migration.ursaprefab",
	       "Script Prefab resource ID resolved to the wrong fixture asset");
	Ursa::ComponentLoadContext context(*resources);
	auto linked =
	    Take(Ursa::DecodeLinkedSceneDocument(Ursa::String("fixture:migration.ursascene"), document, registry, context));
	auto linkedChild = linked.scene->RootGameObjects().front()->Children().front();
	auto* linkedScript = linkedChild->FindComponent<Ursa::Scripting::ScriptBehaviourComponent>();
	Expect(linkedScript != nullptr, "Script Prefab instance has no managed component");
	const nlohmann::json linkedPayload = nlohmann::json::parse(Ursa::ToStdString(linkedScript->SerializedPayload()));
	Expect(linkedPayload.at("Fields").contains("newName"),
	       ("Prefab field migration produced: " + linkedPayload.dump()).c_str());
	Expect(linkedPayload.at("Fields").at("newName").get<double>() == 9.5 &&
	           !linkedPayload.at("Fields").contains("oldName"),
	       "Prefab override did not follow FormerlySerializedAs from oldName to newName");
	auto editedPayload = linkedPayload;
	editedPayload["Fields"]["newName"] = 7.5;
	linkedScript->SetSerializedPayload(Ursa::ToUrsaString(editedPayload.dump()));
	done(Ursa::SaveLinkedSceneResource(scenePath, linked, registry));
	const auto savedPrefab = Take(Ursa::ReadJsonResource(prefabPath, Ursa::ResourceKind::Prefab));
	bool sourceStayedUnchanged = false;
	for (const auto& node : savedPrefab.at("gameObjects"))
	{
		for (const auto& component : node.at("components"))
		{
			if (component.at("type") == "ursa.ScriptBehaviour")
			{
				const auto sourcePayload = component.at("data").at("scriptPayload");
				sourceStayedUnchanged = sourcePayload.at("Fields").at("oldName").get<double>() == 2.5;
			}
		}
	}
	Expect(sourceStayedUnchanged, "Saving a script Prefab instance modified the Prefab source fields");
	auto refreshedResources = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext reloadContext(*refreshedResources);
	auto reloadedLinked = Take(Ursa::LoadLinkedSceneResource(scenePath, registry, reloadContext));
	auto reloadedScript = reloadedLinked.scene->RootGameObjects()
	                          .front()
	                          ->Children()
	                          .front()
	                          ->FindComponent<Ursa::Scripting::ScriptBehaviourComponent>();
	const auto reloadedPayload = nlohmann::json::parse(Ursa::ToStdString(reloadedScript->SerializedPayload()));
	Expect(reloadedPayload.at("Fields").at("newName").get<double>() == 7.5,
	       "Saving/reloading a managed Prefab field override lost its value");
}

void TestProperties(const std::filesystem::path& root)
{
	Ursa::ComponentRegistry registry;
	done(Ursa::RegisterBuiltinComponents(registry));
	auto initialMaterial = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::Unlit);
	auto changedMaterial = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::Unlit);
	done(initialMaterial->SetBaseColor({0.8f, 0.1f, 0.1f, 1.0f}));
	done(changedMaterial->SetBaseColor({0.1f, 0.8f, 0.1f, 1.0f}));
	const auto initialMaterialId = Ursa::ResourceId::Generate();
	const auto changedMaterialId = Ursa::ResourceId::Generate();
	done(Ursa::SaveMaterialResource(root / "initial.ursamat", initialMaterialId, *initialMaterial, {}));
	done(Ursa::SaveMaterialResource(root / "changed.ursamat", changedMaterialId, *changedMaterial, {}));
	Ursa::Scene source;
	const auto sourceRoot = Take(source.CreateGameObject());
	const auto sourceChild = Take(source.CreateGameObject(sourceRoot));
	Ursa::LocalTransform initialTransform;
	initialTransform.position = {1.0f, 1.0f, 0.0f};
	done(source.SetLocalTransform(sourceRoot, initialTransform));
	const auto boxId = Take(Ursa::Test::AddRenderer(source, sourceChild, Ursa::Color{0.2f, 0.3f, 0.4f, 1.0f}));
	auto* box = static_cast<Ursa::MeshRendererComponent*>(source.GetComponent(boxId));
	done(box->SetSharedMaterial(initialMaterial));
	const auto sourceRootId = Take(source.PersistentId(sourceRoot));
	const auto sourceBoxId = Take(source.ComponentPersistentId(boxId));
	const auto prefabId = Ursa::ResourceId::Generate();
	const auto prefabPath = root / "properties.ursaprefab";
	const auto materialResolver = [&](const std::shared_ptr<Ursa::MaterialInstance>& material)
	{
		return Ursa::Result<Ursa::ResourceId>::Success(
		    material == initialMaterial || material->Parameters().baseColor.red == 0.8f ? initialMaterialId
		                                                                                : changedMaterialId);
	};
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry, materialResolver));
	const auto path = root / "properties.ursascene";
	Ursa::Scene empty;
	auto document = Take(Ursa::EncodeSceneDocument(path, Ursa::ResourceId::Generate(), empty, registry));
	const auto firstInstance = Ursa::ResourceId::Generate();
	done(Ursa::AddPrefabInstance(document, prefabId, firstInstance));
	done(Ursa::AddPrefabInstance(document, prefabId, Ursa::ResourceId::Generate()));
	done(Ursa::WriteJsonResource(path, document));
	done(Ursa::WriteResourceMetadataTree(root));
	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext context(*session);
	auto loaded = Take(Ursa::LoadLinkedSceneResource(path, registry, context));
	done(Ursa::SetPrefabPropertyOverride(loaded, firstInstance, sourceRootId, "/localTransform/position/0", 9.0f));
	done(Ursa::SetPrefabPropertyOverride(
	    loaded, firstInstance, sourceBoxId, "/data/materials/0/value",
	    nlohmann::json{{"kind", "asset"},
	                   {"asset", {{"guid", Ursa::ToStdString(initialMaterialId.ToString())}, {"localFileId", 0u}}}}));
	done(Ursa::SaveLinkedSceneResource(path, loaded, registry));

	initialTransform.position = {2.0f, 3.0f, 0.0f};
	done(source.SetLocalTransform(sourceRoot, initialTransform));
	done(box->SetSharedMaterial(changedMaterial));
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry, materialResolver));
	auto refreshed = Take(session->Refreshed());
	Ursa::ComponentLoadContext refreshContext(*refreshed);
	done(Ursa::ReloadLinkedSceneResource(path, registry, refreshContext, loaded));
	const auto roots = loaded.scene->RootGameObjects();
	Expect(roots.size() == 2, "Updated Prefab lost an instance");
	const auto firstTransform = Take(loaded.scene->GetLocalTransform(roots[0]));
	const auto secondTransform = Take(loaded.scene->GetLocalTransform(roots[1]));
	auto firstChild = Take(loaded.scene->ChildrenOf(roots[0]))[0];
	auto secondChild = Take(loaded.scene->ChildrenOf(roots[1]))[0];
	auto* firstBox = loaded.scene->FindComponent<Ursa::MeshRendererComponent>(firstChild);
	auto* secondBox = loaded.scene->FindComponent<Ursa::MeshRendererComponent>(secondChild);
	Expect(firstTransform.position.x == 9.0f && firstTransform.position.y == 3.0f &&
	           secondTransform.position.x == 2.0f && secondTransform.position.y == 3.0f && firstBox && secondBox &&
	           firstBox->SharedMaterial() != secondBox->SharedMaterial() &&
	           firstBox->SharedMaterial()->Parameters().baseColor.red == 0.8f &&
	           secondBox->SharedMaterial()->Parameters().baseColor.green == 0.8f,
	       "Prefab property inheritance or explicit material override failed");
	done(Ursa::ClearPrefabPropertyOverride(loaded, firstInstance, sourceBoxId, "/data/materials/0/value"));
	done(Ursa::SaveLinkedSceneResource(path, loaded, registry));
	done(Ursa::ReloadLinkedSceneResource(path, registry, refreshContext, loaded));
	firstChild = Take(loaded.scene->ChildrenOf(loaded.scene->RootGameObjects()[0]))[0];
	firstBox = loaded.scene->FindComponent<Ursa::MeshRendererComponent>(firstChild);
	Expect(firstBox && firstBox->SharedMaterial()->Parameters().baseColor.green == 0.8f,
	       ("Clearing the explicit material override did not restore source inheritance (green=" +
	        std::to_string(firstBox ? firstBox->SharedMaterial()->Parameters().baseColor.green : -1.0f) + ")")
	           .c_str());
	std::filesystem::remove(path);
	std::filesystem::remove(prefabPath);
	std::filesystem::remove(root / "initial.ursamat");
	std::filesystem::remove(root / "changed.ursamat");
}

void TestStructure(const std::filesystem::path& root)
{
	Ursa::ComponentRegistry registry;
	done(Ursa::RegisterBuiltinComponents(registry));
	Ursa::Scene source;
	const auto sourceRoot = Take(source.CreateGameObject());
	const auto first = Take(source.CreateGameObject(sourceRoot));
	const auto removed = Take(source.CreateGameObject(sourceRoot));
	const auto moved = Take(source.CreateGameObject(sourceRoot));
	const auto firstComponent = Take(source.AddComponent<Ursa::PointLightComponent>(first));
	const auto removedComponent = Take(source.AddComponent<Ursa::PointLightComponent>(first));
	const auto rootId = Ursa::ToStdString(Take(source.PersistentId(sourceRoot)).ToString());
	const auto firstId = Ursa::ToStdString(Take(source.PersistentId(first)).ToString());
	const auto removedId = Ursa::ToStdString(Take(source.PersistentId(removed)).ToString());
	const auto movedId = Ursa::ToStdString(Take(source.PersistentId(moved)).ToString());
	const auto firstComponentId = Ursa::ToStdString(Take(source.ComponentPersistentId(firstComponent)).ToString());
	const auto removedComponentId = Ursa::ToStdString(Take(source.ComponentPersistentId(removedComponent)).ToString());
	const auto addedId = Ursa::ToStdString(Ursa::ResourceId::Generate().ToString());
	const auto addedComponentId = Ursa::ToStdString(Ursa::ResourceId::Generate().ToString());
	const auto secondAddedComponentId = Ursa::ToStdString(Ursa::ResourceId::Generate().ToString());
	const auto prefabId = Ursa::ResourceId::Generate();
	const auto prefabPath = root / "structure.ursaprefab";
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
	const auto path = root / "structure.ursascene";
	Ursa::Scene plain;
	auto document = Take(Ursa::EncodeSceneDocument(path, Ursa::ResourceId::Generate(), plain, registry));
	const auto instanceId = Ursa::ResourceId::Generate();
	done(Ursa::AddPrefabInstance(document, prefabId, instanceId));
	done(Ursa::WriteJsonResource(path, document));
	done(Ursa::WriteResourceMetadataTree(root));
	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext context(*session);
	auto linked = Take(Ursa::LoadLinkedSceneResource(path, registry, context));
	const nlohmann::json addedNode{
	    {"id", addedId},
	    {"parentId", rootId},
	    {"localTransform",
	     {{"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f, 1.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}},
	    {"enabled", true},
	    {"components", nlohmann::json::array()}};
	const auto componentRecord = [](const std::string& id, Ursa::Float32 red)
	{
		return nlohmann::json{{"id", id},
		                      {"type", "ursa.PointLight"},
		                      {"version", 1},
		                      {"enabled", true},
		                      {"data", {{"color", {red, 0.0f, 0.0f, 1.0f}}, {"intensity", 1.0f}, {"range", 10.0f}}}};
	};
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "deletedGameObjects", {removedId}));
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "addedGameObjects", nlohmann::json::array({addedNode})));
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "gameObjectParents", {{movedId, firstId}}));
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "gameObjectOrders", {{rootId, {addedId, firstId}}}));
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "deletedComponents", {removedComponentId}));
	done(Ursa::SetPrefabStructureOverride(
	    linked, instanceId, "addedComponents",
	    nlohmann::json::array({{{"owner", addedId}, {"component", componentRecord(addedComponentId, 0.3f)}},
	                           {{"owner", movedId}, {"component", componentRecord(secondAddedComponentId, 0.8f)}}})));
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "componentOwners", {{firstComponentId, movedId}}));
	done(Ursa::SetPrefabStructureOverride(linked, instanceId, "componentOrders",
	                                      {{movedId, {secondAddedComponentId, firstComponentId}}}));
	done(Ursa::SaveLinkedSceneResource(path, linked, registry));
	const auto sourceNew = Take(source.CreateGameObject(sourceRoot));
	const auto sourceNewId = Take(source.PersistentId(sourceNew));
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
	auto refreshed = Take(session->Refreshed());
	Ursa::ComponentLoadContext refreshContext(*refreshed);
	done(Ursa::ReloadLinkedSceneResource(path, registry, refreshContext, linked));
	const auto runtimeRoot = linked.scene->RootGameObjects()[0];
	const auto rootChildren = Take(linked.scene->ChildrenOf(runtimeRoot));
	Expect(rootChildren.size() == 3, "Structural merge lost source addition or instance addition");
	const auto addedRuntime = rootChildren[0];
	const auto firstRuntime = rootChildren[1];
	const auto movedRuntime = Take(linked.scene->ChildrenOf(firstRuntime))[0];
	Expect(Take(linked.scene->PersistentId(rootChildren[2])) != sourceNewId &&
	           Take(linked.scene->ChildrenOf(firstRuntime)).size() == 1 &&
	           Take(linked.scene->ComponentsOf(firstRuntime)).empty() &&
	           Take(linked.scene->ComponentsOf(addedRuntime)).size() == 1 &&
	           Take(linked.scene->ComponentsOf(movedRuntime)).size() == 2,
	       "Prefab node or component structural overrides were not applied");
	const auto movedComponents = Take(linked.scene->ComponentsOf(movedRuntime));
	auto* ordered = static_cast<Ursa::PointLightComponent*>(linked.scene->GetComponent(movedComponents[0]));
	Expect(ordered && ordered->Color().red == 0.8f, "Prefab component order override was not preserved");
	auto* edited = static_cast<Ursa::PointLightComponent*>(linked.scene->GetComponent(movedComponents[1]));
	done(edited->SetColor({0.6f, 0, 0, 1}));
	const auto runtimeAdded = Take(linked.scene->CreateGameObject(runtimeRoot));
	Take(linked.scene->AddComponent<Ursa::PointLightComponent>(runtimeAdded));
	done(Ursa::SaveLinkedSceneResource(path, linked, registry));
	done(Ursa::SaveLinkedSceneResource(path, linked, registry));
	done(Ursa::ReloadLinkedSceneResource(path, registry, refreshContext, linked));
	const auto capturedRoot = linked.scene->RootGameObjects()[0];
	const auto capturedChildren = Take(linked.scene->ChildrenOf(capturedRoot));
	Expect(capturedChildren.size() == 4, "Directly added instance node was not captured on save");
	const auto capturedMoved = Take(linked.scene->ChildrenOf(capturedChildren[1]))[0];
	const auto capturedComponents = Take(linked.scene->ComponentsOf(capturedMoved));
	edited = static_cast<Ursa::PointLightComponent*>(linked.scene->GetComponent(capturedComponents[1]));
	Expect(edited && edited->Color().red == 0.6f, "Directly edited component field was not captured on save");
	done(linked.scene->DestroyGameObject(capturedChildren[3]));
	done(Ursa::SaveLinkedSceneResource(path, linked, registry));
	done(Ursa::ReloadLinkedSceneResource(path, registry, refreshContext, linked));
	Expect(Take(linked.scene->ChildrenOf(linked.scene->RootGameObjects()[0])).size() == 3,
	       "Deleting a directly added Prefab node left a stale structural override");
	std::filesystem::remove(path);
	std::filesystem::remove(prefabPath);
}

void TestConflicts(const std::filesystem::path& root)
{
	Ursa::ComponentRegistry registry;
	done(Ursa::RegisterBuiltinComponents(registry));
	Ursa::Scene source;
	const auto sourceRoot = Take(source.CreateGameObject());
	const auto first = Take(source.CreateGameObject(sourceRoot));
	const auto second = Take(source.CreateGameObject(sourceRoot));
	const auto rootId = Take(source.PersistentId(sourceRoot));
	const auto firstId = Take(source.PersistentId(first));
	const auto secondId = Take(source.PersistentId(second));
	const auto prefabId = Ursa::ResourceId::Generate();
	const auto prefabPath = root / "conflict.ursaprefab";
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
	const auto path = root / "conflict.ursascene";
	Ursa::Scene empty;
	auto base = Take(Ursa::EncodeSceneDocument(path, Ursa::ResourceId::Generate(), empty, registry));
	const auto firstInstance = Ursa::ResourceId::Generate();
	const auto secondInstance = Ursa::ResourceId::Generate();
	done(Ursa::AddPrefabInstance(base, prefabId, firstInstance));
	done(Ursa::AddPrefabInstance(base, prefabId, secondInstance));
	done(Ursa::WriteJsonResource(path, base));
	done(Ursa::WriteResourceMetadataTree(root));
	auto session = Take(Ursa::ResourceSession::Open({root}));
	Ursa::ComponentLoadContext context(*session);
	auto active = Take(Ursa::LoadLinkedSceneResource(path, registry, context));
	auto* original = active.scene.get();
	const auto checkFailure = [&](const nlohmann::json& document, const std::string& expected)
	{
		done(Ursa::WriteJsonResource(path, document));
		std::ifstream beforeFile(path, std::ios::binary);
		const std::string before(std::istreambuf_iterator<char>{beforeFile}, std::istreambuf_iterator<char>{});
		auto failed = Ursa::ReloadLinkedSceneResource(path, registry, context, active);
		std::ifstream afterFile(path, std::ios::binary);
		const std::string after(std::istreambuf_iterator<char>{afterFile}, std::istreambuf_iterator<char>{});
		Expect(!failed && active.scene.get() == original && before == after &&
		           failed.error.find(prefabId.ToString()) != Ursa::String::npos &&
		           failed.error.find(firstInstance.ToString()) != Ursa::String::npos &&
		           failed.error.find(Ursa::ToUrsaString(expected)) != Ursa::String::npos,
		       "Prefab conflict did not preserve the active scene and saved patch with a precise error");
	};

	auto missing = base;
	missing["instances"][0]["overrides"]["properties"][Ursa::ToStdString(firstId.ToString())]["/enabled"] = false;
	done(source.DestroyGameObject(first));
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
	checkFailure(missing, "missing property target");
	const auto restoredFirst = Take(source.CreateGameObject(sourceRoot));
	done(source.AssignGameObjectId(restoredFirst, firstId));
	done(Ursa::SavePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));

	auto duplicate = base;
	duplicate["instances"][0]["overrides"]["addedGameObjects"] =
	    nlohmann::json::array({{{"id", Ursa::ToStdString(secondId.ToString())},
	                            {"parentId", Ursa::ToStdString(rootId.ToString())},
	                            {"components", nlohmann::json::array()}}});
	checkFailure(duplicate, "Duplicate added node ID");

	auto cycle = base;
	cycle["instances"][0]["overrides"]["gameObjectParents"] = {
	    {Ursa::ToStdString(firstId.ToString()), Ursa::ToStdString(secondId.ToString())},
	    {Ursa::ToStdString(secondId.ToString()), Ursa::ToStdString(firstId.ToString())}};
	checkFailure(cycle, "cycle");

	auto crossInstance = base;
	const auto otherRoot = active.scene->RootGameObjects()[1];
	crossInstance["instances"][0]["overrides"]["gameObjectParents"] = {
	    {Ursa::ToStdString(firstId.ToString()),
	     Ursa::ToStdString(Take(active.scene->PersistentId(otherRoot)).ToString())}};
	checkFailure(crossInstance, "Cross-instance");
	done(Ursa::WriteJsonResource(path, base));
	done(Ursa::ReloadLinkedSceneResource(path, registry, context, active));
	Expect(active.scene.get() != original, "Valid explicit reload did not replace the active scene");
	std::filesystem::remove(path);
	std::filesystem::remove(prefabPath);
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("UrsaPrefabTests-" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
		std::filesystem::create_directory(root);
		const auto reset = [&]
		{
			std::filesystem::remove_all(root);
			std::filesystem::create_directory(root);
		};
		TestInstances(root);
		reset();
		TestScriptFieldOverrideMigration(root);
		reset();
		TestProperties(root);
		reset();
		TestStructure(root);
		reset();
		TestConflicts(root);
		reset();
		Ursa::Scene docsSource;
		const auto docsRoot = Take(docsSource.CreateGameObject());
		PlacePrefab(root, docsSource, docsRoot);
		std::filesystem::remove(root / "tile.ursaprefab");
		std::filesystem::remove(root / "level.ursascene");
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
