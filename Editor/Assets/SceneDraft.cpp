#include <Core/Types.hpp>
#include <Assets/SceneDraft.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Core/StringInterop.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <Serialization/SerializedObject.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Ursa::Editor
{

Result<std::shared_ptr<SceneDraft>> SceneDraft::Create(ResourceKind resourceKind, ComponentRegistry& componentRegistry,
                                                       ResourceSession& resourceSession,
                                                       const EditContextSetup& contextSetup)
{
	if (resourceKind != ResourceKind::Scene && resourceKind != ResourceKind::Prefab)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure("Scene draft kind must be Scene or Prefab");
	}
	auto draft =
	    std::shared_ptr<SceneDraft>(new SceneDraft(resourceKind, componentRegistry, resourceSession, contextSetup));
	if (resourceKind == ResourceKind::Prefab)
	{
		draft->prefabAsset = std::make_unique<PrefabAsset>(ResourceId::Generate(), GameObject::Create("Prefab"));
		auto refreshed = draft->RefreshPrefabPreview();
		if (!refreshed)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(refreshed.error);
		}
	}
	else
	{
		draft->scene = std::make_unique<Scene>();
		auto root = draft->scene->CreateGameObject("Root");
		if (!root)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(root.error);
		}
	}
	return Result<std::shared_ptr<SceneDraft>>::Success(std::move(draft));
}

Result<std::shared_ptr<SceneDraft>> SceneDraft::Load(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                     ResourceKind resourceKind, ComponentRegistry& componentRegistry,
                                                     ResourceSession& resourceSession,
                                                     const EditContextSetup& contextSetup)
{
	if (resourceKind != ResourceKind::Scene && resourceKind != ResourceKind::Prefab)
	{
		return Result<std::shared_ptr<SceneDraft>>::Failure("Scene draft kind must be Scene or Prefab");
	}
	ComponentLoadContext context(resourceSession);
	if (contextSetup)
	{
		auto configured = contextSetup(context);
		if (!configured)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(configured.error);
		}
	}
	auto draft =
	    std::shared_ptr<SceneDraft>(new SceneDraft(resourceKind, componentRegistry, resourceSession, contextSetup));
	if (resourceKind == ResourceKind::Prefab)
	{
		auto asset = LoadPrefabAssetResource(filesystem, path, componentRegistry, context);
		if (!asset)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(asset.error);
		}
		draft->prefabAsset = std::make_unique<PrefabAsset>(std::move(asset.value));
		auto refreshed = draft->RefreshPrefabPreview();
		if (!refreshed)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(refreshed.error);
		}
	}
	else
	{
		auto json = ReadJsonResource(filesystem, path, ResourceKind::Scene);
		if (!json)
		{
			return Result<std::shared_ptr<SceneDraft>>::Failure(json.error);
		}
		if (json.value.contains("instances"))
		{
			auto loaded = LoadLinkedSceneResource(filesystem, path, componentRegistry, context);
			if (!loaded)
			{
				return Result<std::shared_ptr<SceneDraft>>::Failure(loaded.error);
			}
			draft->linked = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
		}
		else
		{
			auto loaded = LoadSceneResource(filesystem, path, componentRegistry, context);
			if (!loaded)
			{
				return Result<std::shared_ptr<SceneDraft>>::Failure(loaded.error);
			}
			draft->scene = std::move(loaded.value.scene);
		}
	}
	return Result<std::shared_ptr<SceneDraft>>::Success(std::move(draft));
}

Result<ResourceId> SceneDraft::MaterialId(const std::shared_ptr<MaterialInstance>& material) const
{
	if (!material)
	{
		return Result<ResourceId>::Failure("No material assigned");
	}
	auto assigned = assignedMaterials.find(material.get());
	if (assigned != assignedMaterials.end())
	{
		return Result<ResourceId>::Success(assigned->second);
	}
	return resources.IdentifyMaterial(material);
}

Result<GameObject::Ptr> SceneDraft::InstantiateModel(ResourceId modelGuid, const nlohmann::json& modelDocument,
                                                     bool initializeAnimation)
{
	if (!modelGuid || !modelDocument.is_object() || modelDocument.value("format", std::string{}) != "UrsaModel" ||
	    modelDocument.value("version", Ursa::UInt32{0}) != 2 || !modelDocument.contains("nodes") ||
	    !modelDocument["nodes"].is_array())
	{
		return Result<GameObject::Ptr>::Failure("Cannot instantiate an invalid imported model document");
	}
	Map<Ursa::UInt64, const nlohmann::json*> nodesById;
	for (const nlohmann::json& node : modelDocument["nodes"])
	{
		if (!node.is_object() || !node.contains("localFileId") || !node["localFileId"].is_number_unsigned())
		{
			return Result<GameObject::Ptr>::Failure("Imported model contains a node without a local file ID");
		}
		const Ursa::UInt64 localFileId = node["localFileId"].get<Ursa::UInt64>();
		if (localFileId == 0 || !nodesById.emplace(localFileId, &node).second)
		{
			return Result<GameObject::Ptr>::Failure("Imported model contains duplicate or zero node local file IDs");
		}
	}
	const Ursa::UInt64 rootNodeId = modelDocument.value("rootNodeLocalFileId", Ursa::UInt64{0});
	if (nodesById.find(rootNodeId) == nodesById.end())
	{
		return Result<GameObject::Ptr>::Failure("Imported model root node reference cannot be resolved");
	}
	Set<Ursa::UInt64> activeNodes;
	Set<Ursa::UInt64> visitedNodes;
	auto createNode = [&](auto&& self, Ursa::UInt64 nodeId) -> Result<GameObject::Ptr>
	{
		const auto found = nodesById.find(nodeId);
		if (found == nodesById.end() || !activeNodes.insert(nodeId).second || !visitedNodes.insert(nodeId).second)
		{
			return Result<GameObject::Ptr>::Failure("Imported model node hierarchy has an invalid or cyclic reference");
		}
		const nlohmann::json& node = *found->second;
		const String name = node.contains("name") && node["name"].is_string()
		                        ? ToUrsaString(node["name"].get<std::string>())
		                        : String("Model Node");
		GameObject::Ptr object = GameObject::Create(name);
		if (!node.contains("localTransform") || !node["localTransform"].is_object())
		{
			return Result<GameObject::Ptr>::Failure("Model node has no decomposed local transform: " + name);
		}
		const nlohmann::json& transform = node["localTransform"];
		const auto readVector = [&](const char* field, Ursa::Size count) -> Result<Vector<Ursa::Float32>>
		{
			if (!transform.contains(field) || !transform[field].is_array() || transform[field].size() != count)
			{
				return Result<Vector<Ursa::Float32>>::Failure("Model node has an invalid " + ToUrsaString(field) +
				                                              " transform: " + name);
			}
			Vector<Ursa::Float32> values;
			values.reserve(count);
			for (const nlohmann::json& value : transform[field])
			{
				if (!value.is_number())
				{
					return Result<Vector<Ursa::Float32>>::Failure("Model node has a nonnumeric local transform: " +
					                                              name);
				}
				const Ursa::Float32 component = value.get<Ursa::Float32>();
				if (!std::isfinite(component))
				{
					return Result<Vector<Ursa::Float32>>::Failure("Model node has a nonfinite local transform: " +
					                                              name);
				}
				values.push_back(component);
			}
			return Result<Vector<Ursa::Float32>>::Success(std::move(values));
		};
		auto position = readVector("position", 3);
		auto rotation = readVector("rotation", 4);
		auto scale = readVector("scale", 3);
		if (!position || !rotation || !scale)
		{
			return Result<GameObject::Ptr>::Failure(!position   ? position.error
			                                        : !rotation ? rotation.error
			                                                    : scale.error);
		}
		LocalTransform localTransform;
		localTransform.position = {position.value[0], position.value[1], position.value[2]};
		localTransform.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
		localTransform.scale = {scale.value[0], scale.value[1], scale.value[2]};
		auto setTransform = object->SetLocalTransform(localTransform);
		if (!setTransform)
		{
			return Result<GameObject::Ptr>::Failure(setTransform.error);
		}
		if (node.contains("meshLocalFileIds"))
		{
			if (!node["meshLocalFileIds"].is_array())
			{
				return Result<GameObject::Ptr>::Failure("Model node mesh references are invalid: " + name);
			}
			for (const nlohmann::json& meshValue : node["meshLocalFileIds"])
			{
				if (!meshValue.is_number_unsigned() || meshValue.get<Ursa::UInt64>() == 0)
				{
					return Result<GameObject::Ptr>::Failure("Model node has an invalid mesh local file ID: " + name);
				}
				const Ursa::UInt64 meshLocalFileId = meshValue.get<Ursa::UInt64>();
				auto geometry = resources.LoadMesh(modelGuid, meshLocalFileId);
				if (!geometry)
				{
					return Result<GameObject::Ptr>::Failure(geometry.error);
				}
				GameObject::Ptr renderObject = object;
				if (node["meshLocalFileIds"].size() > 1)
				{
					renderObject = GameObject::Create("Mesh-" + ToUrsaString(std::to_string(meshLocalFileId)));
					auto parented = renderObject->SetParent(object);
					if (!parented)
					{
						return Result<GameObject::Ptr>::Failure(parented.error);
					}
				}
				RendererComponent* renderer = nullptr;
				if (geometry.value->Snapshot()->data.skeleton)
				{
					auto added = renderObject->AddComponent<SkinnedMeshRendererComponent>();
					if (!added)
					{
						return Result<GameObject::Ptr>::Failure(added.error);
					}
					auto assigned = added.value->SetSharedMesh(geometry.value);
					if (!assigned)
					{
						return Result<GameObject::Ptr>::Failure(assigned.error);
					}
					renderer = added.value;
				}
				else
				{
					auto filter = renderObject->AddComponent<MeshFilterComponent>();
					if (!filter)
					{
						return Result<GameObject::Ptr>::Failure(filter.error);
					}
					auto assigned = filter.value->SetSharedMesh(geometry.value);
					if (!assigned)
					{
						return Result<GameObject::Ptr>::Failure(assigned.error);
					}
					auto added = renderObject->AddComponent<MeshRendererComponent>();
					if (!added)
					{
						return Result<GameObject::Ptr>::Failure(added.error);
					}
					renderer = added.value;
				}
				Vector<std::shared_ptr<MaterialInstance>> materials;
				const std::string bindingKey = std::to_string(meshLocalFileId);
				if (modelDocument.at("meshMaterialBindings").contains(bindingKey))
				{
					for (const auto& slot : modelDocument.at("meshMaterialBindings").at(bindingKey))
					{
						const auto materialId = slot.get<UInt64>();
						if (!materialId)
						{
							materials.push_back(nullptr);
							continue;
						}
						auto loaded = resources.LoadMaterial(modelGuid, materialId);
						if (!loaded)
						{
							return Result<GameObject::Ptr>::Failure(loaded.error);
						}
						materials.push_back(std::move(loaded.value));
					}
				}
				auto assigned = renderer->SetSharedMaterials(std::move(materials));
				if (!assigned)
				{
					return Result<GameObject::Ptr>::Failure(assigned.error);
				}
			}
		}
		if (node.contains("children"))
		{
			if (!node["children"].is_array())
			{
				return Result<GameObject::Ptr>::Failure("Model node children are invalid: " + name);
			}
			for (const nlohmann::json& childValue : node["children"])
			{
				if (!childValue.is_number_unsigned())
				{
					return Result<GameObject::Ptr>::Failure("Model node has an invalid child reference: " + name);
				}
				auto child = self(self, childValue.get<Ursa::UInt64>());
				if (!child)
				{
					return child;
				}
				auto parented = child.value->SetParent(object);
				if (!parented)
				{
					return Result<GameObject::Ptr>::Failure(parented.error);
				}
			}
		}
		activeNodes.erase(nodeId);
		return Result<GameObject::Ptr>::Success(std::move(object));
	};
	auto rootObject = createNode(createNode, rootNodeId);
	if (!rootObject)
	{
		return rootObject;
	}
	if (initializeAnimation && modelDocument.contains("skeletonLocalFileIds") &&
	    modelDocument["skeletonLocalFileIds"].is_array() && !modelDocument["skeletonLocalFileIds"].empty() &&
	    modelDocument.contains("animationClipLocalFileIds") && modelDocument["animationClipLocalFileIds"].is_array() &&
	    !modelDocument["animationClipLocalFileIds"].empty())
	{
		if (!modelDocument["skeletonLocalFileIds"][0].is_number_unsigned() ||
		    !modelDocument["animationClipLocalFileIds"][0].is_number_unsigned())
		{
			return Result<GameObject::Ptr>::Failure("Imported model has invalid skeleton or animation references");
		}
		auto animator = rootObject.value->AddComponent<AnimatorComponent>();
		if (!animator)
		{
			return Result<GameObject::Ptr>::Failure(animator.error);
		}
		auto animation =
		    animator.value->SetAnimation(modelGuid, modelDocument["skeletonLocalFileIds"][0].get<Ursa::UInt64>(),
		                                 modelDocument["animationClipLocalFileIds"][0].get<Ursa::UInt64>(), resources);
		if (!animation)
		{
			return Result<GameObject::Ptr>::Failure(animation.error);
		}
	}
	if (visitedNodes.size() != nodesById.size())
	{
		return Result<GameObject::Ptr>::Failure(
		    "Imported model contains nodes that are detached from the root hierarchy");
	}
	if (kind == ResourceKind::Prefab)
	{
		if (!prefabAsset || !prefabAsset->Root())
		{
			return Result<GameObject::Ptr>::Failure("No PrefabAsset is available for model instantiation");
		}
		auto parented = rootObject.value->SetParent(prefabAsset->Root());
		if (!parented)
		{
			return Result<GameObject::Ptr>::Failure(parented.error);
		}
		auto refreshed = RefreshPrefabPreview();
		return refreshed ? rootObject : Result<GameObject::Ptr>::Failure(refreshed.error);
	}
	auto attached = GetScene().AttachGameObject(rootObject.value);
	return attached ? rootObject : Result<GameObject::Ptr>::Failure(attached.error);
}

Result<void> SceneDraft::Save(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id)
{
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	ComponentLoadContext validation(resources);
	if (setup)
	{
		auto prepared = setup(validation);
		if (!prepared)
		{
			return prepared;
		}
	}
	if (kind == ResourceKind::Prefab)
	{
		if (!prefabAsset || !prefabAsset->Root())
		{
			return Result<void>::Failure("Prefab document has no detached asset graph");
		}
		PrefabAsset resource(id, prefabAsset->Root());
		auto saved = SavePrefabAssetResource(filesystem, path, resource, registry, resolver);
		if (!saved)
		{
			return saved;
		}
		auto loaded = LoadPrefabAssetResource(filesystem, path, registry, validation);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	if (linked)
	{
		auto saved = SaveLinkedSceneResource(filesystem, path, *linked, registry, resolver);
		if (!saved)
		{
			return saved;
		}
		auto loaded = LoadLinkedSceneResource(filesystem, path, registry, validation);
		return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
	}
	auto saved = SaveSceneResource(filesystem, path, id, GetScene(), registry, resolver);
	if (!saved)
	{
		return saved;
	}
	auto loaded = LoadSceneResource(filesystem, path, registry, validation);
	return loaded ? Result<void>{} : Result<void>::Failure(loaded.error);
}

Result<void> SceneDraft::RefreshPrefabPreview()
{
	if (kind != ResourceKind::Prefab || !prefabAsset || !prefabAsset->Root())
	{
		return Result<void>::Failure("Only a PrefabAsset draft can refresh its preview Scene");
	}
	auto preview = std::make_unique<Scene>();
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto prepared = setup(context);
		if (!prepared)
		{
			return prepared;
		}
	}
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	auto instantiated = prefabAsset->Instantiate(*preview, registry, context, resolver);
	if (!instantiated)
	{
		return Result<void>::Failure(instantiated.error);
	}
	previewObjects.clear();
	std::function<Result<void>(const GameObject::Ptr&)> mapPreview = [&](const GameObject::Ptr& source) -> Result<void>
	{
		auto previewObject = context.GameObjectById(source->Id());
		if (!previewObject)
		{
			return Result<void>::Failure(previewObject.error);
		}
		previewObjects[source->Id().ToString()] = previewObject.value;
		for (const auto& child : source->Children())
		{
			auto result = mapPreview(child);
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	auto mapped = mapPreview(prefabAsset->Root());
	if (!mapped)
	{
		return mapped;
	}
	scene = std::move(preview);
	return {};
}

Result<GameObject::Ptr> SceneDraft::PreviewGameObject(ResourceId prefabObjectId) const
{
	const auto found = previewObjects.find(prefabObjectId.ToString());
	return found == previewObjects.end()
	           ? Result<GameObject::Ptr>::Failure("Prefab preview has no mapping for GameObject " +
	                                              prefabObjectId.ToString())
	           : Result<GameObject::Ptr>::Success(found->second);
}

Result<void> SceneDraft::SyncPrefabTransform(ResourceId prefabObjectId)
{
	if (!prefabAsset || !prefabAsset->Root())
	{
		return Result<void>::Failure("Prefab draft has no detached asset graph");
	}
	auto previewObject = PreviewGameObject(prefabObjectId);
	if (!previewObject)
	{
		return Result<void>::Failure(previewObject.error);
	}
	std::function<GameObject::Ptr(const GameObject::Ptr&)> find = [&](const GameObject::Ptr& object)
	{
		if (object->Id() == prefabObjectId)
		{
			return object;
		}
		for (const auto& child : object->Children())
		{
			auto found = find(child);
			if (found)
			{
				return found;
			}
		}
		return GameObject::Ptr{};
	};
	auto assetObject = find(prefabAsset->Root());
	if (!assetObject)
	{
		return Result<void>::Failure("Prefab asset has no GameObject " + prefabObjectId.ToString());
	}
	auto local = previewObject.value->GetLocalTransform();
	return local ? assetObject->SetLocalTransform(local.value) : Result<void>::Failure(local.error);
}

Result<void> SceneDraft::AssignMaterial(GameObject::Ptr node, ResourceId materialId, ResourceSession& resourceSession)
{
	if (kind == ResourceKind::Prefab)
	{
		auto* box = node ? node->FindComponent<RendererComponent>() : nullptr;
		if (!box)
		{
			return Result<void>::Failure("Selected Prefab GameObject has no box renderer component");
		}
		if (!materialId)
		{
			auto assigned = box->SetSharedMaterial(nullptr);
			if (!assigned)
			{
				return assigned;
			}
			return {};
		}
		auto material = resourceSession.LoadMaterial(materialId);
		if (!material)
		{
			return Result<void>::Failure(material.error);
		}
		auto assigned = box->SetSharedMaterial(material.value);
		if (!assigned)
		{
			return assigned;
		}
		assignedMaterials[material.value.get()] = materialId;
		return {};
	}
	auto* box = GetScene().FindComponent<RendererComponent>(node);
	if (!box)
	{
		return Result<void>::Failure("Selected node has no box renderer component");
	}
	auto components = GetScene().ComponentsOf(node);
	if (!components)
	{
		return Result<void>::Failure(components.error);
	}
	for (ComponentId id : components.value)
	{
		if (GetScene().GetComponent(id) == box)
		{
			return AssignMaterial(id, materialId, resourceSession);
		}
	}
	return Result<void>::Failure("Box renderer component is missing");
}

Result<void> SceneDraft::AssignMaterial(ComponentId component, ResourceId materialId, ResourceSession& resourceSession)
{
	auto* box = dynamic_cast<RendererComponent*>(GetScene().GetComponent(component));
	if (!box)
	{
		return Result<void>::Failure("Selected component is not a box renderer");
	}
	if (!materialId)
	{
		auto assigned = box->SetSharedMaterial(nullptr);
		if (!assigned)
		{
			return assigned;
		}
		return {};
	}
	auto material = resourceSession.LoadMaterial(materialId);
	if (!material)
	{
		return Result<void>::Failure(material.error);
	}
	auto assigned = box->SetSharedMaterial(material.value);
	if (!assigned)
	{
		return assigned;
	}
	assignedMaterials[material.value.get()] = materialId;
	return {};
}

Result<void> SceneDraft::ApplyInstance(VirtualFileSystem& files, const VirtualPath& source, ResourceId instance)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto prepared = setup(context);
		if (!prepared)
		{
			return prepared;
		}
	}
	return ApplyPrefabInstance(files, source, *linked, instance, registry, context);
}

Result<void> SceneDraft::RevertInstance(VirtualFileSystem& files, const VirtualPath& source, ResourceId instance)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto prepared = setup(context);
		if (!prepared)
		{
			return prepared;
		}
	}
	return RevertPrefabInstance(files, source, *linked, instance, registry, context);
}

Result<void> SceneDraft::SetInstanceProperty(ResourceId instanceId, ResourceId sourceObjectId, const String& pointer,
                                             const nlohmann::json& value)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	return SetPrefabPropertyOverride(*linked, instanceId, sourceObjectId, pointer, value);
}

Result<void> SceneDraft::SetInstanceProperties(const Vector<SceneInstancePropertyOverride>& overrides)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	const auto previousDocument = linked->document;
	for (const auto& property : overrides)
	{
		auto updated = SetPrefabPropertyOverride(*linked, property.instanceId, property.sourceObjectId, property.path,
		                                         property.value);
		if (!updated)
		{
			linked->document = previousDocument;
			return updated;
		}
	}
	return {};
}

Result<void> SceneDraft::SetInstanceStructure(ResourceId instanceId, const String& operation,
                                              const nlohmann::json& value)
{
	if (!linked)
	{
		return Result<void>::Failure("Scene has no linked Prefab instances");
	}
	return SetPrefabStructureOverride(*linked, instanceId, operation, value);
}

Result<ResourceId> SceneDraft::PlacePrefab(ResourceId prefabId, ResourceId parentId, const VirtualPath& scenePath,
                                           ResourceId sceneId)
{
	auto placed = PlacePrefabBatch({{prefabId, parentId}}, scenePath, sceneId);
	return placed ? Result<ResourceId>::Success(placed.value.front()) : Result<ResourceId>::Failure(placed.error);
}

Result<Vector<ResourceId>> SceneDraft::PlacePrefabBatch(const Vector<std::pair<ResourceId, ResourceId>>& placements,
                                                        const VirtualPath& scenePath, ResourceId sceneId)
{
	if (kind != ResourceKind::Scene)
	{
		return Result<Vector<ResourceId>>::Failure("Prefab instances can only be placed in scenes");
	}
	if (placements.empty())
	{
		return Result<Vector<ResourceId>>::Failure("Prefab placement requires at least one instance");
	}
	const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance>& material)
	{ return MaterialId(material); };
	nlohmann::json document;
	if (linked)
	{
		const auto previousDocument = linked->document;
		const auto previousSnapshot = linked->snapshot;
		const auto previousScopes = linked->scopedIds;
		auto captured = CaptureLinkedSceneChanges(scenePath.ToString(), *linked, registry, resolver);
		if (!captured)
		{
			return Result<Vector<ResourceId>>::Failure(captured.error);
		}
		document = linked->document;
		linked->document = previousDocument;
		linked->snapshot = previousSnapshot;
		linked->scopedIds = previousScopes;
	}
	else
	{
		auto encoded = EncodeSceneDocument(scenePath.ToString(), sceneId, *scene, registry, resolver);
		if (!encoded)
		{
			return Result<Vector<ResourceId>>::Failure(encoded.error);
		}
		document = std::move(encoded.value);
	}
	Vector<ResourceId> instanceIds;
	instanceIds.reserve(placements.size());
	for (const auto& [prefabId, parentId] : placements)
	{
		const ResourceId instanceId = ResourceId::Generate();
		auto placed = AddPrefabInstance(document, prefabId, instanceId, parentId);
		if (!placed)
		{
			return Result<Vector<ResourceId>>::Failure(placed.error);
		}
		instanceIds.push_back(instanceId);
	}
	ComponentLoadContext context(resources);
	if (setup)
	{
		auto prepared = setup(context);
		if (!prepared)
		{
			return Result<Vector<ResourceId>>::Failure(prepared.error);
		}
	}
	auto loaded = DecodeLinkedSceneDocument(scenePath.ToString(), std::move(document), registry, context);
	if (!loaded)
	{
		return Result<Vector<ResourceId>>::Failure(loaded.error);
	}
	linked = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
	scene.reset();
	return Result<Vector<ResourceId>>::Success(std::move(instanceIds));
}

Result<Vector<ResourceId>> SceneDraft::PlaceImportedAssets(const Vector<SceneAssetPlacement>& placements,
                                                           ResourceId parentId, std::optional<Size> insertIndex,
                                                           const VirtualPath& scenePath, ResourceId sceneId)
{
	if (kind != ResourceKind::Scene || placements.empty())
	{
		return Result<Vector<ResourceId>>::Failure("Imported assets can only be placed into a Scene");
	}
	Set<String> seen;
	Vector<std::pair<ResourceId, ResourceId>> prefabPlacements;
	for (const auto& placement : placements)
	{
		if (!placement.guid || (placement.kind != "Prefab" && placement.kind != "Model") ||
		    !seen.insert(placement.guid.ToString()).second)
		{
			return Result<Vector<ResourceId>>::Failure("Scene placement requires unique Prefab or Model main assets");
		}
		if (placement.kind == "Model" && (!placement.modelDocument.is_object() ||
		                                  placement.modelDocument.value("format", std::string{}) != "UrsaModel"))
		{
			return Result<Vector<ResourceId>>::Failure("Scene placement contains an invalid imported Model document");
		}
		if (placement.kind == "Prefab")
		{
			prefabPlacements.push_back({placement.guid, parentId});
		}
	}
	GameObject::Ptr parent;
	if (parentId)
	{
		auto resolved = GetScene().GameObjectById(parentId);
		if (!resolved)
		{
			return Result<Vector<ResourceId>>::Failure(resolved.error);
		}
		parent = resolved.value;
	}
	Size siblingCount = 0;
	if (parent)
	{
		auto children = GetScene().ChildrenOf(parent);
		if (!children)
		{
			return Result<Vector<ResourceId>>::Failure(children.error);
		}
		siblingCount = children.value.size();
	}
	else
	{
		siblingCount = GetScene().RootGameObjects().size();
	}
	if (insertIndex && *insertIndex > siblingCount)
	{
		return Result<Vector<ResourceId>>::Failure("Scene placement sibling index is out of range");
	}
	Vector<std::pair<ResourceId, GameObject::Ptr>> createdModels;
	Vector<std::pair<ResourceId, ResourceId>> modelIds;
	auto rollbackModels = [&]
	{
		for (const auto& item : createdModels)
		{
			if (GetScene().Contains(item.second))
			{
				GetScene().DestroyGameObject(item.second);
			}
		}
	};
	for (const auto& placement : placements)
	{
		if (placement.kind != "Model")
		{
			continue;
		}
		auto model = InstantiateModel(placement.guid, placement.modelDocument, false);
		if (!model)
		{
			rollbackModels();
			return Result<Vector<ResourceId>>::Failure(model.error);
		}
		auto attached = GetScene().SetParent(model.value, parent);
		if (!attached)
		{
			GetScene().DestroyGameObject(model.value);
			rollbackModels();
			return Result<Vector<ResourceId>>::Failure(attached.error);
		}
		auto id = GetScene().PersistentId(model.value);
		if (!id)
		{
			GetScene().DestroyGameObject(model.value);
			rollbackModels();
			return Result<Vector<ResourceId>>::Failure(id.error);
		}
		modelIds.push_back({placement.guid, id.value});
		createdModels.push_back({placement.guid, std::move(model.value)});
	}
	Vector<ResourceId> prefabInstances;
	if (!prefabPlacements.empty())
	{
		auto placed = PlacePrefabBatch(prefabPlacements, scenePath, sceneId);
		if (!placed)
		{
			rollbackModels();
			return Result<Vector<ResourceId>>::Failure(placed.error);
		}
		prefabInstances = std::move(placed.value);
	}
	Vector<ResourceId> placedIds;
	placedIds.reserve(placements.size());
	Size prefabIndex = 0;
	for (const auto& placement : placements)
	{
		if (placement.kind == "Model")
		{
			const auto model = std::find_if(modelIds.begin(), modelIds.end(),
			                                [&](const auto& item) { return item.first == placement.guid; });
			placedIds.push_back(model->second);
			continue;
		}
		const ResourceId instanceId = prefabInstances[prefabIndex++];
		ResourceId rootId = instanceId;
		if (!GetScene().GameObjectById(rootId) && linked)
		{
			const auto scope = linked->scopedIds.find(instanceId.ToString());
			if (scope != linked->scopedIds.end())
			{
				for (const auto& [localId, scopedId] : scope->second)
				{
					(void)localId;
					auto parsed = ResourceId::Parse(scopedId);
					auto candidate = parsed ? GetScene().GameObjectById(parsed.value)
					                        : Result<GameObject::Ptr>::Failure(parsed.error);
					if (!candidate)
					{
						continue;
					}
					auto candidateParent = GetScene().ParentOf(candidate.value);
					if (!candidateParent)
					{
						continue;
					}
					ResourceId resolvedParent;
					if (candidateParent.value)
					{
						auto resolved = GetScene().PersistentId(candidateParent.value);
						if (!resolved)
						{
							continue;
						}
						resolvedParent = resolved.value;
					}
					if (resolvedParent == parentId)
					{
						rootId = parsed.value;
						break;
					}
				}
			}
		}
		placedIds.push_back(rootId);
	}
	if (insertIndex)
	{
		for (Size index = 0; index < placedIds.size(); ++index)
		{
			auto object = GetScene().GameObjectById(placedIds[index]);
			if (!object)
			{
				return Result<Vector<ResourceId>>::Failure(object.error);
			}
			auto reordered = GetScene().ReorderGameObject(object.value, *insertIndex + index);
			if (!reordered)
			{
				return Result<Vector<ResourceId>>::Failure(reordered.error);
			}
		}
	}
	return Result<Vector<ResourceId>>::Success(std::move(placedIds));
}

Result<std::unique_ptr<EditorDocument>> NewSceneDocument(const VirtualPath& path, ResourceKind kind,
                                                         ComponentRegistry& registry, ResourceSession& resources,
                                                         const EditContextSetup& setup)
{
	auto draft = SceneDraft::Create(kind, registry, resources, setup);
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document =
	    EditorDocument::Create(kind, ResourceId::Generate(), path,
	                           [draft = draft.value](VirtualFileSystem& filesystem, const VirtualPath& target,
	                                                 ResourceId id) { return draft->Save(filesystem, target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
		document.value->MarkDirty();
	}
	return document;
}

Result<std::unique_ptr<EditorDocument>> OpenSceneDocument(const VirtualPath& path, ResourceKind kind,
                                                          ComponentRegistry& registry, ResourceSession& resources,
                                                          const EditContextSetup& setup)
{

	auto header = ReadResourceHeader(resources.Index().Filesystem(), path);
	if (!header)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(header.error);
	}
	if (header.value.kind != kind)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure("Scene document kind does not match file");
	}
	auto draft = SceneDraft::Load(resources.Index().Filesystem(), path, kind, registry, resources, setup);
	if (!draft)
	{
		return Result<std::unique_ptr<EditorDocument>>::Failure(draft.error);
	}
	auto document =
	    EditorDocument::Create(kind, header.value.id, path,
	                           [draft = draft.value](VirtualFileSystem& filesystem, const VirtualPath& target,
	                                                 ResourceId id) { return draft->Save(filesystem, target, id); });
	if (document)
	{
		document.value->SetPayload(draft.value);
	}
	return document;
}

} // namespace Ursa::Editor
