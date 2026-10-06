#include <Core/Containers.hpp>
#include <Serialization/JsonInterop.hpp>
#include <Serialization/PrefabMerge.hpp>
#include <algorithm>
#include <functional>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

Result<String> IdOf(const Json& object, const String& location)
{
	if (!object.is_object() || !object.contains("id") || !object["id"].is_string())
	{
		return Result<String>::Failure("Missing object ID at " + location);
	}
	const auto id = object["id"].get<String>();
	if (id.empty())
	{
		return Result<String>::Failure("Empty object ID at " + location);
	}
	return Result<String>::Success(id);
}

Result<void> RequireArray(const Json& overrides, const char* name, const String& location)
{
	if (overrides.contains(name) && !overrides[name].is_array())
	{
		return Result<void>::Failure("Invalid " + String(name) + " at " + location);
	}
	return {};
}

Result<void> RequireObject(const Json& overrides, const char* name, const String& location)
{
	if (overrides.contains(name) && !overrides[name].is_object())
	{
		return Result<void>::Failure("Invalid " + String(name) + " at " + location);
	}
	return {};
}

Result<void> Reorder(Vector<String>& ids, const Json& fixed, const String& location)
{
	if (!fixed.is_array())
	{
		return Result<void>::Failure("Invalid order at " + location);
	}
	Set<String> seen;
	Vector<String> ordered;
	for (const auto& item : fixed)
	{
		if (!item.is_string())
		{
			return Result<void>::Failure("Invalid ordered ID at " + location);
		}
		const auto id = item.get<String>();
		if (std::find(ids.begin(), ids.end(), id) == ids.end() || !seen.insert(id).second)
		{
			return Result<void>::Failure("Missing or repeated order target " + id + " at " + location);
		}
		ordered.push_back(id);
	}
	for (const auto& id : ids)
	{
		if (!seen.count(id))
		{
			ordered.push_back(id);
		}
	}
	ids = std::move(ordered);
	return {};
}
} // namespace

Result<Json> MergePrefabNodes(const Json& source, const Json& overrides, const String& location)
{
	if (!source.is_array() || source.empty() || !overrides.is_object())
	{
		return Result<Json>::Failure("Invalid Prefab source or overrides at " + location);
	}
	static const Set<String> supported{"properties",        "deletedGameObjects", "addedGameObjects",
	                                   "gameObjectParents", "gameObjectOrders",   "deletedComponents",
	                                   "addedComponents",   "componentOwners",    "componentOrders"};
	for (const auto& entry : overrides.items())
	{
		if (!supported.count(ToUrsaString(entry.key())))
		{
			return Result<Json>::Failure("Unknown Prefab override operation " + ToUrsaString(entry.key()) + " at " +
			                             location);
		}
	}
	for (const char* name : {"deletedGameObjects", "addedGameObjects", "deletedComponents", "addedComponents"})
	{
		auto checked = RequireArray(overrides, name, location);
		if (!checked)
		{
			return Result<Json>::Failure(checked.error);
		}
	}
	for (const char* name : {"gameObjectParents", "gameObjectOrders", "componentOwners", "componentOrders"})
	{
		auto checked = RequireObject(overrides, name, location);
		if (!checked)
		{
			return Result<Json>::Failure(checked.error);
		}
	}
	Map<String, Json> nodes;
	Vector<String> sourceOrder;
	Set<String> componentIds;
	for (const auto& node : source)
	{
		auto id = IdOf(node, location + ".sourceNode");
		if (!id || !node.contains("parentId") || !node.contains("components") || !node["components"].is_array())
		{
			return Result<Json>::Failure(!id ? id.error : "Invalid Prefab node at " + location);
		}
		if (!nodes.emplace(id.value, node).second)
		{
			return Result<Json>::Failure("Duplicate Prefab node ID " + id.value + " at " + location);
		}
		sourceOrder.push_back(id.value);
		for (const auto& component : node["components"])
		{
			auto componentId = IdOf(component, location + ".component");
			if (!componentId || !componentIds.insert(componentId.value).second)
			{
				return Result<Json>::Failure("Duplicate or invalid Prefab component ID at " + location);
			}
		}
	}
	const auto root = sourceOrder.front();
	if (!nodes[root]["parentId"].is_null())
	{
		return Result<Json>::Failure("Prefab root has a parent at " + location);
	}
	if (overrides.contains("deletedGameObjects"))
	{
		Set<String> deleted;
		for (const auto& item : overrides["deletedGameObjects"])
		{
			if (!item.is_string())
			{
				return Result<Json>::Failure("Invalid deleted node ID at " + location);
			}
			const auto id = item.get<String>();
			if (id == root || !nodes.count(id) || !deleted.insert(id).second)
			{
				return Result<Json>::Failure("Missing or invalid deleted node " + id + " at " + location);
			}
		}
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (const auto& entry : nodes)
			{
				if (!entry.second["parentId"].is_null() && deleted.count(entry.second["parentId"].get<String>()) &&
				    !deleted.count(entry.first))
				{
					deleted.insert(entry.first);
					changed = true;
				}
			}
		}
		for (const auto& id : deleted)
		{
			nodes.erase(id);
		}
	}
	if (overrides.contains("addedGameObjects"))
	{
		for (const auto& node : overrides["addedGameObjects"])
		{
			auto id = IdOf(node, location + ".addedNode");
			if (!id || !node.contains("parentId") || !node["parentId"].is_string() || !node.contains("components") ||
			    !node["components"].is_array())
			{
				return Result<Json>::Failure(!id ? id.error : "Invalid added node at " + location);
			}
			if (!nodes.emplace(id.value, node).second)
			{
				return Result<Json>::Failure("Duplicate added node ID " + id.value + " at " + location);
			}
			sourceOrder.push_back(id.value);
			for (const auto& component : node["components"])
			{
				auto componentId = IdOf(component, location + ".addedNode.component");
				if (!componentId || !componentIds.insert(componentId.value).second)
				{
					return Result<Json>::Failure("Duplicate added component ID at " + location);
				}
			}
		}
	}
	if (overrides.contains("gameObjectParents"))
	{
		for (const auto& entry : overrides["gameObjectParents"].items())
		{
			if (ToUrsaString(entry.key()) == root || !nodes.count(ToUrsaString(entry.key())) ||
			    !entry.value().is_string())
			{
				return Result<Json>::Failure("Missing or invalid reparent target " + ToUrsaString(entry.key()) +
				                             " at " + location);
			}
			const auto parent = entry.value().get<String>();
			if (!nodes.count(parent))
			{
				return Result<Json>::Failure("Cross-instance or missing parent " + parent + " at " + location);
			}
			nodes[ToUrsaString(entry.key())]["parentId"] = parent;
		}
	}
	for (const auto& entry : nodes)
	{
		if (entry.first == root)
		{
			continue;
		}
		if (!entry.second["parentId"].is_string() || !nodes.count(entry.second["parentId"].get<String>()))
		{
			return Result<Json>::Failure("Missing Prefab parent for node " + entry.first + " at " + location);
		}
		Set<String> visited;
		String current = entry.first;
		while (current != root)
		{
			if (!visited.insert(current).second)
			{
				return Result<Json>::Failure("Prefab parent cycle at node " + current + " in " + location);
			}
			current = nodes[current]["parentId"].get<String>();
		}
	}
	if (overrides.contains("deletedComponents"))
	{
		for (const auto& item : overrides["deletedComponents"])
		{
			if (!item.is_string())
			{
				return Result<Json>::Failure("Invalid deleted component ID at " + location);
			}
			const auto id = item.get<String>();
			bool found = false;
			for (auto& entry : nodes)
			{
				auto& components = entry.second["components"];
				for (auto iterator = components.begin(); iterator != components.end(); ++iterator)
				{
					if ((*iterator)["id"] == id)
					{
						components.erase(iterator);
						found = true;
						break;
					}
				}
				if (found)
				{
					break;
				}
			}
			if (!found)
			{
				return Result<Json>::Failure("Missing deleted component " + id + " at " + location);
			}
			componentIds.erase(id);
		}
	}
	if (overrides.contains("addedComponents"))
	{
		for (const auto& addition : overrides["addedComponents"])
		{
			if (!addition.is_object() || !addition.contains("owner") || !addition["owner"].is_string() ||
			    !addition.contains("component"))
			{
				return Result<Json>::Failure("Invalid added component at " + location);
			}
			const auto owner = addition["owner"].get<String>();
			auto id = IdOf(addition["component"], location + ".addedComponent");
			if (!nodes.count(owner) || !id || !componentIds.insert(id.value).second)
			{
				return Result<Json>::Failure("Missing owner or duplicate added component at " + location);
			}
			nodes[owner]["components"].push_back(addition["component"]);
		}
	}
	if (overrides.contains("componentOwners"))
	{
		for (const auto& entry : overrides["componentOwners"].items())
		{
			if (!entry.value().is_string())
			{
				return Result<Json>::Failure("Invalid component owner at " + location);
			}
			const auto owner = entry.value().get<String>();
			if (!nodes.count(owner))
			{
				return Result<Json>::Failure("Cross-instance or missing component owner " + owner + " at " + location);
			}
			Json moved;
			bool found = false;
			for (auto& node : nodes)
			{
				auto& components = node.second["components"];
				for (auto iterator = components.begin(); iterator != components.end(); ++iterator)
				{
					if ((*iterator)["id"] == entry.key())
					{
						moved = *iterator;
						components.erase(iterator);
						found = true;
						break;
					}
				}
				if (found)
				{
					break;
				}
			}
			if (!found)
			{
				return Result<Json>::Failure("Missing component move target " + ToUrsaString(entry.key()) + " at " +
				                             location);
			}
			nodes[owner]["components"].push_back(std::move(moved));
		}
	}
	if (overrides.contains("componentOrders"))
	{
		for (const auto& entry : overrides["componentOrders"].items())
		{
			if (!nodes.count(ToUrsaString(entry.key())))
			{
				return Result<Json>::Failure("Missing component order owner " + ToUrsaString(entry.key()) + " at " +
				                             location);
			}
			auto& components = nodes[ToUrsaString(entry.key())]["components"];
			Vector<String> ids;
			Map<String, Json> objects;
			for (const auto& component : components)
			{
				const auto id = component["id"].get<String>();
				ids.push_back(id);
				objects.emplace(id, component);
			}
			auto ordered =
			    Reorder(ids, entry.value(), location + ".componentOrders[" + ToUrsaString(entry.key()) + "]");
			if (!ordered)
			{
				return Result<Json>::Failure(ordered.error);
			}
			components = Json::array();
			for (const auto& id : ids)
			{
				components.push_back(objects.at(id));
			}
		}
	}
	Map<String, Vector<String>> children;
	for (const auto& id : sourceOrder)
	{
		if (nodes.count(id) && id != root)
		{
			children[nodes[id]["parentId"].get<String>()].push_back(id);
		}
	}
	if (overrides.contains("gameObjectOrders"))
	{
		for (const auto& entry : overrides["gameObjectOrders"].items())
		{
			if (!nodes.count(ToUrsaString(entry.key())))
			{
				return Result<Json>::Failure("Missing node order parent " + ToUrsaString(entry.key()) + " at " +
				                             location);
			}
			auto ordered = Reorder(children[ToUrsaString(entry.key())], entry.value(),
			                       location + ".gameObjectOrders[" + ToUrsaString(entry.key()) + "]");
			if (!ordered)
			{
				return Result<Json>::Failure(ordered.error);
			}
		}
	}
	Json result = Json::array();
	std::function<void(const String&)> append = [&](const String& id)
	{
		result.push_back(nodes.at(id));
		for (const auto& child : children[id])
		{
			append(child);
		}
	};
	append(root);
	if (result.size() != nodes.size())
	{
		return Result<Json>::Failure("Disconnected Prefab nodes at " + location);
	}
	return Result<Json>::Success(std::move(result));
}
} // namespace Ursa
