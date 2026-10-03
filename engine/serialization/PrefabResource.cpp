#include <serialization/JsonInterop.hpp>
#include <serialization/PrefabResource.hpp>
#include <serialization/JsonResource.hpp>
#include <serialization/PrefabMerge.hpp>
#include <serialization/PrefabCapture.hpp>
#include <map>
#include <set>

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
	return pointer == "/enabled" || pointer == "/label" || pointer.rfind("/transform/", 0) == 0 ||
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
	for (size_t index = 0; index < result.bytes.size(); ++index)
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
	return {
	    {"format", "UrsaScene"},   {"version", resourceFormatVersion},
	    {"id", id.ToString()},     {"ambient", {{"color", Json::array({1.0f, 1.0f, 1.0f, 1.0f})}, {"intensity", 0.1f}}},
	    {"activeCamera", nullptr}, {"nodes", std::move(nodes)}};
}

Result<Json> ExpandInstances(const std::filesystem::path& scenePath, const Json& source, ResourceSession& resources,
                             Map<String, Map<String, String>>& scopedIds)
{
	Json expanded = source;
	if (!expanded.contains("nodes") || !expanded["nodes"].is_array())
	{
		return Result<Json>::Failure("Invalid scene nodes at " + PathToUrsaString(scenePath));
	}
	auto instances = source.find("instances");
	if (instances == source.end())
	{
		return Result<Json>::Success(std::move(expanded));
	}
	if (!instances->is_array() || instances->size() > 100000)
	{
		return Result<Json>::Failure("Invalid prefab instances at " + PathToUrsaString(scenePath));
	}
	Set<String> instanceIds;
	Set<String> objectIds;
	Set<String> regularNodes;
	for (const auto& node : expanded["nodes"])
	{
		if (!node.is_object() || !node.contains("id") || !node["id"].is_string())
		{
			return Result<Json>::Failure("Invalid regular scene node at " + PathToUrsaString(scenePath));
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
	for (size_t index = 0; index < instances->size(); ++index)
	{
		const auto& instance = (*instances)[index];
		const String location = PathToUrsaString(scenePath) + ".instances[" + ToUrsaString(std::to_string(index)) + "]";
		if (!instance.is_object() || !instance.contains("id") || !instance.contains("prefab") ||
		    !instance.contains("parent") || !instance.contains("overrides"))
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
		if (!instance["parent"].is_null())
		{
			auto externalParent = ParseId(instance["parent"], location + ".parent");
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
		if (resolved.value.kind != ResourceKind::prefab)
		{
			return Result<Json>::Failure("Referenced resource is not a Prefab at " + location);
		}
		const String prefabSource = resolved.value.path.ToString();
		auto prefabPath = resources.Index().Locate(resolved.value);
		if (!prefabPath)
		{
			return Result<Json>::Failure(prefabPath.error);
		}
		auto prefab = ReadJsonResource(prefabPath.value, ResourceKind::prefab);
		if (!prefab)
		{
			return Result<Json>::Failure(prefab.error);
		}
		if (!prefab.value.contains("nodes") || !prefab.value["nodes"].is_array() || prefab.value["nodes"].empty() ||
		    prefab.value.contains("instances"))
		{
			return Result<Json>::Failure("Invalid or nested Prefab at " + prefabSource);
		}
		const auto& overrides = instance["overrides"];
		auto merged = MergePrefabNodes(prefab.value["nodes"], overrides,
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
		if (merged.value.front().contains("parent") && !merged.value.front()["parent"].is_null())
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
			if (original["parent"].is_null())
			{
				node["parent"] = instance["parent"];
			}
			else
			{
				auto parent = scoped.find(original["parent"].get<String>());
				if (parent == scoped.end())
				{
					return Result<Json>::Failure("Missing Prefab parent at " + location + ".node[" + localId + "]");
				}
				node["parent"] = parent->second;
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
			expanded["nodes"].push_back(std::move(node));
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

Result<void> SavePrefabResource(const std::filesystem::path& path, ResourceId id, const Scene& scene, NodeId root,
                                const ComponentRegistry& registry, const MaterialIdResolver& materialId)
{
	if (!id || path.extension() != ".ursaprefab" || !scene.Contains(root))
	{
		return Result<void>::Failure("Invalid Prefab path, ID or root at " + PathToUrsaString(path));
	}
	auto encoded =
	    EncodeSceneDocument(path.parent_path() / "prefab-source.ursascene", id, scene, registry, materialId, root);
	if (!encoded)
	{
		return Result<void>::Failure(encoded.error);
	}
	Json nodes = std::move(encoded.value["nodes"]);
	nodes[0]["parent"] = nullptr;
	Json document{{"format", "UrsaPrefab"},
	              {"version", resourceFormatVersion},
	              {"id", id.ToString()},
	              {"nodes", std::move(nodes)}};
	return WriteJsonResource(path, document);
}

Result<LoadedSceneResource> LoadPrefabResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                               ComponentLoadContext& context)
{
	auto prefab = ReadJsonResource(path, ResourceKind::prefab);
	if (!prefab)
	{
		return Result<LoadedSceneResource>::Failure(prefab.error);
	}
	if (!prefab.value.contains("nodes") || !prefab.value["nodes"].is_array() || prefab.value["nodes"].empty())
	{
		return Result<LoadedSceneResource>::Failure("Invalid Prefab nodes at " + PathToUrsaString(path));
	}
	size_t roots = 0;
	for (const auto& node : prefab.value["nodes"])
	{
		if (node.is_object() && node.contains("parent") && node["parent"].is_null())
		{
			++roots;
		}
	}
	if (roots != 1 || !prefab.value["nodes"].front().is_object() || !prefab.value["nodes"].front().contains("parent") ||
	    !prefab.value["nodes"].front()["parent"].is_null())
	{
		return Result<LoadedSceneResource>::Failure("Prefab must contain one rooted subtree at " +
		                                            PathToUrsaString(path));
	}
	auto id = ParseId(prefab.value["id"], PathToUrsaString(path) + ".id");
	if (!id)
	{
		return Result<LoadedSceneResource>::Failure(id.error);
	}
	return DecodeSceneDocument(path, SceneEnvelope(id.value, prefab.value["nodes"]), registry, context);
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
	                                      {"parent", parentId ? Json(parentId.ToString()) : Json(nullptr)},
	                                      {"overrides", Json::object()}});
	return {};
}

Result<LinkedSceneResource> LoadLinkedSceneResource(const std::filesystem::path& path,
                                                    const ComponentRegistry& registry, ComponentLoadContext& context)
{
	auto document = ReadJsonResource(path, ResourceKind::scene);
	if (!document)
	{
		return Result<LinkedSceneResource>::Failure(document.error);
	}
	return DecodeLinkedSceneDocument(path, std::move(document.value), registry, context);
}

Result<LinkedSceneResource> DecodeLinkedSceneDocument(const std::filesystem::path& path, Json document,
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
		return Result<LinkedSceneResource>::Failure("Invalid linked scene at " + PathToUrsaString(path) + ": " +
		                                            error.what());
	}
}

Result<void> SaveLinkedSceneResource(const std::filesystem::path& path, LinkedSceneResource& linked,
                                     const ComponentRegistry& registry, const MaterialIdResolver& materialId)
{
	if (path.extension() != ".ursascene" || !linked.id || !linked.document.is_object() ||
	    linked.document.value("id", String{}) != linked.id.ToString())
	{
		return Result<void>::Failure("Invalid linked scene document at " + PathToUrsaString(path));
	}
	const MaterialIdResolver resolver = materialId ? materialId
	                                               : [&linked](const std::shared_ptr<MaterialInstance>& material)
	{
		return linked.resources ? linked.resources->IdentifyMaterial(material)
		                        : Result<ResourceId>::Failure("No resource session for material lookup");
	};
	try
	{
		auto captured = CaptureLinkedSceneChanges(path, linked, registry, resolver);
		return captured ? WriteJsonResource(path, linked.document) : captured;
	}
	catch (const Json::exception& error)
	{
		return Result<void>::Failure("Cannot save linked scene at " + PathToUrsaString(path) + ": " + error.what());
	}
}

Result<void> ReloadLinkedSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                       ComponentLoadContext& context, LinkedSceneResource& active)
{
	auto candidate = LoadLinkedSceneResource(path, registry, context);
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
	static const Set<String> arrays{"deletedNodes", "addedNodes", "deletedComponents", "addedComponents"};
	static const Set<String> objects{"nodeParents", "nodeOrders", "componentOwners", "componentOrders"};
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
