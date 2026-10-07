#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/PrefabMerge.hpp>
#include <Serialization/PrefabCapture.hpp>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

Result<Json*> FindInstance(Json& document, ResourceId id)
{
	if (!id || !document.is_object() || !document.contains("instances") || !document["instances"].is_array())
	{
		return Result<Json*>::Failure("Invalid scene or Prefab instance ID " + id.ToString());
	}
	for (auto& instance : document["instances"])
	{
		if (instance.is_object() && instance.value("id", String{}) == id.ToString())
		{
			return Result<Json*>::Success(&instance);
		}
	}
	return Result<Json*>::Failure("Missing Prefab instance " + id.ToString());
}

bool AllowedPropertyPath(const String& pointer)
{
	return pointer == "/enabled" || pointer == "/name" || pointer.rfind("/localTransform/", 0) == 0 ||
	       pointer.rfind("/data/", 0) == 0;
}

Result<void> ApplyProperties(Json& object, const Json& properties, const String& location)
{
	if (!properties.is_object())
	{
		return Result<void>::Failure("Invalid Prefab property overrides at " + location);
	}
	for (const auto& entry : properties.items())
	{
		if (!AllowedPropertyPath(ToUrsaString(entry.key())))
		{
			return Result<void>::Failure("Invalid Prefab property path " + ToUrsaString(entry.key()) + " at " +
			                             location);
		}
		try
		{
			const Json::json_pointer pointer(entry.key());
			if (!object.contains(pointer))
			{
				return Result<void>::Failure("Missing Prefab property target " + ToUrsaString(entry.key()) + " at " +
				                             location);
			}
			object[pointer] = entry.value();
		}
		catch (const Json::exception& error)
		{
			return Result<void>::Failure("Invalid Prefab property path " + ToUrsaString(entry.key()) + " at " +
			                             location + ": " + error.what());
		}
	}
	return {};
}

Result<ResourceId> ParseId(const Json& value, const String& location)
{
	if (!value.is_string())
	{
		return Result<ResourceId>::Failure("Invalid ID at " + location);
	}
	auto id = ResourceId::Parse(value.get<String>());
	return id ? id : Result<ResourceId>::Failure(id.error + " at " + location);
}

ResourceId ScopedId(ResourceId instance, ResourceId local)
{
	ResourceId result;
	for (Ursa::Size index = 0; index < result.bytes.size(); ++index)
	{
		result.bytes[index] = instance.bytes[index] ^ local.bytes[index];
	}
	if (!result)
	{
		result.bytes[0] = 1;
	}
	return result;
}

Json SceneEnvelope(ResourceId id, Json nodes)
{
	return {{"format", "UrsaScene"},
	        {"version", resourceFormatVersion},
	        {"sceneGraphVersion", sceneGraphFormatVersion},
	        {"id", id.ToString()},
	        {"ambient", {{"color", Json::array({1.0f, 1.0f, 1.0f, 1.0f})}, {"intensity", 0.1f}}},
	        {"activeCamera", nullptr},
	        {"gameObjects", std::move(nodes)}};
}

Result<Json> ExpandInstances(const String& scenePath, const Json& source, ResourceSession& resources,
                             Map<String, Map<String, String>>& scopedIds)
{
	Json expanded = source;
	auto graphVersion = source.find("sceneGraphVersion");
	if (graphVersion == source.end() || !graphVersion->is_number_unsigned() ||
	    graphVersion->get<Ursa::UInt32>() != sceneGraphFormatVersion)
	{
		return Result<Json>::Failure("Unsupported or missing GameObject Scene format version at " + scenePath);
	}
	if (!expanded.contains("gameObjects") || !expanded["gameObjects"].is_array())
	{
		return Result<Json>::Failure("Invalid scene nodes at " + scenePath);
	}
	auto instances = source.find("instances");
	if (instances == source.end())
	{
		return Result<Json>::Success(std::move(expanded));
	}
	if (!instances->is_array() || instances->size() > 100000)
	{
		return Result<Json>::Failure("Invalid prefab instances at " + scenePath);
	}
	Set<String> instanceIds;
	Set<String> objectIds;
	Set<String> regularNodes;
	for (const auto& node : expanded["gameObjects"])
	{
		if (!node.is_object() || !node.contains("id") || !node["id"].is_string())
		{
			return Result<Json>::Failure("Invalid regular scene node at " + scenePath);
		}
		objectIds.insert(node["id"].get<String>());
		regularNodes.insert(node["id"].get<String>());
		if (node.contains("components") && node["components"].is_array())
		{
			for (const auto& component : node["components"])
			{
				if (component.is_object() && component.contains("id") && component["id"].is_string())
				{
					objectIds.insert(component["id"].get<String>());
				}
			}
		}
	}
	for (Ursa::Size index = 0; index < instances->size(); ++index)
	{
		const auto& instance = (*instances)[index];
		const String location = scenePath + ".instances[" + ToUrsaString(std::to_string(index)) + "]";
		if (!instance.is_object() || !instance.contains("id") || !instance.contains("prefab") ||
		    !instance.contains("parentId") || !instance.contains("overrides"))
		{
			return Result<Json>::Failure("Invalid prefab instance at " + location);
		}
		auto instanceId = ParseId(instance["id"], location + ".id");
		auto prefabId = ParseId(instance["prefab"], location + ".prefab");
		if (!instanceId || !prefabId)
		{
			return Result<Json>::Failure(!instanceId ? instanceId.error : prefabId.error);
		}
		if (!instanceIds.insert(instanceId.value.ToString()).second)
		{
			return Result<Json>::Failure("Duplicate prefab instance ID " + instanceId.value.ToString() + " at " +
			                             location);
		}
		if (!instance["parentId"].is_null())
		{
			auto externalParent = ParseId(instance["parentId"], location + ".parent");
			if (!externalParent)
			{
				return Result<Json>::Failure(externalParent.error);
			}
			if (!regularNodes.count(externalParent.value.ToString()))
			{
				return Result<Json>::Failure("Cross-instance or missing external parent " +
				                             externalParent.value.ToString() + " at " + location);
			}
		}
		if (!instance["overrides"].is_object())
		{
			return Result<Json>::Failure("Invalid prefab overrides at " + location);
		}
		auto resolved = resources.Index().Resolve(prefabId.value, location);
		if (!resolved)
		{
			return Result<Json>::Failure(resolved.error);
		}
		if (resolved.value.kind != ResourceKind::Prefab)
		{
			return Result<Json>::Failure("Referenced resource is not a Prefab at " + location);
		}
		const String prefabSource = resolved.value.path.ToString();
		auto prefab = ReadJsonResource(resources.Index().Filesystem(), resolved.value.path, ResourceKind::Prefab);
		if (!prefab)
		{
			return Result<Json>::Failure(prefab.error);
		}
		if (!prefab.value.contains("gameObjects") || !prefab.value["gameObjects"].is_array() ||
		    prefab.value["gameObjects"].empty() || prefab.value.contains("instances"))
		{
			return Result<Json>::Failure("Invalid or nested Prefab at " + prefabSource);
		}
		const auto& overrides = instance["overrides"];
		auto merged = MergePrefabNodes(prefab.value["gameObjects"], overrides,
		                               "Prefab " + prefabId.value.ToString() + " instance " +
		                                   instanceId.value.ToString() + " at " + location);
		if (!merged)
		{
			return Result<Json>::Failure(merged.error);
		}
		Map<String, String> scoped;
		for (const auto& node : merged.value)
		{
			if (!node.is_object() || !node.contains("id") || !node["id"].is_string() || !node.contains("components") ||
			    !node["components"].is_array())
			{
				return Result<Json>::Failure("Invalid Prefab node at " + prefabSource);
			}
			auto local = ParseId(node["id"], prefabSource + ".nodes.id");
			if (!local)
			{
				return Result<Json>::Failure(local.error);
			}
			const auto mapped = ScopedId(instanceId.value, local.value).ToString();
			if (!scoped.emplace(local.value.ToString(), mapped).second || !objectIds.insert(mapped).second)
			{
				return Result<Json>::Failure("Duplicate Prefab node ID " + local.value.ToString() + " at " + location);
			}
			for (const auto& component : node["components"])
			{
				if (!component.is_object() || !component.contains("id"))
				{
					return Result<Json>::Failure("Invalid Prefab component at " + location);
				}
				auto componentId = ParseId(component["id"], location + ".component.id");
				if (!componentId)
				{
					return Result<Json>::Failure(componentId.error);
				}
				const auto mappedComponent = ScopedId(instanceId.value, componentId.value).ToString();
				if (!scoped.emplace(componentId.value.ToString(), mappedComponent).second ||
				    !objectIds.insert(mappedComponent).second)
				{
					return Result<Json>::Failure("Duplicate Prefab component ID " + componentId.value.ToString() +
					                             " at " + location);
				}
			}
		}
		if (merged.value.front().contains("parentId") && !merged.value.front()["parentId"].is_null())
		{
			return Result<Json>::Failure("Prefab root has a parent at " + prefabSource);
		}
		auto properties = overrides.find("properties");
		if (properties != overrides.end() && !properties->is_object())
		{
			return Result<Json>::Failure("Invalid Prefab properties at " + location);
		}
		Set<String> usedProperties;
		for (const auto& original : merged.value)
		{
			Json node = original;
			const auto localId = original["id"].get<String>();
			if (properties != overrides.end() && properties->contains(ToStdString(localId)))
			{
				auto applied =
				    ApplyProperties(node, (*properties)[ToStdString(localId)], location + ".node[" + localId + "]");
				if (!applied)
				{
					return Result<Json>::Failure(applied.error);
				}
				usedProperties.insert(localId);
			}
			node["id"] = scoped.at(localId);
			if (original["parentId"].is_null())
			{
				node["parentId"] = instance["parentId"];
			}
			else
			{
				auto parent = scoped.find(original["parentId"].get<String>());
				if (parent == scoped.end())
				{
					return Result<Json>::Failure("Missing Prefab parent at " + location + ".node[" + localId + "]");
				}
				node["parentId"] = parent->second;
			}
			for (auto& component : node["components"])
			{
				const auto componentLocalId = component["id"].get<String>();
				if (properties != overrides.end() && properties->contains(ToStdString(componentLocalId)))
				{
					auto applied = ApplyProperties(component, (*properties)[ToStdString(componentLocalId)],
					                               location + ".component[" + componentLocalId + "]");
					if (!applied)
					{
						return Result<Json>::Failure(applied.error);
					}
					usedProperties.insert(componentLocalId);
				}
				component["id"] = scoped.at(componentLocalId);
				std::function<void(Json&)> remap = [&](Json& value)
				{
					if (value.is_string())
					{
						auto found = scoped.find(value.get<String>());
						if (found != scoped.end())
						{
							value = found->second;
						}
					}
					else if (value.is_array())
					{
						for (auto& item : value)
						{
							remap(item);
						}
					}
					else if (value.is_object())
					{
						for (auto& item : value.items())
						{
							remap(item.value());
						}
					}
				};
				remap(component["data"]);
			}
			expanded["gameObjects"].push_back(std::move(node));
		}
		if (properties != overrides.end())
		{
			for (const auto& entry : properties->items())
			{
				if (!usedProperties.count(ToUrsaString(entry.key())))
				{
					return Result<Json>::Failure("Prefab " + prefabId.value.ToString() + " instance " +
					                             instanceId.value.ToString() + " missing property target " +
					                             ToUrsaString(entry.key()) + " at " + location);
				}
			}
		}
		scopedIds.emplace(instanceId.value.ToString(), std::move(scoped));
	}
	return Result<Json>::Success(std::move(expanded));
}
} // namespace

Result<void> SavePrefabResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                                const Scene& scene, GameObject::Ptr root, const ComponentRegistry& registry,
                                const MaterialIdResolver& materialId)
{
	if (!id || std::filesystem::path(ToStdString(path.relative)).extension() != ".ursaprefab" || !scene.Contains(root))
	{
		return Result<void>::Failure("Invalid Prefab path, ID or root at " + path.ToString());
	}
	auto encoded = EncodeSceneDocument(path.ToString() + ".ursascene", id, scene, registry, materialId, root);
	if (!encoded)
	{
		return Result<void>::Failure(encoded.error);
	}
	Json nodes = std::move(encoded.value["gameObjects"]);
	nodes[0]["parentId"] = nullptr;
	Json document{{"format", "UrsaPrefab"},
	              {"version", resourceFormatVersion},
	              {"prefabGraphVersion", prefabGraphFormatVersion},
	              {"id", id.ToString()},
	              {"gameObjects", std::move(nodes)}};
	return WriteJsonResource(filesystem, path, document);
}

Result<LoadedSceneResource> LoadPrefabResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                               const ComponentRegistry& registry, ComponentLoadContext& context)
{
	auto prefab = ReadJsonResource(filesystem, path, ResourceKind::Prefab);
	if (!prefab)
	{
		return Result<LoadedSceneResource>::Failure(prefab.error);
	}
	const auto graphVersion = prefab.value.find("prefabGraphVersion");
	if (graphVersion == prefab.value.end() || !graphVersion->is_number_unsigned() ||
	    graphVersion->get<Ursa::UInt32>() != prefabGraphFormatVersion)
	{
		return Result<LoadedSceneResource>::Failure("Unsupported or missing GameObject Prefab format version at " +
		                                            path.ToString());
	}
	if (!prefab.value.contains("gameObjects") || !prefab.value["gameObjects"].is_array() ||
	    prefab.value["gameObjects"].empty())
	{
		return Result<LoadedSceneResource>::Failure("Invalid Prefab nodes at " + path.ToString());
	}
	Ursa::Size roots = 0;
	for (const auto& node : prefab.value["gameObjects"])
	{
		if (node.is_object() && node.contains("parentId") && node["parentId"].is_null())
		{
			++roots;
		}
	}
	if (roots != 1 || !prefab.value["gameObjects"].front().is_object() ||
	    !prefab.value["gameObjects"].front().contains("parentId") ||
	    !prefab.value["gameObjects"].front()["parentId"].is_null())
	{
		return Result<LoadedSceneResource>::Failure("Prefab must contain one rooted subtree at " + path.ToString());
	}
	auto id = ParseId(prefab.value["id"], path.ToString() + ".id");
	if (!id)
	{
		return Result<LoadedSceneResource>::Failure(id.error);
	}
	return DecodeSceneDocument(path.ToString(), SceneEnvelope(id.value, prefab.value["gameObjects"]), registry,
	                           context);
}

Result<void> AddPrefabInstance(Json& sceneDocument, ResourceId prefabId, ResourceId instanceId, ResourceId parentId)
{
	if (!prefabId || !instanceId || !sceneDocument.is_object())
	{
		return Result<void>::Failure("Invalid Prefab instance or scene document");
	}
	if (!sceneDocument.contains("instances"))
	{
		sceneDocument["instances"] = Json::array();
	}
	if (!sceneDocument["instances"].is_array())
	{
		return Result<void>::Failure("Invalid scene instances array");
	}
	for (const auto& existing : sceneDocument["instances"])
	{
		if (existing.is_object() && existing.value("id", String{}) == instanceId.ToString())
		{
			return Result<void>::Failure("Duplicate Prefab instance ID " + instanceId.ToString());
		}
	}
	sceneDocument["instances"].push_back({{"id", instanceId.ToString()},
	                                      {"prefab", prefabId.ToString()},
	                                      {"parentId", parentId ? Json(parentId.ToString()) : Json(nullptr)},
	                                      {"overrides", Json::object()}});
	return {};
}

Result<LinkedSceneResource> LoadLinkedSceneResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                                    const ComponentRegistry& registry, ComponentLoadContext& context)
{
	auto document = ReadJsonResource(filesystem, path, ResourceKind::Scene);
	if (!document)
	{
		return Result<LinkedSceneResource>::Failure(document.error);
	}
	return DecodeLinkedSceneDocument(path.ToString(), std::move(document.value), registry, context);
}

Result<LinkedSceneResource> DecodeLinkedSceneDocument(const String& path, Json document,
                                                      const ComponentRegistry& registry, ComponentLoadContext& context)
{
	try
	{
		Map<String, Map<String, String>> scopedIds;
		auto expanded = ExpandInstances(path, document, context.Resources(), scopedIds);
		if (!expanded)
		{
			return Result<LinkedSceneResource>::Failure(expanded.error);
		}
		auto loaded = DecodeSceneDocument(path, expanded.value, registry, context);
		if (!loaded)
		{
			return Result<LinkedSceneResource>::Failure(loaded.error);
		}
		return Result<LinkedSceneResource>::Success({loaded.value.id, std::move(loaded.value.scene),
		                                             std::move(document), std::move(expanded.value),
		                                             std::move(scopedIds), &context.Resources()});
	}
	catch (const Json::exception& error)
	{
		return Result<LinkedSceneResource>::Failure("Invalid linked scene at " + path + ": " + error.what());
	}
}

Result<void> SaveLinkedSceneResource(VirtualFileSystem& filesystem, const VirtualPath& path,
                                     LinkedSceneResource& linked, const ComponentRegistry& registry,
                                     const MaterialIdResolver& materialId)
{
	if (std::filesystem::path(ToStdString(path.relative)).extension() != ".ursascene" || !linked.id ||
	    !linked.document.is_object() || linked.document.value("id", String{}) != linked.id.ToString())
	{
		return Result<void>::Failure("Invalid linked scene document at " + path.ToString());
	}
	const MaterialIdResolver resolver = materialId ? materialId
	                                               : [&linked](const std::shared_ptr<MaterialInstance>& material)
	{
		return linked.resources ? linked.resources->IdentifyMaterial(material)
		                        : Result<ResourceId>::Failure("No resource session for material lookup");
	};
	try
	{
		auto captured = CaptureLinkedSceneChanges(path.ToString(), linked, registry, resolver);
		return captured ? WriteJsonResource(filesystem, path, linked.document) : captured;
	}
	catch (const Json::exception& error)
	{
		return Result<void>::Failure("Cannot save linked scene at " + path.ToString() + ": " + error.what());
	}
}

Result<void> ReloadLinkedSceneResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                       const ComponentRegistry& registry, ComponentLoadContext& context,
                                       LinkedSceneResource& active)
{
	auto candidate = LoadLinkedSceneResource(filesystem, path, registry, context);
	if (!candidate)
	{
		return Result<void>::Failure(candidate.error);
	}
	active = std::move(candidate.value);
	return {};
}

Result<void> SetPrefabPropertyOverride(LinkedSceneResource& linked, ResourceId instanceId, ResourceId objectId,
                                       const String& pointer, const Json& value)
{
	if (!objectId || !AllowedPropertyPath(pointer))
	{
		return Result<void>::Failure("Invalid Prefab property target or path " + pointer);
	}
	try
	{
		Json::json_pointer checked(ToStdString(pointer));
		(void)checked;
	}
	catch (const Json::exception& error)
	{
		return Result<void>::Failure(String("Invalid Prefab property path: ") + error.what());
	}
	auto instance = FindInstance(linked.document, instanceId);
	if (!instance)
	{
		return Result<void>::Failure(instance.error);
	}
	(*instance.value)["overrides"]["properties"][ToStdString(objectId.ToString())][ToStdString(pointer)] = value;
	return {};
}

Result<void> ClearPrefabPropertyOverride(LinkedSceneResource& linked, ResourceId instanceId, ResourceId objectId,
                                         const String& pointer)
{
	auto instance = FindInstance(linked.document, instanceId);
	if (!instance)
	{
		return Result<void>::Failure(instance.error);
	}
	auto& overrides = (*instance.value)["overrides"];
	if (!overrides.contains("properties") || !overrides["properties"].contains(ToStdString(objectId.ToString())) ||
	    !overrides["properties"][ToStdString(objectId.ToString())].contains(ToStdString(pointer)))
	{
		return Result<void>::Failure("Missing Prefab property override " + objectId.ToString() + pointer);
	}
	overrides["properties"][ToStdString(objectId.ToString())].erase(ToStdString(pointer));
	if (overrides["properties"][ToStdString(objectId.ToString())].empty())
	{
		overrides["properties"].erase(ToStdString(objectId.ToString()));
	}
	return {};
}

Result<void> SetPrefabStructureOverride(LinkedSceneResource& linked, ResourceId instanceId, const String& operation,
                                        const Json& value)
{
	static const Set<String> arrays{"deletedGameObjects", "addedGameObjects", "deletedComponents", "addedComponents"};
	static const Set<String> objects{"gameObjectParents", "gameObjectOrders", "componentOwners", "componentOrders"};
	if ((arrays.count(operation) && !value.is_array()) || (objects.count(operation) && !value.is_object()) ||
	    (!arrays.count(operation) && !objects.count(operation)))
	{
		return Result<void>::Failure("Invalid Prefab structural operation " + operation);
	}
	auto instance = FindInstance(linked.document, instanceId);
	if (!instance)
	{
		return Result<void>::Failure(instance.error);
	}
	(*instance.value)["overrides"][ToStdString(operation)] = value;
	return {};
}

} // namespace Ursa
