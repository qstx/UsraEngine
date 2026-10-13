#include <Core/Types.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/DocumentResources.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/Scene.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

Json TransformJson(const LocalTransform& value)
{
	return {{"position", {value.position.x, value.position.y, value.position.z}},
	        {"rotation", {value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}},
	        {"scale", {value.scale.x, value.scale.y, value.scale.z}}};
}

Result<LocalTransform> ReadTransform(const Json& value, const String& location)
{
	if (!value.is_object() || !value.contains("position") || !value.contains("rotation") || !value.contains("scale"))
	{
		return Result<LocalTransform>::Failure("Invalid Prefab transform at " + location);
	}
	const auto read = [&](const char* key, Ursa::Size size) -> Result<Vector<Ursa::Float32>>
	{
		const auto& array = value[key];
		if (!array.is_array() || array.size() != size)
		{
			return Result<Vector<Ursa::Float32>>::Failure("Invalid Prefab transform at " + location);
		}
		Vector<Ursa::Float32> result;
		for (const auto& item : array)
		{
			if (!item.is_number())
			{
				return Result<Vector<Ursa::Float32>>::Failure("Invalid Prefab transform at " + location);
			}
			const Ursa::Float32 number = item.get<Ursa::Float32>();
			if (!std::isfinite(number))
			{
				return Result<Vector<Ursa::Float32>>::Failure("Non-finite Prefab transform at " + location);
			}
			result.push_back(number);
		}
		return Result<Vector<Ursa::Float32>>::Success(std::move(result));
	};
	const auto position = read("position", 3);
	const auto rotation = read("rotation", 4);
	const auto scale = read("scale", 3);
	if (!position || !rotation || !scale)
	{
		return Result<LocalTransform>::Failure(!position ? position.error : !rotation ? rotation.error : scale.error);
	}
	LocalTransform transform;
	transform.position = {position.value[0], position.value[1], position.value[2]};
	transform.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
	transform.scale = {scale.value[0], scale.value[1], scale.value[2]};
	return Result<LocalTransform>::Success(transform);
}
} // namespace

Result<Json>
EncodePrefabAssetDocument(const String& label, const PrefabAsset& asset, const ComponentRegistry& registry,
                          const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId)
{
	if (std::filesystem::path(ToStdString(label)).extension() != ".ursaprefab" || !asset.Id() || !asset.Root() ||
	    asset.Root()->Parent())
	{
		return Result<Json>::Failure("PrefabAsset requires a valid ID and one detached root at " + label);
	}
	Json objects = Json::array();
	DocumentResources localResources;
	SerializationContext serialization;
	DocumentResourceScope resourceScope(serialization, localResources);
	const ComponentSaveContext context{nullptr, materialId, &serialization};
	String error;
	std::function<void(const GameObject::Ptr&, ResourceId)> visit =
	    [&](const GameObject::Ptr& object, ResourceId parent)
	{
		if (!error.empty())
		{
			return;
		}
		auto name = object->Name();
		auto local = object->GetLocalTransform();
		auto enabled = object->ActiveSelf();
		if (!name || !local || !enabled)
		{
			error = "Cannot inspect Prefab GameObject";
			return;
		}
		Json components = Json::array();
		for (Component* component : object->Components())
		{
			auto codec = registry.FindFor(*component);
			if (!codec)
			{
				error = codec.error;
				return;
			}
			auto data = codec.value->save(*component, context);
			if (!data)
			{
				error = data.error;
				return;
			}
			components.push_back({{"id", ToStdString(component->PersistentId().ToString())},
			                      {"type", ToStdString(codec.value->type)},
			                      {"version", codec.value->version},
			                      {"enabled", component->IsEnabled()},
			                      {"data", std::move(data.value)}});
		}
		ArchiveWriter writer;
		auto saved = object->Serialize(writer);
		if (!saved)
		{
			error = saved.error;
			return;
		}
		auto fields = ArchiveToJson(writer);
		if (!fields)
		{
			error = fields.error;
			return;
		}
		fields.value["parentId"] = parent ? Json(ToStdString(parent.ToString())) : Json(nullptr);
		fields.value["components"] = std::move(components);
		objects.push_back(std::move(fields.value));
		for (const auto& child : object->Children())
		{
			visit(child, object->Id());
		}
	};
	visit(asset.Root(), {});
	if (!error.empty())
	{
		return Result<Json>::Failure("Cannot save PrefabAsset at " + label + ": " + error);
	}
	auto localData = localResources.Encode(serialization);
	if (!localData)
	{
		return Result<Json>::Failure(localData.error);
	}
	return Result<Json>::Success({{"localResourceVersion", UInt32{1}},
	                              {"localResources", std::move(localData.value)},
	                              {"format", "UrsaPrefab"},
	                              {"version", resourceFormatVersion},
	                              {"prefabGraphVersion", prefabGraphFormatVersion},
	                              {"id", ToStdString(asset.Id().ToString())},
	                              {"gameObjects", std::move(objects)}});
}

Result<void>
SavePrefabAssetResource(VirtualFileSystem& filesystem, const VirtualPath& path, const PrefabAsset& asset,
                        const ComponentRegistry& registry,
                        const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId)
{
	auto document = EncodePrefabAssetDocument("asset.ursaprefab", asset, registry, materialId);
	return document ? WriteJsonResource(filesystem, path, document.value) : Result<void>::Failure(document.error);
}

Result<PrefabAsset> DecodePrefabAssetDocument(const String& label, const Json& json, const ComponentRegistry& registry,
                                              ComponentLoadContext& context)
{
	ComponentMappingScope mappings(context);
	DocumentResources localResources;
	DocumentResourceScope resourceScope(context, localResources);
	auto restoredResources = localResources.Decode(json, context);
	if (!restoredResources)
	{
		return Result<PrefabAsset>::Failure(restoredResources.error + " at " + label);
	}

	if (!json.contains("prefabGraphVersion") || !json["prefabGraphVersion"].is_number_unsigned() ||
	    json["prefabGraphVersion"].get<Ursa::UInt32>() != prefabGraphFormatVersion)
	{
		return Result<PrefabAsset>::Failure("Unsupported or missing GameObject Prefab format version at " + label);
	}
	if (!json.contains("gameObjects") || !json["gameObjects"].is_array() || json["gameObjects"].empty())
	{
		return Result<PrefabAsset>::Failure("Invalid Prefab GameObject list at " + label);
	}
	auto resourceId = json.contains("id") && json["id"].is_string()
	                      ? ResourceId::Parse(ToUrsaString(json["id"].get<std::string>()))
	                      : Result<ResourceId>::Failure("Missing Prefab resource ID");
	if (!resourceId || json["gameObjects"].size() > 100000)
	{
		return Result<PrefabAsset>::Failure("Invalid Prefab ID or GameObject count at " + label);
	}

	struct Pending
	{
		GameObject::Ptr object;
		ResourceId parent;
		const Json* components;
	};

	Vector<Pending> pending;
	Set<String> ids;
	Set<String> componentIds;
	for (Ursa::Size index = 0; index < json["gameObjects"].size(); ++index)
	{
		const auto& item = json["gameObjects"][index];
		const String location = label + ".gameObjects[" + ToUrsaString(std::to_string(index)) + "]";
		if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("name") ||
		    !item["name"].is_string() || !item.contains("parentId") || !item.contains("enabled") ||
		    !item["enabled"].is_boolean() || !item.contains("components") || !item["components"].is_array())
		{
			return Result<PrefabAsset>::Failure("Invalid GameObject record at " + location);
		}
		auto objectId = ResourceId::Parse(ToUrsaString(item["id"].get<std::string>()));
		if (!objectId || !ids.insert(objectId.value.ToString()).second)
		{
			return Result<PrefabAsset>::Failure("Invalid or duplicate GameObject ID at " + location);
		}
		ResourceId parentId;
		if (!item["parentId"].is_null())
		{
			if (!item["parentId"].is_string())
			{
				return Result<PrefabAsset>::Failure("Invalid parent ID at " + location);
			}
			auto parsedParent = ResourceId::Parse(ToUrsaString(item["parentId"].get<std::string>()));
			if (!parsedParent)
			{
				return Result<PrefabAsset>::Failure(parsedParent.error + " at " + location);
			}
			parentId = parsedParent.value;
		}
		auto transform = ReadTransform(item.value("localTransform", Json{}), location);
		if (!transform)
		{
			return Result<PrefabAsset>::Failure(transform.error);
		}
		auto object = GameObject::Create(ToUrsaString(item["name"].get<std::string>()));
		auto reader = ArchiveFromJson(item);
		if (!reader)
		{
			return Result<PrefabAsset>::Failure(reader.error);
		}
		auto deserialized = object->Deserialize(reader.value, context);
		if (!deserialized)
		{
			return Result<PrefabAsset>::Failure(deserialized.error + " at " + location);
		}
		auto mapped = context.MapGameObject(objectId.value, object);
		if (!mapped)
		{
			return Result<PrefabAsset>::Failure(mapped.error + " at " + location);
		}
		pending.push_back({std::move(object), parentId, &item["components"]});
	}
	GameObject::Ptr root;
	for (auto& entry : pending)
	{
		if (!entry.parent)
		{
			if (root)
			{
				return Result<PrefabAsset>::Failure("Prefab must contain exactly one root at " + label);
			}
			root = entry.object;
			continue;
		}
		auto parent = context.GameObjectById(entry.parent);
		if (!parent)
		{
			return Result<PrefabAsset>::Failure("Missing parent GameObject at " + label);
		}
		auto placed = entry.object->SetParent(parent.value);
		if (!placed)
		{
			return Result<PrefabAsset>::Failure(placed.error + " at " + label);
		}
	}
	if (!root)
	{
		return Result<PrefabAsset>::Failure("Prefab has no root GameObject at " + label);
	}

	struct PendingBind
	{
		const ComponentCodec* codec;
		Component* component;
		const Json* data;
	};

	Vector<PendingBind> binds;
	for (auto& entry : pending)
	{
		for (const auto& item : *entry.components)
		{
			if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("type") ||
			    !item["type"].is_string() || !item.contains("version") || !item["version"].is_number_unsigned() ||
			    !item.contains("enabled") || !item["enabled"].is_boolean() || !item.contains("data"))
			{
				return Result<PrefabAsset>::Failure("Invalid Prefab component record at " + label);
			}
			auto componentId = ResourceId::Parse(ToUrsaString(item["id"].get<std::string>()));
			if (!componentId || !componentIds.insert(componentId.value.ToString()).second)
			{
				return Result<PrefabAsset>::Failure("Invalid or duplicate Prefab component ID at " + label);
			}
			const auto componentType = ToUrsaString(item["type"].get<std::string>());
			const auto* codec = registry.Find(componentType);
			if (!codec || codec->version != item["version"].get<Ursa::UInt32>() || !codec->createDetached)
			{
				return Result<PrefabAsset>::Failure(
				    "Prefab component codec is missing or incompatible: " + componentType + " at " + label);
			}
			auto created = codec->createDetached(entry.object, item["data"], context);
			if (!created)
			{
				return Result<PrefabAsset>::Failure("Cannot create Prefab component " + codec->type + ": " +
				                                    created.error);
			}
			created.value->SetPersistentId(componentId.value);
			auto changed = created.value->SupportsEnabled() ? created.value->SetEnabled(item["enabled"].get<bool>())
			               : item["enabled"].get<bool>()    ? Result<void>{}
			                                             : Result<void>::Failure("Data component cannot be disabled");
			if (!changed)
			{
				return Result<PrefabAsset>::Failure(changed.error);
			}
			auto mapped = context.MapComponentObject(componentId.value, created.value);
			if (!mapped)
			{
				return Result<PrefabAsset>::Failure(mapped.error);
			}
			binds.push_back({codec, created.value, &item["data"]});
		}
	}
	for (const auto& bind : binds)
	{
		if (bind.codec->bindDetached)
		{
			auto result = bind.codec->bindDetached(bind.component, *bind.data, context);
			if (!result)
			{
				return Result<PrefabAsset>::Failure("Cannot bind Prefab component " + bind.codec->type + ": " +
				                                    result.error);
			}
		}
	}
	auto resolvedReferences = context.ResolveReferences();
	if (!resolvedReferences)
	{
		return Result<PrefabAsset>::Failure(resolvedReferences.error + " at " + label);
	}
	mappings.Commit();
	return Result<PrefabAsset>::Success(PrefabAsset(resourceId.value, std::move(root)));
}

Result<PrefabAsset> LoadPrefabAssetResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                            const ComponentRegistry& registry, ComponentLoadContext& context)
{
	auto document = ReadJsonResource(filesystem, path, ResourceKind::Prefab);
	return document ? DecodePrefabAssetDocument(path.ToString(), document.value, registry, context)
	                : Result<PrefabAsset>::Failure(document.error);
}

Result<PrefabAsset>
CapturePrefabAsset(ResourceId id, const Scene& scene, GameObject::Ptr root, const ComponentRegistry& registry,
                   ComponentLoadContext& context,
                   const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId)
{
	if (!id || !root || !scene.Contains(root))
	{
		return Result<PrefabAsset>::Failure("Cannot capture an invalid PrefabAsset root");
	}
	const String scenePath = "capture.ursascene";
	auto encoded = EncodeSceneDocument(scenePath, id, scene, registry, materialId, root);
	if (!encoded)
	{
		return Result<PrefabAsset>::Failure(encoded.error);
	}
	Json objects = std::move(encoded.value["gameObjects"]);
	objects.front()["parentId"] = nullptr;
	return DecodePrefabAssetDocument("capture.ursaprefab",
	                                 {{"format", "UrsaPrefab"},
	                                  {"version", resourceFormatVersion},
	                                  {"prefabGraphVersion", prefabGraphFormatVersion},
	                                  {"id", ToStdString(id.ToString())},
	                                  {"gameObjects", std::move(objects)},
	                                  {"localResourceVersion", UInt32{1}},
	                                  {"localResources", std::move(encoded.value["localResources"])}},
	                                 registry, context);
}

Result<GameObject::Ptr> PrefabAsset::Instantiate(
    Scene& scene, const ComponentRegistry& registry, ComponentLoadContext& context,
    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId) const
{
	if (!root)
	{
		return Result<GameObject::Ptr>::Failure("Cannot instantiate an empty PrefabAsset");
	}
	ComponentMappingScope mappings(context);
	auto sourceDocument = EncodePrefabAssetDocument("instantiate.ursaprefab", *this, registry, materialId);
	if (!sourceDocument)
	{
		return Result<GameObject::Ptr>::Failure(sourceDocument.error);
	}
	DocumentResources localResources;
	DocumentResourceScope resourceScope(context, localResources);
	auto restoredResources = localResources.Decode(sourceDocument.value, context, true);
	if (!restoredResources)
	{
		return Result<GameObject::Ptr>::Failure(restoredResources.error);
	}
	DocumentResources saveResources;
	SerializationContext serialization;
	DocumentResourceScope saveScope(serialization, saveResources);
	Map<String, GameObject::Ptr> objects;

	struct PendingBind
	{
		const ComponentCodec* codec;
		Component* component;
		Json data;
	};

	Vector<PendingBind> binds;
	GameObject::Ptr instanceRoot;
	std::function<Result<GameObject::Ptr>(const GameObject::Ptr&, const GameObject::Ptr&)> clone =
	    [&](const GameObject::Ptr& source, const GameObject::Ptr& parent) -> Result<GameObject::Ptr>
	{
		auto name = source->Name();
		auto local = source->GetLocalTransform();
		auto enabled = source->ActiveSelf();
		if (!name || !local || !enabled)
		{
			return Result<GameObject::Ptr>::Failure("Cannot inspect PrefabAsset GameObject");
		}
		auto object = GameObject::Create(name.value);
		auto setTransform = object->SetLocalTransform(local.value);
		auto setEnabled = object->SetActive(enabled.value);
		auto setLayer = object->SetLayer(source->Layer().value);
		if (!setTransform || !setEnabled || !setLayer)
		{
			return Result<GameObject::Ptr>::Failure(!setTransform ? setTransform.error
			                                        : !setEnabled ? setEnabled.error
			                                                      : setLayer.error);
		}
		auto mappedObject = context.MapGameObject(source->Id(), object);
		if (!mappedObject)
		{
			return Result<GameObject::Ptr>::Failure(mappedObject.error);
		}
		objects.emplace(source->Id().ToString(), object);
		if (parent)
		{
			auto attachedParent = object->SetParent(parent);
			if (!attachedParent)
			{
				return Result<GameObject::Ptr>::Failure(attachedParent.error);
			}
		}
		for (Component* sourceComponent : source->Components())
		{
			auto codec = registry.FindFor(*sourceComponent);
			if (!codec || !codec.value->createDetached)
			{
				return Result<GameObject::Ptr>::Failure(
				    codec ? "Prefab component has no detached factory: " + codec.value->type : codec.error);
			}
			auto data = codec.value->save(*sourceComponent, ComponentSaveContext{nullptr, materialId, &serialization});
			if (!data)
			{
				return Result<GameObject::Ptr>::Failure(data.error);
			}
			auto created = codec.value->createDetached(object, data.value, context);
			if (!created)
			{
				return Result<GameObject::Ptr>::Failure(created.error);
			}
			const ResourceId newComponentId = ResourceId::Generate();
			created.value->SetPersistentId(newComponentId);
			if (created.value->SupportsEnabled())
			{
				auto changed = created.value->SetEnabled(sourceComponent->IsEnabled());
				if (!changed)
				{
					return Result<GameObject::Ptr>::Failure(changed.error);
				}
			}
			auto mappedComponent = context.MapComponentObject(sourceComponent->PersistentId(), created.value);
			if (!mappedComponent)
			{
				return Result<GameObject::Ptr>::Failure(mappedComponent.error);
			}
			binds.push_back({codec.value, created.value, std::move(data.value)});
		}
		for (const auto& sourceChild : source->Children())
		{
			auto child = clone(sourceChild, object);
			if (!child)
			{
				return child;
			}
		}
		return Result<GameObject::Ptr>::Success(std::move(object));
	};
	auto cloned = clone(root, {});
	if (!cloned)
	{
		return cloned;
	}
	instanceRoot = cloned.value;
	for (const auto& bind : binds)
	{
		if (bind.codec->bind && !bind.codec->bindDetached)
		{
			return Result<GameObject::Ptr>::Failure("Prefab component has no detached binder: " + bind.codec->type);
		}
		if (bind.codec->bindDetached)
		{
			auto result = bind.codec->bindDetached(bind.component, bind.data, context);
			if (!result)
			{
				return Result<GameObject::Ptr>::Failure(result.error);
			}
		}
	}
	auto resolvedReferences = context.ResolveReferences();
	if (!resolvedReferences)
	{
		return Result<GameObject::Ptr>::Failure(resolvedReferences.error);
	}
	auto attached = scene.AttachGameObject(instanceRoot);
	if (!attached)
	{
		return Result<GameObject::Ptr>::Failure(attached.error);
	}
	mappings.Commit();
	return Result<GameObject::Ptr>::Success(instanceRoot);
}

} // namespace Ursa
