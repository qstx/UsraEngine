#include <serialization/JsonInterop.hpp>
#include <serialization/PrefabMerge.hpp>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace ursa {
namespace {
using Json = nlohmann::json;

Result<String> idOf(const Json &object, const String &location) {
    if (!object.is_object() || !object.contains("id") || !object["id"].is_string())
        return Result<String>::failure("Missing object ID at " + location);
    const auto id = object["id"].get<String>();
    if (id.empty())
        return Result<String>::failure("Empty object ID at " + location);
    return Result<String>::success(id);
}

Result<void> requireArray(const Json &overrides, const char *name, const String &location) {
    if (overrides.contains(name) && !overrides[name].is_array())
        return Result<void>::failure("Invalid " + String(name) + " at " + location);
    return {};
}

Result<void> requireObject(const Json &overrides, const char *name, const String &location) {
    if (overrides.contains(name) && !overrides[name].is_object())
        return Result<void>::failure("Invalid " + String(name) + " at " + location);
    return {};
}

Result<void> reorder(Vector<String> &ids, const Json &fixed, const String &location) {
    if (!fixed.is_array())
        return Result<void>::failure("Invalid order at " + location);
    Set<String> seen;
    Vector<String> ordered;
    for (const auto &item : fixed) {
        if (!item.is_string())
            return Result<void>::failure("Invalid ordered ID at " + location);
        const auto id = item.get<String>();
        if (std::find(ids.begin(), ids.end(), id) == ids.end() || !seen.insert(id).second)
            return Result<void>::failure("Missing or repeated order target " + id + " at " + location);
        ordered.push_back(id);
    }
    for (const auto &id : ids)
        if (!seen.count(id))
            ordered.push_back(id);
    ids = std::move(ordered);
    return {};
}
} // namespace

Result<Json> mergePrefabNodes(const Json &source, const Json &overrides, const String &location) {
    if (!source.is_array() || source.empty() || !overrides.is_object())
        return Result<Json>::failure("Invalid Prefab source or overrides at " + location);
    static const Set<String> supported{"properties",      "deletedNodes",    "addedNodes",
                                       "nodeParents",     "nodeOrders",      "deletedComponents",
                                       "addedComponents", "componentOwners", "componentOrders"};
    for (const auto &entry : overrides.items())
        if (!supported.count(toUrsaString(entry.key())))
            return Result<Json>::failure("Unknown Prefab override operation " + toUrsaString(entry.key()) + " at " +
                                         location);
    for (const char *name : {"deletedNodes", "addedNodes", "deletedComponents", "addedComponents"}) {
        auto checked = requireArray(overrides, name, location);
        if (!checked)
            return Result<Json>::failure(checked.error);
    }
    for (const char *name : {"nodeParents", "nodeOrders", "componentOwners", "componentOrders"}) {
        auto checked = requireObject(overrides, name, location);
        if (!checked)
            return Result<Json>::failure(checked.error);
    }
    Map<String, Json> nodes;
    Vector<String> sourceOrder;
    Set<String> componentIds;
    for (const auto &node : source) {
        auto id = idOf(node, location + ".sourceNode");
        if (!id || !node.contains("parent") || !node.contains("components") || !node["components"].is_array())
            return Result<Json>::failure(!id ? id.error : "Invalid Prefab node at " + location);
        if (!nodes.emplace(id.value, node).second)
            return Result<Json>::failure("Duplicate Prefab node ID " + id.value + " at " + location);
        sourceOrder.push_back(id.value);
        for (const auto &component : node["components"]) {
            auto componentId = idOf(component, location + ".component");
            if (!componentId || !componentIds.insert(componentId.value).second)
                return Result<Json>::failure("Duplicate or invalid Prefab component ID at " + location);
        }
    }
    const auto root = sourceOrder.front();
    if (!nodes[root]["parent"].is_null())
        return Result<Json>::failure("Prefab root has a parent at " + location);
    if (overrides.contains("deletedNodes")) {
        Set<String> deleted;
        for (const auto &item : overrides["deletedNodes"]) {
            if (!item.is_string())
                return Result<Json>::failure("Invalid deleted node ID at " + location);
            const auto id = item.get<String>();
            if (id == root || !nodes.count(id) || !deleted.insert(id).second)
                return Result<Json>::failure("Missing or invalid deleted node " + id + " at " + location);
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto &entry : nodes)
                if (!entry.second["parent"].is_null() && deleted.count(entry.second["parent"].get<String>()) &&
                    !deleted.count(entry.first)) {
                    deleted.insert(entry.first);
                    changed = true;
                }
        }
        for (const auto &id : deleted)
            nodes.erase(id);
    }
    if (overrides.contains("addedNodes"))
        for (const auto &node : overrides["addedNodes"]) {
            auto id = idOf(node, location + ".addedNode");
            if (!id || !node.contains("parent") || !node["parent"].is_string() || !node.contains("components") ||
                !node["components"].is_array())
                return Result<Json>::failure(!id ? id.error : "Invalid added node at " + location);
            if (!nodes.emplace(id.value, node).second)
                return Result<Json>::failure("Duplicate added node ID " + id.value + " at " + location);
            sourceOrder.push_back(id.value);
            for (const auto &component : node["components"]) {
                auto componentId = idOf(component, location + ".addedNode.component");
                if (!componentId || !componentIds.insert(componentId.value).second)
                    return Result<Json>::failure("Duplicate added component ID at " + location);
            }
        }
    if (overrides.contains("nodeParents"))
        for (const auto &entry : overrides["nodeParents"].items()) {
            if (toUrsaString(entry.key()) == root || !nodes.count(toUrsaString(entry.key())) ||
                !entry.value().is_string())
                return Result<Json>::failure("Missing or invalid reparent target " + toUrsaString(entry.key()) +
                                             " at " + location);
            const auto parent = entry.value().get<String>();
            if (!nodes.count(parent))
                return Result<Json>::failure("Cross-instance or missing parent " + parent + " at " + location);
            nodes[toUrsaString(entry.key())]["parent"] = parent;
        }
    for (const auto &entry : nodes) {
        if (entry.first == root)
            continue;
        if (!entry.second["parent"].is_string() || !nodes.count(entry.second["parent"].get<String>()))
            return Result<Json>::failure("Missing Prefab parent for node " + entry.first + " at " + location);
        Set<String> visited;
        String current = entry.first;
        while (current != root) {
            if (!visited.insert(current).second)
                return Result<Json>::failure("Prefab parent cycle at node " + current + " in " + location);
            current = nodes[current]["parent"].get<String>();
        }
    }
    if (overrides.contains("deletedComponents"))
        for (const auto &item : overrides["deletedComponents"]) {
            if (!item.is_string())
                return Result<Json>::failure("Invalid deleted component ID at " + location);
            const auto id = item.get<String>();
            bool found = false;
            for (auto &entry : nodes) {
                auto &components = entry.second["components"];
                for (auto iterator = components.begin(); iterator != components.end(); ++iterator)
                    if ((*iterator)["id"] == id) {
                        components.erase(iterator);
                        found = true;
                        break;
                    }
                if (found)
                    break;
            }
            if (!found)
                return Result<Json>::failure("Missing deleted component " + id + " at " + location);
            componentIds.erase(id);
        }
    if (overrides.contains("addedComponents"))
        for (const auto &addition : overrides["addedComponents"]) {
            if (!addition.is_object() || !addition.contains("owner") || !addition["owner"].is_string() ||
                !addition.contains("component"))
                return Result<Json>::failure("Invalid added component at " + location);
            const auto owner = addition["owner"].get<String>();
            auto id = idOf(addition["component"], location + ".addedComponent");
            if (!nodes.count(owner) || !id || !componentIds.insert(id.value).second)
                return Result<Json>::failure("Missing owner or duplicate added component at " + location);
            nodes[owner]["components"].push_back(addition["component"]);
        }
    if (overrides.contains("componentOwners"))
        for (const auto &entry : overrides["componentOwners"].items()) {
            if (!entry.value().is_string())
                return Result<Json>::failure("Invalid component owner at " + location);
            const auto owner = entry.value().get<String>();
            if (!nodes.count(owner))
                return Result<Json>::failure("Cross-instance or missing component owner " + owner + " at " + location);
            Json moved;
            bool found = false;
            for (auto &node : nodes) {
                auto &components = node.second["components"];
                for (auto iterator = components.begin(); iterator != components.end(); ++iterator)
                    if ((*iterator)["id"] == entry.key()) {
                        moved = *iterator;
                        components.erase(iterator);
                        found = true;
                        break;
                    }
                if (found)
                    break;
            }
            if (!found)
                return Result<Json>::failure("Missing component move target " + toUrsaString(entry.key()) + " at " +
                                             location);
            nodes[owner]["components"].push_back(std::move(moved));
        }
    if (overrides.contains("componentOrders"))
        for (const auto &entry : overrides["componentOrders"].items()) {
            if (!nodes.count(toUrsaString(entry.key())))
                return Result<Json>::failure("Missing component order owner " + toUrsaString(entry.key()) + " at " +
                                             location);
            auto &components = nodes[toUrsaString(entry.key())]["components"];
            Vector<String> ids;
            Map<String, Json> objects;
            for (const auto &component : components) {
                const auto id = component["id"].get<String>();
                ids.push_back(id);
                objects.emplace(id, component);
            }
            auto ordered =
                reorder(ids, entry.value(), location + ".componentOrders[" + toUrsaString(entry.key()) + "]");
            if (!ordered)
                return Result<Json>::failure(ordered.error);
            components = Json::array();
            for (const auto &id : ids)
                components.push_back(objects.at(id));
        }
    Map<String, Vector<String>> children;
    for (const auto &id : sourceOrder)
        if (nodes.count(id) && id != root)
            children[nodes[id]["parent"].get<String>()].push_back(id);
    if (overrides.contains("nodeOrders"))
        for (const auto &entry : overrides["nodeOrders"].items()) {
            if (!nodes.count(toUrsaString(entry.key())))
                return Result<Json>::failure("Missing node order parent " + toUrsaString(entry.key()) + " at " +
                                             location);
            auto ordered = reorder(children[toUrsaString(entry.key())], entry.value(),
                                   location + ".nodeOrders[" + toUrsaString(entry.key()) + "]");
            if (!ordered)
                return Result<Json>::failure(ordered.error);
        }
    Json result = Json::array();
    std::function<void(const String &)> append = [&](const String &id) {
        result.push_back(nodes.at(id));
        for (const auto &child : children[id])
            append(child);
    };
    append(root);
    if (result.size() != nodes.size())
        return Result<Json>::failure("Disconnected Prefab nodes at " + location);
    return Result<Json>::success(std::move(result));
}
} // namespace ursa
