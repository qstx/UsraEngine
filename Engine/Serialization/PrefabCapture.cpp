#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/PrefabCapture.hpp>
#include <algorithm>
#include <functional>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

struct Indexed
{
	Map<String, Json> nodes;
	Map<String, Json> components;
	Map<String, String> owners;
	Map<String, Vector<String>> children;
	Map<String, Vector<String>> componentOrder;
};

Result<Indexed> Index(const Json& document, const String& location)
{
	if (!document.is_object() || !document.contains("gameObjects") || !document["gameObjects"].is_array())
	{
		return Result<Indexed>::Failure("Invalid scene nodes at " + location);
	}
	Indexed indexed;
	for (const auto& node : document["gameObjects"])
	{
		if (!node.is_object() || !node.contains("id") || !node["id"].is_string() || !node.contains("parentId") ||
		    !node.contains("components") || !node["components"].is_array())
		{
			return Result<Indexed>::Failure("Invalid scene node at " + location);
		}
		const auto id = node["id"].get<String>();
		if (!indexed.nodes.emplace(id, node).second)
		{
			return Result<Indexed>::Failure("Duplicate scene node " + id + " at " + location);
		}
		const auto parent = node["parentId"].is_null() ? String{} : node["parentId"].get<String>();
		indexed.children[parent].push_back(id);
		for (const auto& component : node["components"])
		{
			if (!component.is_object() || !component.contains("id") || !component["id"].is_string())
			{
				return Result<Indexed>::Failure("Invalid scene component at " + location);
			}
			const auto componentId = component["id"].get<String>();
			if (!indexed.components.emplace(componentId, component).second)
			{
				return Result<Indexed>::Failure("Duplicate scene component " + componentId + " at " + location);
			}
			indexed.owners.emplace(componentId, id);
			indexed.componentOrder[id].push_back(componentId);
		}
	}
	return Result<Indexed>::Success(std::move(indexed));
}

void AppendUnique(Json& array, const String& id)
{
	if (!array.is_array())
	{
		array = Json::array();
	}
	if (std::find(array.begin(), array.end(), Json(id)) == array.end())
	{
		array.push_back(id);
	}
}

void RemapReferences(Json& value, const Map<String, String>& reverse)
{
	if (value.is_string())
	{
		auto found = reverse.find(value.get<String>());
		if (found != reverse.end())
		{
			value = found->second;
		}
	}
	else if (value.is_array())
	{
		for (auto& entry : value)
		{
			RemapReferences(entry, reverse);
		}
	}
	else if (value.is_object())
	{
		for (auto& entry : value.items())
		{
			RemapReferences(entry.value(), reverse);
		}
	}
}

String EscapePointer(const String& part)
{
	String escaped;
	for (char character : part)
	{
		if (character == '~')
		{
			escaped += "~0";
		}
		else if (character == '/')
		{
			escaped += "~1";
		}
		else
		{
			escaped += character;
		}
	}
	return escaped;
}

void CaptureFields(const Json& before, const Json& after, const String& pointer, Json& properties,
                   const Map<String, String>& reverse)
{
	if (before == after)
	{
		return;
	}
	if (before.is_object() && after.is_object())
	{
		for (const auto& entry : after.items())
		{
			const auto child = pointer + "/" + EscapePointer(ToUrsaString(entry.key()));
			if (before.contains(entry.key()))
			{
				CaptureFields(before[entry.key()], entry.value(), child, properties, reverse);
			}
			else
			{
				Json value = entry.value();
				RemapReferences(value, reverse);
				properties[ToStdString(child)] = std::move(value);
			}
		}
		return;
	}
	if (before.is_array() && after.is_array() && before.size() == after.size())
	{
		for (Ursa::Size position = 0; position < after.size(); ++position)
		{
			CaptureFields(before[position], after[position], pointer + "/" + ToUrsaString(std::to_string(position)),
			              properties, reverse);
		}
		return;
	}
	Json value = after;
	RemapReferences(value, reverse);
	properties[ToStdString(pointer)] = std::move(value);
}

Vector<String> LocalOrder(const Vector<String>& runtimeIds, const Map<String, String>& reverse,
                          const Set<String>& scope)
{
	Vector<String> result;
	for (const auto& id : runtimeIds)
	{
		if (scope.count(id))
		{
			auto found = reverse.find(id);
			if (found != reverse.end())
			{
				result.push_back(found->second);
			}
		}
	}
	return result;
}

Result<void> CaptureInstance(const String& instanceId, Map<String, String>& known, const Indexed& baseline,
                             const Indexed& current, Json& patch, Set<String>& allCurrentInstanceNodes,
                             Set<String>& allBaselineInstanceNodes, Set<String>& allCurrentInstanceComponents,
                             const String& location)
{
	Map<String, String> reverse;
	Set<String> baselineNodes;
	Set<String> baselineComponents;
	for (const auto& entry : known)
	{
		reverse.emplace(entry.second, entry.first);
		if (baseline.nodes.count(entry.second))
		{
			baselineNodes.insert(entry.second);
		}
		if (baseline.components.count(entry.second))
		{
			baselineComponents.insert(entry.second);
		}
	}
	String root;
	for (const auto& id : baselineNodes)
	{
		const auto& parent = baseline.nodes.at(id)["parentId"];
		if (parent.is_null() || !baselineNodes.count(parent.get<String>()))
		{
			if (!root.empty())
			{
				return Result<void>::Failure("Multiple Prefab roots for instance " + instanceId + " at " + location);
			}
			root = id;
		}
	}
	if (root.empty() || !current.nodes.count(root))
	{
		return Result<void>::Failure("Missing Prefab root for instance " + instanceId + " at " + location);
	}
	Set<String> currentNodes;
	std::function<void(const String&)> visit = [&](const String& id)
	{
		if (!currentNodes.insert(id).second)
		{
			return;
		}
		for (const auto& child : current.children.count(id) ? current.children.at(id) : Vector<String>{})
		{
			visit(child);
		}
	};
	visit(root);
	for (const auto& id : baselineNodes)
	{
		if (current.nodes.count(id) && !currentNodes.count(id))
		{
			return Result<void>::Failure("Cross-instance node move " + id + " in instance " + instanceId);
		}
	}
	for (const auto& id : currentNodes)
	{
		if (!reverse.count(id))
		{
			reverse.emplace(id, id);
		}
	}
	for (const auto& id : currentNodes)
	{
		allCurrentInstanceNodes.insert(id);
		const auto& node = current.nodes.at(id);
		for (const auto& component : node["components"])
		{
			const auto componentId = component["id"].get<String>();
			allCurrentInstanceComponents.insert(componentId);
			if (!reverse.count(componentId))
			{
				reverse.emplace(componentId, componentId);
			}
		}
	}
	allBaselineInstanceNodes.insert(baselineNodes.begin(), baselineNodes.end());
	for (const auto& id : baselineNodes)
	{
		if (!currentNodes.count(id))
		{
			const auto local = reverse.at(id);
			bool wasAdded = false;
			if (patch.contains("addedGameObjects") && patch["addedGameObjects"].is_array())
			{
				for (auto iterator = patch["addedGameObjects"].begin(); iterator != patch["addedGameObjects"].end();
				     ++iterator)
				{
					if ((*iterator)["id"] == local)
					{
						patch["addedGameObjects"].erase(iterator);
						wasAdded = true;
						break;
					}
				}
			}
			if (!wasAdded)
			{
				AppendUnique(patch["deletedGameObjects"], local);
			}
			if (patch.contains("properties") && patch["properties"].is_object())
			{
				patch["properties"].erase(ToStdString(local));
			}
			if (patch.contains("gameObjectParents") && patch["gameObjectParents"].is_object())
			{
				patch["gameObjectParents"].erase(ToStdString(local));
			}
			if (patch.contains("gameObjectOrders") && patch["gameObjectOrders"].is_object())
			{
				patch["gameObjectOrders"].erase(ToStdString(local));
			}
			if (patch.contains("componentOrders") && patch["componentOrders"].is_object())
			{
				patch["componentOrders"].erase(ToStdString(local));
			}
			if (wasAdded && patch.contains("addedComponents") && patch["addedComponents"].is_array())
			{
				auto& additions = patch["addedComponents"];
				for (auto iterator = additions.begin(); iterator != additions.end();)
				{
					if ((*iterator)["owner"] == local)
					{
						const auto componentLocal = (*iterator)["component"]["id"].get<String>();
						known.erase(componentLocal);
						iterator = additions.erase(iterator);
					}
					else
					{
						++iterator;
					}
				}
			}
			known.erase(local);
		}
	}
	for (const auto& id : baselineComponents)
	{
		if (!allCurrentInstanceComponents.count(id))
		{
			const auto local = reverse.at(id);
			if (!currentNodes.count(baseline.owners.at(id)))
			{
				if (patch.contains("properties") && patch["properties"].is_object())
				{
					patch["properties"].erase(ToStdString(local));
				}
				if (patch.contains("componentOwners") && patch["componentOwners"].is_object())
				{
					patch["componentOwners"].erase(ToStdString(local));
				}
				known.erase(local);
				continue;
			}
			bool wasAdded = false;
			if (patch.contains("addedComponents") && patch["addedComponents"].is_array())
			{
				for (auto iterator = patch["addedComponents"].begin(); iterator != patch["addedComponents"].end();
				     ++iterator)
				{
					if ((*iterator)["component"]["id"] == local)
					{
						patch["addedComponents"].erase(iterator);
						wasAdded = true;
						break;
					}
				}
			}
			if (!wasAdded)
			{
				AppendUnique(patch["deletedComponents"], local);
			}
			if (patch.contains("properties") && patch["properties"].is_object())
			{
				patch["properties"].erase(ToStdString(local));
			}
			if (patch.contains("componentOwners") && patch["componentOwners"].is_object())
			{
				patch["componentOwners"].erase(ToStdString(local));
			}
			known.erase(local);
		}
	}
	for (const auto& id : currentNodes)
	{
		const auto& node = current.nodes.at(id);
		const auto localId = reverse.at(id);
		if (!baselineNodes.count(id))
		{
			Json added = node;
			added["id"] = localId;
			if (node["parentId"].is_null() || !reverse.count(node["parentId"].get<String>()))
			{
				return Result<void>::Failure("Cross-instance parent for added node " + localId + " at " + location);
			}
			added["parentId"] = reverse.at(node["parentId"].get<String>());
			added["components"] = Json::array();
			patch["addedGameObjects"].push_back(std::move(added));
			known.emplace(localId, id);
		}
		else
		{
			const auto& old = baseline.nodes.at(id);
			Json changes = Json::object();
			CaptureFields(old["localTransform"], node["localTransform"], "/localTransform", changes, reverse);
			CaptureFields(old.value("name", Json("")), node.value("name", Json("")), "/name", changes, reverse);
			CaptureFields(old["enabled"], node["enabled"], "/enabled", changes, reverse);
			if (!changes.empty())
			{
				auto& properties = patch["properties"][ToStdString(localId)];
				if (!properties.is_object())
				{
					properties = Json::object();
				}
				properties.update(changes);
			}
			if (old["parentId"] != node["parentId"] && id != root)
			{
				if (node["parentId"].is_null() || !reverse.count(node["parentId"].get<String>()))
				{
					return Result<void>::Failure("Cross-instance parent for node " + localId + " at " + location);
				}
				patch["gameObjectParents"][ToStdString(localId)] = reverse.at(node["parentId"].get<String>());
			}
		}
		for (const auto& component : node["components"])
		{
			const auto runtimeId = component["id"].get<String>();
			const auto localComponentId = reverse.at(runtimeId);
			if (!baselineComponents.count(runtimeId))
			{
				Json addedComponent = component;
				addedComponent["id"] = localComponentId;
				RemapReferences(addedComponent["data"], reverse);
				patch["addedComponents"].push_back({{"owner", localId}, {"component", std::move(addedComponent)}});
				known.emplace(localComponentId, runtimeId);
			}
			else
			{
				const auto& old = baseline.components.at(runtimeId);
				Json changes = Json::object();
				CaptureFields(old["data"], component["data"], "/data", changes, reverse);
				CaptureFields(old["enabled"], component["enabled"], "/enabled", changes, reverse);
				if (!changes.empty())
				{
					auto& properties = patch["properties"][ToStdString(localComponentId)];
					if (!properties.is_object())
					{
						properties = Json::object();
					}
					properties.update(changes);
				}
				if (baseline.owners.at(runtimeId) != id)
				{
					patch["componentOwners"][ToStdString(localComponentId)] = localId;
				}
			}
		}
	}
	for (const auto& id : currentNodes)
	{
		const auto localId = reverse.at(id);
		const auto currentChildren =
		    LocalOrder(current.children.count(id) ? current.children.at(id) : Vector<String>{}, reverse, currentNodes);
		const auto oldChildren = LocalOrder(baseline.children.count(id) ? baseline.children.at(id) : Vector<String>{},
		                                    reverse, baselineNodes);
		if (currentChildren != oldChildren && !currentChildren.empty())
		{
			patch["gameObjectOrders"][ToStdString(localId)] = currentChildren;
		}
		const auto currentComponents =
		    LocalOrder(current.componentOrder.count(id) ? current.componentOrder.at(id) : Vector<String>{}, reverse,
		               allCurrentInstanceComponents);
		const auto oldComponents =
		    LocalOrder(baseline.componentOrder.count(id) ? baseline.componentOrder.at(id) : Vector<String>{}, reverse,
		               baselineComponents);
		if (currentComponents != oldComponents && !currentComponents.empty())
		{
			patch["componentOrders"][ToStdString(localId)] = currentComponents;
		}
	}
	return {};
}
} // namespace

Result<void> CaptureLinkedSceneChanges(const String& path, LinkedSceneResource& linked,
                                       const ComponentRegistry& registry, const MaterialIdResolver& materialId)
{
	auto encoded = EncodeSceneDocument(path, linked.id, *linked.scene, registry, materialId);
	if (!encoded)
	{
		return Result<void>::Failure(encoded.error);
	}
	auto baseline = Index(linked.snapshot, path + " previous snapshot");
	auto current = Index(encoded.value, path + " current scene");
	if (!baseline || !current)
	{
		return Result<void>::Failure(!baseline ? baseline.error : current.error);
	}
	Json candidate = linked.document;
	Set<String> currentInstanceNodes;
	Set<String> baselineInstanceNodes;
	Set<String> currentInstanceComponents;
	auto updatedScopes = linked.scopedIds;
	for (auto& instance : candidate["instances"])
	{
		const auto id = instance["id"].get<String>();
		auto mapping = updatedScopes.find(id);
		if (mapping == updatedScopes.end())
		{
			return Result<void>::Failure("Missing Prefab scope for instance " + id + " at " + path);
		}
		auto result = CaptureInstance(id, mapping->second, baseline.value, current.value, instance["overrides"],
		                              currentInstanceNodes, baselineInstanceNodes, currentInstanceComponents, path);
		if (!result)
		{
			return result;
		}
	}
	for (const auto& id : currentInstanceNodes)
	{
		if (baseline.value.nodes.count(id) && !baselineInstanceNodes.count(id))
		{
			return Result<void>::Failure("Regular node moved into Prefab instance: " + id);
		}
	}
	candidate["gameObjects"] = Json::array();
	for (const auto& node : encoded.value["gameObjects"])
	{
		if (!currentInstanceNodes.count(node["id"].get<String>()))
		{
			candidate["gameObjects"].push_back(node);
		}
	}
	candidate["ambient"] = encoded.value["ambient"];
	candidate["activeCamera"] = encoded.value["activeCamera"];
	linked.document = std::move(candidate);
	linked.snapshot = std::move(encoded.value);
	linked.scopedIds = std::move(updatedScopes);
	return {};
}
} // namespace Ursa
