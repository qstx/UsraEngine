#include <serialization/JsonInterop.hpp>
#include <serialization/PrefabResource.hpp>
#include <serialization/JsonResource.hpp>
#include <serialization/PrefabMerge.hpp>
#include <serialization/PrefabCapture.hpp>
#include <map>
#include <set>

namespace ursa {
namespace {
using Json = nlohmann::json;

Result<Json *> findInstance(Json &document, ResourceId id) {
    if (!id || !document.is_object() || !document.contains("instances") || !document["instances"].is_array())
        return Result<Json *>::failure("Invalid scene or Prefab instance ID " + id.toString());
    for (auto &instance : document["instances"])
        if (instance.is_object() && instance.value("id", String{}) == id.toString())
            return Result<Json *>::success(&instance);
    return Result<Json *>::failure("Missing Prefab instance " + id.toString());
}

bool allowedPropertyPath(const String &pointer) {
    return pointer == "/enabled" || pointer == "/label" || pointer.rfind("/transform/", 0) == 0 ||
           pointer.rfind("/data/", 0) == 0;
}

Result<void> applyProperties(Json &object, const Json &properties, const String &location) {
    if (!properties.is_object())
        return Result<void>::failure("Invalid Prefab property overrides at " + location);
    for (const auto &entry : properties.items()) {
        if (!allowedPropertyPath(toUrsaString(entry.key())))
            return Result<void>::failure("Invalid Prefab property path " + toUrsaString(entry.key()) + " at " +
                                         location);
        try {
            const Json::json_pointer pointer(entry.key());
            if (!object.contains(pointer))
                return Result<void>::failure("Missing Prefab property target " + toUrsaString(entry.key()) + " at " +
                                             location);
            object[pointer] = entry.value();
        } catch (const Json::exception &error) {
            return Result<void>::failure("Invalid Prefab property path " + toUrsaString(entry.key()) + " at " +
                                         location + ": " + error.what());
        }
    }
    return {};
}

Result<ResourceId> parseId(const Json &value, const String &location) {
    if (!value.is_string())
        return Result<ResourceId>::failure("Invalid ID at " + location);
    auto id = ResourceId::parse(value.get<String>());
    return id ? id : Result<ResourceId>::failure(id.error + " at " + location);
}

ResourceId scopedId(ResourceId instance, ResourceId local) {
    ResourceId result;
    for (size_t index = 0; index < result.bytes.size(); ++index)
        result.bytes[index] = instance.bytes[index] ^ local.bytes[index];
    if (!result)
        result.bytes[0] = 1;
    return result;
}

Json sceneEnvelope(ResourceId id, Json nodes) {
    return {
        {"format", "UrsaScene"},   {"version", resourceFormatVersion},
        {"id", id.toString()},     {"ambient", {{"color", Json::array({1.0f, 1.0f, 1.0f, 1.0f})}, {"intensity", 0.1f}}},
        {"activeCamera", nullptr}, {"nodes", std::move(nodes)}};
}

Result<Json> expandInstances(const std::filesystem::path &scenePath, const Json &source, ResourceSession &resources,
                             Map<String, Map<String, String>> &scopedIds) {
    Json expanded = source;
    if (!expanded.contains("nodes") || !expanded["nodes"].is_array())
        return Result<Json>::failure("Invalid scene nodes at " + pathToUrsaString(scenePath));
    auto instances = source.find("instances");
    if (instances == source.end())
        return Result<Json>::success(std::move(expanded));
    if (!instances->is_array() || instances->size() > 100000)
        return Result<Json>::failure("Invalid prefab instances at " + pathToUrsaString(scenePath));
    Set<String> instanceIds;
    Set<String> objectIds;
    Set<String> regularNodes;
    for (const auto &node : expanded["nodes"]) {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string())
            return Result<Json>::failure("Invalid regular scene node at " + pathToUrsaString(scenePath));
        objectIds.insert(node["id"].get<String>());
        regularNodes.insert(node["id"].get<String>());
        if (node.contains("components") && node["components"].is_array())
            for (const auto &component : node["components"])
                if (component.is_object() && component.contains("id") && component["id"].is_string())
                    objectIds.insert(component["id"].get<String>());
    }
    for (size_t index = 0; index < instances->size(); ++index) {
        const auto &instance = (*instances)[index];
        const String location = pathToUrsaString(scenePath) + ".instances[" + toUrsaString(std::to_string(index)) + "]";
        if (!instance.is_object() || !instance.contains("id") || !instance.contains("prefab") ||
            !instance.contains("parent") || !instance.contains("overrides"))
            return Result<Json>::failure("Invalid prefab instance at " + location);
        auto instanceId = parseId(instance["id"], location + ".id");
        auto prefabId = parseId(instance["prefab"], location + ".prefab");
        if (!instanceId || !prefabId)
            return Result<Json>::failure(!instanceId ? instanceId.error : prefabId.error);
        if (!instanceIds.insert(instanceId.value.toString()).second)
            return Result<Json>::failure("Duplicate prefab instance ID " + instanceId.value.toString() + " at " +
                                         location);
        if (!instance["parent"].is_null()) {
            auto externalParent = parseId(instance["parent"], location + ".parent");
            if (!externalParent)
                return Result<Json>::failure(externalParent.error);
            if (!regularNodes.count(externalParent.value.toString()))
                return Result<Json>::failure("Cross-instance or missing external parent " +
                                             externalParent.value.toString() + " at " + location);
        }
        if (!instance["overrides"].is_object())
            return Result<Json>::failure("Invalid prefab overrides at " + location);
        auto resolved = resources.index().resolve(prefabId.value, location);
        if (!resolved)
            return Result<Json>::failure(resolved.error);
        if (resolved.value.kind != ResourceKind::Prefab)
            return Result<Json>::failure("Referenced resource is not a Prefab at " + location);
        const String prefabSource = resolved.value.path.toString();
        auto prefabPath = resources.index().locate(resolved.value);
        if (!prefabPath)
            return Result<Json>::failure(prefabPath.error);
        auto prefab = readJsonResource(prefabPath.value, ResourceKind::Prefab);
        if (!prefab)
            return Result<Json>::failure(prefab.error);
        if (!prefab.value.contains("nodes") || !prefab.value["nodes"].is_array() || prefab.value["nodes"].empty() ||
            prefab.value.contains("instances"))
            return Result<Json>::failure("Invalid or nested Prefab at " + prefabSource);
        const auto &overrides = instance["overrides"];
        auto merged = mergePrefabNodes(prefab.value["nodes"], overrides,
                                       "Prefab " + prefabId.value.toString() + " instance " +
                                           instanceId.value.toString() + " at " + location);
        if (!merged)
            return Result<Json>::failure(merged.error);
        Map<String, String> scoped;
        for (const auto &node : merged.value) {
            if (!node.is_object() || !node.contains("id") || !node["id"].is_string() || !node.contains("components") ||
                !node["components"].is_array())
                return Result<Json>::failure("Invalid Prefab node at " + prefabSource);
            auto local = parseId(node["id"], prefabSource + ".nodes.id");
            if (!local)
                return Result<Json>::failure(local.error);
            const auto mapped = scopedId(instanceId.value, local.value).toString();
            if (!scoped.emplace(local.value.toString(), mapped).second || !objectIds.insert(mapped).second)
                return Result<Json>::failure("Duplicate Prefab node ID " + local.value.toString() + " at " + location);
            for (const auto &component : node["components"]) {
                if (!component.is_object() || !component.contains("id"))
                    return Result<Json>::failure("Invalid Prefab component at " + location);
                auto componentId = parseId(component["id"], location + ".component.id");
                if (!componentId)
                    return Result<Json>::failure(componentId.error);
                const auto mappedComponent = scopedId(instanceId.value, componentId.value).toString();
                if (!scoped.emplace(componentId.value.toString(), mappedComponent).second ||
                    !objectIds.insert(mappedComponent).second)
                    return Result<Json>::failure("Duplicate Prefab component ID " + componentId.value.toString() +
                                                 " at " + location);
            }
        }
        if (merged.value.front().contains("parent") && !merged.value.front()["parent"].is_null())
            return Result<Json>::failure("Prefab root has a parent at " + prefabSource);
        auto properties = overrides.find("properties");
        if (properties != overrides.end() && !properties->is_object())
            return Result<Json>::failure("Invalid Prefab properties at " + location);
        Set<String> usedProperties;
        for (const auto &original : merged.value) {
            Json node = original;
            const auto localId = original["id"].get<String>();
            if (properties != overrides.end() && properties->contains(toStdString(localId))) {
                auto applied =
                    applyProperties(node, (*properties)[toStdString(localId)], location + ".node[" + localId + "]");
                if (!applied)
                    return Result<Json>::failure(applied.error);
                usedProperties.insert(localId);
            }
            node["id"] = scoped.at(localId);
            if (original["parent"].is_null()) {
                node["parent"] = instance["parent"];
            } else {
                auto parent = scoped.find(original["parent"].get<String>());
                if (parent == scoped.end())
                    return Result<Json>::failure("Missing Prefab parent at " + location + ".node[" + localId + "]");
                node["parent"] = parent->second;
            }
            for (auto &component : node["components"]) {
                const auto componentLocalId = component["id"].get<String>();
                if (properties != overrides.end() && properties->contains(toStdString(componentLocalId))) {
                    auto applied = applyProperties(component, (*properties)[toStdString(componentLocalId)],
                                                   location + ".component[" + componentLocalId + "]");
                    if (!applied)
                        return Result<Json>::failure(applied.error);
                    usedProperties.insert(componentLocalId);
                }
                component["id"] = scoped.at(componentLocalId);
                std::function<void(Json &)> remap = [&](Json &value) {
                    if (value.is_string()) {
                        auto found = scoped.find(value.get<String>());
                        if (found != scoped.end())
                            value = found->second;
                    } else if (value.is_array()) {
                        for (auto &item : value)
                            remap(item);
                    } else if (value.is_object()) {
                        for (auto &item : value.items())
                            remap(item.value());
                    }
                };
                remap(component["data"]);
            }
            expanded["nodes"].push_back(std::move(node));
        }
        if (properties != overrides.end())
            for (const auto &entry : properties->items())
                if (!usedProperties.count(toUrsaString(entry.key())))
                    return Result<Json>::failure("Prefab " + prefabId.value.toString() + " instance " +
                                                 instanceId.value.toString() + " missing property target " +
                                                 toUrsaString(entry.key()) + " at " + location);
        scopedIds.emplace(instanceId.value.toString(), std::move(scoped));
    }
    return Result<Json>::success(std::move(expanded));
}
} // namespace

Result<void> savePrefabResource(const std::filesystem::path &path, ResourceId id, const Scene &scene, NodeId root,
                                const ComponentRegistry &registry, const MaterialIdResolver &materialId) {
    if (!id || path.extension() != ".ursaprefab" || !scene.contains(root))
        return Result<void>::failure("Invalid Prefab path, ID or root at " + pathToUrsaString(path));
    auto encoded =
        encodeSceneDocument(path.parent_path() / "prefab-source.ursascene", id, scene, registry, materialId, root);
    if (!encoded)
        return Result<void>::failure(encoded.error);
    Json nodes = std::move(encoded.value["nodes"]);
    nodes[0]["parent"] = nullptr;
    Json document{{"format", "UrsaPrefab"},
                  {"version", resourceFormatVersion},
                  {"id", id.toString()},
                  {"nodes", std::move(nodes)}};
    return writeJsonResource(path, document);
}

Result<LoadedSceneResource> loadPrefabResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                               ComponentLoadContext &context) {
    auto prefab = readJsonResource(path, ResourceKind::Prefab);
    if (!prefab)
        return Result<LoadedSceneResource>::failure(prefab.error);
    if (!prefab.value.contains("nodes") || !prefab.value["nodes"].is_array() || prefab.value["nodes"].empty())
        return Result<LoadedSceneResource>::failure("Invalid Prefab nodes at " + pathToUrsaString(path));
    size_t roots = 0;
    for (const auto &node : prefab.value["nodes"])
        if (node.is_object() && node.contains("parent") && node["parent"].is_null())
            ++roots;
    if (roots != 1 || !prefab.value["nodes"].front().is_object() || !prefab.value["nodes"].front().contains("parent") ||
        !prefab.value["nodes"].front()["parent"].is_null())
        return Result<LoadedSceneResource>::failure("Prefab must contain one rooted subtree at " +
                                                    pathToUrsaString(path));
    auto id = parseId(prefab.value["id"], pathToUrsaString(path) + ".id");
    if (!id)
        return Result<LoadedSceneResource>::failure(id.error);
    return decodeSceneDocument(path, sceneEnvelope(id.value, prefab.value["nodes"]), registry, context);
}

Result<void> addPrefabInstance(Json &sceneDocument, ResourceId prefabId, ResourceId instanceId, ResourceId parentId) {
    if (!prefabId || !instanceId || !sceneDocument.is_object())
        return Result<void>::failure("Invalid Prefab instance or scene document");
    if (!sceneDocument.contains("instances"))
        sceneDocument["instances"] = Json::array();
    if (!sceneDocument["instances"].is_array())
        return Result<void>::failure("Invalid scene instances array");
    for (const auto &existing : sceneDocument["instances"])
        if (existing.is_object() && existing.value("id", String{}) == instanceId.toString())
            return Result<void>::failure("Duplicate Prefab instance ID " + instanceId.toString());
    sceneDocument["instances"].push_back({{"id", instanceId.toString()},
                                          {"prefab", prefabId.toString()},
                                          {"parent", parentId ? Json(parentId.toString()) : Json(nullptr)},
                                          {"overrides", Json::object()}});
    return {};
}

Result<LinkedSceneResource> loadLinkedSceneResource(const std::filesystem::path &path,
                                                    const ComponentRegistry &registry, ComponentLoadContext &context) {
    auto document = readJsonResource(path, ResourceKind::Scene);
    if (!document)
        return Result<LinkedSceneResource>::failure(document.error);
    return decodeLinkedSceneDocument(path, std::move(document.value), registry, context);
}

Result<LinkedSceneResource> decodeLinkedSceneDocument(const std::filesystem::path &path, Json document,
                                                       const ComponentRegistry &registry, ComponentLoadContext &context) {
    try {
        Map<String, Map<String, String>> scopedIds;
        auto expanded = expandInstances(path, document, context.resources(), scopedIds);
        if (!expanded)
            return Result<LinkedSceneResource>::failure(expanded.error);
        auto loaded = decodeSceneDocument(path, expanded.value, registry, context);
        if (!loaded)
            return Result<LinkedSceneResource>::failure(loaded.error);
        return Result<LinkedSceneResource>::success({loaded.value.id, std::move(loaded.value.scene),
                                                     std::move(document), std::move(expanded.value),
                                                     std::move(scopedIds), &context.resources()});
    } catch (const Json::exception &error) {
        return Result<LinkedSceneResource>::failure("Invalid linked scene at " + pathToUrsaString(path) + ": " +
                                                    error.what());
    }
}

Result<void> saveLinkedSceneResource(const std::filesystem::path &path, LinkedSceneResource &linked,
                                     const ComponentRegistry &registry, const MaterialIdResolver &materialId) {
    if (path.extension() != ".ursascene" || !linked.id || !linked.document.is_object() ||
        linked.document.value("id", String{}) != linked.id.toString())
        return Result<void>::failure("Invalid linked scene document at " + pathToUrsaString(path));
    const MaterialIdResolver resolver =
        materialId ? materialId : [&linked](const std::shared_ptr<MaterialInstance> &material) {
            return linked.resources ? linked.resources->identifyMaterial(material)
                                    : Result<ResourceId>::failure("No resource session for material lookup");
        };
    try {
        auto captured = captureLinkedSceneChanges(path, linked, registry, resolver);
        return captured ? writeJsonResource(path, linked.document) : captured;
    } catch (const Json::exception &error) {
        return Result<void>::failure("Cannot save linked scene at " + pathToUrsaString(path) + ": " + error.what());
    }
}

Result<void> reloadLinkedSceneResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                       ComponentLoadContext &context, LinkedSceneResource &active) {
    auto candidate = loadLinkedSceneResource(path, registry, context);
    if (!candidate)
        return Result<void>::failure(candidate.error);
    active = std::move(candidate.value);
    return {};
}

Result<void> setPrefabPropertyOverride(LinkedSceneResource &linked, ResourceId instanceId, ResourceId objectId,
                                       const String &pointer, const Json &value) {
    if (!objectId || !allowedPropertyPath(pointer))
        return Result<void>::failure("Invalid Prefab property target or path " + pointer);
    try {
        Json::json_pointer checked(toStdString(pointer));
        (void)checked;
    } catch (const Json::exception &error) {
        return Result<void>::failure(String("Invalid Prefab property path: ") + error.what());
    }
    auto instance = findInstance(linked.document, instanceId);
    if (!instance)
        return Result<void>::failure(instance.error);
    (*instance.value)["overrides"]["properties"][toStdString(objectId.toString())][toStdString(pointer)] = value;
    return {};
}

Result<void> clearPrefabPropertyOverride(LinkedSceneResource &linked, ResourceId instanceId, ResourceId objectId,
                                         const String &pointer) {
    auto instance = findInstance(linked.document, instanceId);
    if (!instance)
        return Result<void>::failure(instance.error);
    auto &overrides = (*instance.value)["overrides"];
    if (!overrides.contains("properties") || !overrides["properties"].contains(toStdString(objectId.toString())) ||
        !overrides["properties"][toStdString(objectId.toString())].contains(toStdString(pointer)))
        return Result<void>::failure("Missing Prefab property override " + objectId.toString() + pointer);
    overrides["properties"][toStdString(objectId.toString())].erase(toStdString(pointer));
    if (overrides["properties"][toStdString(objectId.toString())].empty())
        overrides["properties"].erase(toStdString(objectId.toString()));
    return {};
}

Result<void> setPrefabStructureOverride(LinkedSceneResource &linked, ResourceId instanceId, const String &operation,
                                        const Json &value) {
    static const Set<String> arrays{"deletedNodes", "addedNodes", "deletedComponents", "addedComponents"};
    static const Set<String> objects{"nodeParents", "nodeOrders", "componentOwners", "componentOrders"};
    if ((arrays.count(operation) && !value.is_array()) || (objects.count(operation) && !value.is_object()) ||
        (!arrays.count(operation) && !objects.count(operation)))
        return Result<void>::failure("Invalid Prefab structural operation " + operation);
    auto instance = findInstance(linked.document, instanceId);
    if (!instance)
        return Result<void>::failure(instance.error);
    (*instance.value)["overrides"][toStdString(operation)] = value;
    return {};
}
} // namespace ursa
