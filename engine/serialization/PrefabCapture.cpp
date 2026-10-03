#include <serialization/JsonInterop.hpp>
#include <serialization/PrefabCapture.hpp>
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace ursa {
namespace {
using Json = nlohmann::json;

struct Indexed {
    Map<String, Json> nodes;
    Map<String, Json> components;
    Map<String, String> owners;
    Map<String, Vector<String>> children;
    Map<String, Vector<String>> componentOrder;
};

Result<Indexed> index(const Json &document, const String &location) {
    if (!document.is_object() || !document.contains("nodes") || !document["nodes"].is_array())
        return Result<Indexed>::failure("Invalid scene nodes at " + location);
    Indexed indexed;
    for (const auto &node : document["nodes"]) {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string() || !node.contains("parent") ||
            !node.contains("components") || !node["components"].is_array())
            return Result<Indexed>::failure("Invalid scene node at " + location);
        const auto id = node["id"].get<String>();
        if (!indexed.nodes.emplace(id, node).second)
            return Result<Indexed>::failure("Duplicate scene node " + id + " at " + location);
        const auto parent = node["parent"].is_null() ? String{} : node["parent"].get<String>();
        indexed.children[parent].push_back(id);
        for (const auto &component : node["components"]) {
            if (!component.is_object() || !component.contains("id") || !component["id"].is_string())
                return Result<Indexed>::failure("Invalid scene component at " + location);
            const auto componentId = component["id"].get<String>();
            if (!indexed.components.emplace(componentId, component).second)
                return Result<Indexed>::failure("Duplicate scene component " + componentId + " at " + location);
            indexed.owners.emplace(componentId, id);
            indexed.componentOrder[id].push_back(componentId);
        }
    }
    return Result<Indexed>::success(std::move(indexed));
}

void appendUnique(Json &array, const String &id) {
    if (!array.is_array())
        array = Json::array();
    if (std::find(array.begin(), array.end(), Json(id)) == array.end())
        array.push_back(id);
}

void remapReferences(Json &value, const Map<String, String> &reverse) {
    if (value.is_string()) {
        auto found = reverse.find(value.get<String>());
        if (found != reverse.end())
            value = found->second;
    } else if (value.is_array()) {
        for (auto &entry : value)
            remapReferences(entry, reverse);
    } else if (value.is_object()) {
        for (auto &entry : value.items())
            remapReferences(entry.value(), reverse);
    }
}

String escapePointer(const String &part) {
    String escaped;
    for (char character : part)
        if (character == '~')
            escaped += "~0";
        else if (character == '/')
            escaped += "~1";
        else
            escaped += character;
    return escaped;
}

void captureFields(const Json &before, const Json &after, const String &pointer, Json &properties,
                   const Map<String, String> &reverse) {
    if (before == after)
        return;
    if (before.is_object() && after.is_object()) {
        for (const auto &entry : after.items()) {
            const auto child = pointer + "/" + escapePointer(toUrsaString(entry.key()));
            if (before.contains(entry.key()))
                captureFields(before[entry.key()], entry.value(), child, properties, reverse);
            else {
                Json value = entry.value();
                remapReferences(value, reverse);
                properties[toStdString(child)] = std::move(value);
            }
        }
        return;
    }
    if (before.is_array() && after.is_array() && before.size() == after.size()) {
        for (size_t position = 0; position < after.size(); ++position)
            captureFields(before[position], after[position], pointer + "/" + toUrsaString(std::to_string(position)),
                          properties, reverse);
        return;
    }
    Json value = after;
    remapReferences(value, reverse);
    properties[toStdString(pointer)] = std::move(value);
}

Vector<String> localOrder(const Vector<String> &runtimeIds, const Map<String, String> &reverse,
                          const Set<String> &scope) {
    Vector<String> result;
    for (const auto &id : runtimeIds)
        if (scope.count(id)) {
            auto found = reverse.find(id);
            if (found != reverse.end())
                result.push_back(found->second);
        }
    return result;
}

Result<void> captureInstance(const String &instanceId, Map<String, String> &known, const Indexed &baseline,
                             const Indexed &current, Json &patch, Set<String> &allCurrentInstanceNodes,
                             Set<String> &allBaselineInstanceNodes, Set<String> &allCurrentInstanceComponents,
                             const String &location) {
    Map<String, String> reverse;
    Set<String> baselineNodes;
    Set<String> baselineComponents;
    for (const auto &entry : known) {
        reverse.emplace(entry.second, entry.first);
        if (baseline.nodes.count(entry.second))
            baselineNodes.insert(entry.second);
        if (baseline.components.count(entry.second))
            baselineComponents.insert(entry.second);
    }
    String root;
    for (const auto &id : baselineNodes) {
        const auto &parent = baseline.nodes.at(id)["parent"];
        if (parent.is_null() || !baselineNodes.count(parent.get<String>())) {
            if (!root.empty())
                return Result<void>::failure("Multiple Prefab roots for instance " + instanceId + " at " + location);
            root = id;
        }
    }
    if (root.empty() || !current.nodes.count(root))
        return Result<void>::failure("Missing Prefab root for instance " + instanceId + " at " + location);
    Set<String> currentNodes;
    std::function<void(const String &)> visit = [&](const String &id) {
        if (!currentNodes.insert(id).second)
            return;
        for (const auto &child : current.children.count(id) ? current.children.at(id) : Vector<String>{})
            visit(child);
    };
    visit(root);
    for (const auto &id : baselineNodes)
        if (current.nodes.count(id) && !currentNodes.count(id))
            return Result<void>::failure("Cross-instance node move " + id + " in instance " + instanceId);
    for (const auto &id : currentNodes)
        if (!reverse.count(id))
            reverse.emplace(id, id);
    for (const auto &id : currentNodes) {
        allCurrentInstanceNodes.insert(id);
        const auto &node = current.nodes.at(id);
        for (const auto &component : node["components"]) {
            const auto componentId = component["id"].get<String>();
            allCurrentInstanceComponents.insert(componentId);
            if (!reverse.count(componentId))
                reverse.emplace(componentId, componentId);
        }
    }
    allBaselineInstanceNodes.insert(baselineNodes.begin(), baselineNodes.end());
    for (const auto &id : baselineNodes)
        if (!currentNodes.count(id)) {
            const auto local = reverse.at(id);
            bool wasAdded = false;
            if (patch.contains("addedNodes") && patch["addedNodes"].is_array())
                for (auto iterator = patch["addedNodes"].begin(); iterator != patch["addedNodes"].end(); ++iterator)
                    if ((*iterator)["id"] == local) {
                        patch["addedNodes"].erase(iterator);
                        wasAdded = true;
                        break;
                    }
            if (!wasAdded)
                appendUnique(patch["deletedNodes"], local);
            if (patch.contains("properties") && patch["properties"].is_object())
                patch["properties"].erase(toStdString(local));
            if (patch.contains("nodeParents") && patch["nodeParents"].is_object())
                patch["nodeParents"].erase(toStdString(local));
            if (patch.contains("nodeOrders") && patch["nodeOrders"].is_object())
                patch["nodeOrders"].erase(toStdString(local));
            if (patch.contains("componentOrders") && patch["componentOrders"].is_object())
                patch["componentOrders"].erase(toStdString(local));
            if (wasAdded && patch.contains("addedComponents") && patch["addedComponents"].is_array()) {
                auto &additions = patch["addedComponents"];
                for (auto iterator = additions.begin(); iterator != additions.end();) {
                    if ((*iterator)["owner"] == local) {
                        const auto componentLocal = (*iterator)["component"]["id"].get<String>();
                        known.erase(componentLocal);
                        iterator = additions.erase(iterator);
                    } else {
                        ++iterator;
                    }
                }
            }
            known.erase(local);
        }
    for (const auto &id : baselineComponents)
        if (!allCurrentInstanceComponents.count(id)) {
            const auto local = reverse.at(id);
            if (!currentNodes.count(baseline.owners.at(id))) {
                if (patch.contains("properties") && patch["properties"].is_object())
                    patch["properties"].erase(toStdString(local));
                if (patch.contains("componentOwners") && patch["componentOwners"].is_object())
                    patch["componentOwners"].erase(toStdString(local));
                known.erase(local);
                continue;
            }
            bool wasAdded = false;
            if (patch.contains("addedComponents") && patch["addedComponents"].is_array())
                for (auto iterator = patch["addedComponents"].begin(); iterator != patch["addedComponents"].end();
                     ++iterator)
                    if ((*iterator)["component"]["id"] == local) {
                        patch["addedComponents"].erase(iterator);
                        wasAdded = true;
                        break;
                    }
            if (!wasAdded)
                appendUnique(patch["deletedComponents"], local);
            if (patch.contains("properties") && patch["properties"].is_object())
                patch["properties"].erase(toStdString(local));
            if (patch.contains("componentOwners") && patch["componentOwners"].is_object())
                patch["componentOwners"].erase(toStdString(local));
            known.erase(local);
        }
    for (const auto &id : currentNodes) {
        const auto &node = current.nodes.at(id);
        const auto localId = reverse.at(id);
        if (!baselineNodes.count(id)) {
            Json added = node;
            added["id"] = localId;
            if (node["parent"].is_null() || !reverse.count(node["parent"].get<String>()))
                return Result<void>::failure("Cross-instance parent for added node " + localId + " at " + location);
            added["parent"] = reverse.at(node["parent"].get<String>());
            added["components"] = Json::array();
            patch["addedNodes"].push_back(std::move(added));
            known.emplace(localId, id);
        } else {
            const auto &old = baseline.nodes.at(id);
            Json changes = Json::object();
            captureFields(old["transform"], node["transform"], "/transform", changes, reverse);
            captureFields(old.value("label", Json("")), node.value("label", Json("")), "/label", changes, reverse);
            captureFields(old["enabled"], node["enabled"], "/enabled", changes, reverse);
            if (!changes.empty()) {
                auto &properties = patch["properties"][toStdString(localId)];
                if (!properties.is_object())
                    properties = Json::object();
                properties.update(changes);
            }
            if (old["parent"] != node["parent"] && id != root) {
                if (node["parent"].is_null() || !reverse.count(node["parent"].get<String>()))
                    return Result<void>::failure("Cross-instance parent for node " + localId + " at " + location);
                patch["nodeParents"][toStdString(localId)] = reverse.at(node["parent"].get<String>());
            }
        }
        for (const auto &component : node["components"]) {
            const auto runtimeId = component["id"].get<String>();
            const auto localComponentId = reverse.at(runtimeId);
            if (!baselineComponents.count(runtimeId)) {
                Json addedComponent = component;
                addedComponent["id"] = localComponentId;
                remapReferences(addedComponent["data"], reverse);
                patch["addedComponents"].push_back({{"owner", localId}, {"component", std::move(addedComponent)}});
                known.emplace(localComponentId, runtimeId);
            } else {
                const auto &old = baseline.components.at(runtimeId);
                Json changes = Json::object();
                captureFields(old["data"], component["data"], "/data", changes, reverse);
                captureFields(old["enabled"], component["enabled"], "/enabled", changes, reverse);
                if (!changes.empty()) {
                    auto &properties = patch["properties"][toStdString(localComponentId)];
                    if (!properties.is_object())
                        properties = Json::object();
                    properties.update(changes);
                }
                if (baseline.owners.at(runtimeId) != id)
                    patch["componentOwners"][toStdString(localComponentId)] = localId;
            }
        }
    }
    for (const auto &id : currentNodes) {
        const auto localId = reverse.at(id);
        const auto currentChildren =
            localOrder(current.children.count(id) ? current.children.at(id) : Vector<String>{}, reverse, currentNodes);
        const auto oldChildren = localOrder(baseline.children.count(id) ? baseline.children.at(id) : Vector<String>{},
                                            reverse, baselineNodes);
        if (currentChildren != oldChildren && !currentChildren.empty())
            patch["nodeOrders"][toStdString(localId)] = currentChildren;
        const auto currentComponents =
            localOrder(current.componentOrder.count(id) ? current.componentOrder.at(id) : Vector<String>{}, reverse,
                       allCurrentInstanceComponents);
        const auto oldComponents =
            localOrder(baseline.componentOrder.count(id) ? baseline.componentOrder.at(id) : Vector<String>{}, reverse,
                       baselineComponents);
        if (currentComponents != oldComponents && !currentComponents.empty())
            patch["componentOrders"][toStdString(localId)] = currentComponents;
    }
    return {};
}
} // namespace

Result<void> captureLinkedSceneChanges(const std::filesystem::path &path, LinkedSceneResource &linked,
                                       const ComponentRegistry &registry, const MaterialIdResolver &materialId) {
    auto encoded = encodeSceneDocument(path, linked.id, *linked.scene, registry, materialId);
    if (!encoded)
        return Result<void>::failure(encoded.error);
    auto baseline = index(linked.snapshot, pathToUrsaString(path) + " previous snapshot");
    auto current = index(encoded.value, pathToUrsaString(path) + " current scene");
    if (!baseline || !current)
        return Result<void>::failure(!baseline ? baseline.error : current.error);
    Json candidate = linked.document;
    Set<String> currentInstanceNodes;
    Set<String> baselineInstanceNodes;
    Set<String> currentInstanceComponents;
    auto updatedScopes = linked.scopedIds;
    for (auto &instance : candidate["instances"]) {
        const auto id = instance["id"].get<String>();
        auto mapping = updatedScopes.find(id);
        if (mapping == updatedScopes.end())
            return Result<void>::failure("Missing Prefab scope for instance " + id + " at " + pathToUrsaString(path));
        auto result = captureInstance(id, mapping->second, baseline.value, current.value, instance["overrides"],
                                      currentInstanceNodes, baselineInstanceNodes, currentInstanceComponents,
                                      pathToUrsaString(path));
        if (!result)
            return result;
    }
    for (const auto &id : currentInstanceNodes)
        if (baseline.value.nodes.count(id) && !baselineInstanceNodes.count(id))
            return Result<void>::failure("Regular node moved into Prefab instance: " + id);
    candidate["nodes"] = Json::array();
    for (const auto &node : encoded.value["nodes"])
        if (!currentInstanceNodes.count(node["id"].get<String>()))
            candidate["nodes"].push_back(node);
    candidate["ambient"] = encoded.value["ambient"];
    candidate["activeCamera"] = encoded.value["activeCamera"];
    linked.document = std::move(candidate);
    linked.snapshot = std::move(encoded.value);
    linked.scopedIds = std::move(updatedScopes);
    return {};
}
} // namespace ursa
