#include <serialization/JsonInterop.hpp>
#include <serialization/SceneResource.hpp>
#include <serialization/JsonResource.hpp>
#include <cmath>
#include <functional>
#include <limits>
#include <set>

namespace ursa {
namespace {
using Json = nlohmann::json;
constexpr size_t maxSceneNodes = 100000;
constexpr size_t maxSceneComponents = 100000;

Json colorJson(Color value) {
    return Json::array({value.red, value.green, value.blue, value.alpha});
}

Json transformJson(LocalTransform value) {
    return {{"position", Json::array({value.position.x, value.position.y, value.position.z})},
            {"rotation", Json::array({value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w})},
            {"scale", Json::array({value.scale.x, value.scale.y, value.scale.z})}};
}

Result<Vector<float>> readFloatArray(const Json &object, const char *field, size_t count, const String &context) {
    auto value = object.find(field);
    if (value == object.end() || !value->is_array() || value->size() != count)
        return Result<Vector<float>>::failure("Invalid " + context + "." + field);
    Vector<float> numbers;
    numbers.reserve(count);
    for (const auto &number : *value) {
        if (!number.is_number())
            return Result<Vector<float>>::failure("Invalid number in " + context + "." + field);
        const double converted = number.get<double>();
        if (!std::isfinite(converted) || std::abs(converted) > std::numeric_limits<float>::max())
            return Result<Vector<float>>::failure("Out-of-range number in " + context + "." + field);
        numbers.push_back(static_cast<float>(converted));
    }
    return Result<Vector<float>>::success(std::move(numbers));
}

Result<ResourceId> readId(const Json &object, const char *field, const String &context) {
    auto value = object.find(field);
    if (value == object.end() || !value->is_string())
        return Result<ResourceId>::failure("Invalid or missing " + context + "." + field);
    auto id = ResourceId::parse(value->get<String>());
    if (!id)
        return Result<ResourceId>::failure("Invalid " + context + "." + field + ": " + id.error);
    return id;
}

struct NodeRecord {
    ResourceId id;
    ResourceId parent;
    String label;
    LocalTransform local;
    bool enabled = true;
    Json components;
};

Result<NodeRecord> parseNode(const Json &item, const String &context) {
    if (!item.is_object())
        return Result<NodeRecord>::failure("Invalid node object in " + context);
    NodeRecord record;
    auto id = readId(item, "id", context);
    if (!id)
        return Result<NodeRecord>::failure(id.error);
    record.id = id.value;
    const auto label = item.find("label");
    if (label != item.end()) {
        if (!label->is_string())
            return Result<NodeRecord>::failure("Invalid " + context + ".label");
        record.label = label->get<String>();
        if (record.label.size() > 256 || record.label.find('\0') != String::npos)
            return Result<NodeRecord>::failure("Invalid " + context + ".label");
    }
    auto parent = item.find("parent");
    if (parent == item.end())
        return Result<NodeRecord>::failure("Missing " + context + ".parent");
    if (!parent->is_null()) {
        if (!parent->is_string())
            return Result<NodeRecord>::failure("Invalid " + context + ".parent");
        auto parsed = ResourceId::parse(parent->get<String>());
        if (!parsed)
            return Result<NodeRecord>::failure("Invalid " + context + ".parent: " + parsed.error);
        record.parent = parsed.value;
    }
    auto transform = item.find("transform");
    if (transform == item.end() || !transform->is_object())
        return Result<NodeRecord>::failure("Invalid " + context + ".transform");
    const auto position = readFloatArray(*transform, "position", 3, context + ".transform");
    const auto rotation = readFloatArray(*transform, "rotation", 4, context + ".transform");
    const auto scale = readFloatArray(*transform, "scale", 3, context + ".transform");
    if (!position || !rotation || !scale)
        return Result<NodeRecord>::failure(!position ? position.error : !rotation ? rotation.error : scale.error);
    record.local.position = {position.value[0], position.value[1], position.value[2]};
    record.local.rotation = {rotation.value[0], rotation.value[1], rotation.value[2], rotation.value[3]};
    record.local.scale = {scale.value[0], scale.value[1], scale.value[2]};
    auto enabled = item.find("enabled");
    if (enabled == item.end() || !enabled->is_boolean())
        return Result<NodeRecord>::failure("Invalid " + context + ".enabled");
    record.enabled = enabled->get<bool>();
    auto components = item.find("components");
    if (components == item.end() || !components->is_array())
        return Result<NodeRecord>::failure("Invalid " + context + ".components");
    record.components = *components;
    return Result<NodeRecord>::success(std::move(record));
}
} // namespace

Result<Json> encodeSceneDocument(const std::filesystem::path &path, ResourceId id, const Scene &scene,
                                 const ComponentRegistry &registry, const MaterialIdResolver &materialId,
                                 NodeId subtree) {
    if (!id || path.extension() != ".ursascene")
        return Result<Json>::failure("Invalid scene resource ID or .ursascene path: " + pathToUrsaString(path));
    if (subtree && !scene.contains(subtree))
        return Result<Json>::failure("Invalid scene subtree root at " + pathToUrsaString(path));
    Json nodes = Json::array();
    const ComponentSaveContext context{scene, materialId};
    std::function<Result<void>(NodeId)> append = [&](NodeId node) -> Result<void> {
        if (nodes.size() >= maxSceneNodes)
            return Result<void>::failure("Scene node limit exceeded at " + pathToUrsaString(path));
        const auto nodeId = scene.nodePersistentId(node);
        const auto parent = scene.parentOf(node);
        const auto local = scene.localTransform(node);
        const auto label = scene.nodeLabel(node);
        const auto enabled = scene.isEnabled(node);
        const auto components = scene.componentsOf(node);
        if (!nodeId || !parent || !local || !label || !enabled || !components)
            return Result<void>::failure("Cannot inspect scene node at " + pathToUrsaString(path));
        Json componentData = Json::array();
        for (ComponentId componentId : components.value) {
            auto codec = registry.findFor(*scene.component(componentId));
            if (!codec)
                return Result<void>::failure("Cannot save component on node " + nodeId.value.toString() + " in " +
                                             pathToUrsaString(path) + ": " + codec.error);
            auto data = codec.value->save(*scene.component(componentId), context);
            if (!data)
                return Result<void>::failure("Cannot save component " + codec.value->type + " on node " +
                                             nodeId.value.toString() + " in " + pathToUrsaString(path) + ": " +
                                             data.error);
            const auto persistent = scene.componentPersistentId(componentId);
            const auto active = scene.isComponentEnabled(componentId);
            if (!persistent || !active)
                return Result<void>::failure("Cannot inspect component on node " + nodeId.value.toString());
            componentData.push_back({{"id", persistent.value.toString()},
                                     {"type", codec.value->type},
                                     {"version", codec.value->version},
                                     {"enabled", active.value},
                                     {"data", std::move(data.value)}});
        }
        const auto parentId = parent.value ? scene.nodePersistentId(parent.value) : Result<ResourceId>{};
        if (parent.value && !parentId)
            return Result<void>::failure(parentId.error);
        nodes.push_back({{"id", nodeId.value.toString()},
                         {"label", label.value},
                         {"parent", parent.value ? Json(parentId.value.toString()) : Json(nullptr)},
                         {"transform", transformJson(local.value)},
                         {"enabled", enabled.value},
                         {"components", std::move(componentData)}});
        auto children = scene.childrenOf(node);
        if (!children)
            return Result<void>::failure(children.error);
        for (NodeId child : children.value) {
            auto result = append(child);
            if (!result)
                return result;
        }
        return {};
    };
    const auto roots = subtree ? Vector<NodeId>{subtree} : scene.rootNodes();
    for (NodeId root : roots) {
        auto result = append(root);
        if (!result)
            return Result<Json>::failure(result.error);
    }
    Json activeCamera = nullptr;
    if (scene.activeCamera()) {
        auto cameraId = scene.componentPersistentId(scene.activeCamera());
        if (!cameraId)
            return Result<Json>::failure("Invalid active camera in " + pathToUrsaString(path) + ": " + cameraId.error);
        activeCamera = cameraId.value.toString();
    }
    const auto ambient = scene.ambientLightColor();
    const Json document{{"format", "UrsaScene"},
                        {"version", resourceFormatVersion},
                        {"id", id.toString()},
                        {"ambient", {{"color", colorJson(ambient)}, {"intensity", scene.ambientLightIntensity()}}},
                        {"activeCamera", std::move(activeCamera)},
                        {"nodes", std::move(nodes)}};
    return Result<Json>::success(document);
}

Result<void> saveSceneResource(const std::filesystem::path &path, ResourceId id, const Scene &scene,
                               const ComponentRegistry &registry, const MaterialIdResolver &materialId) {
    auto document = encodeSceneDocument(path, id, scene, registry, materialId);
    return document ? writeJsonResource(path, document.value) : Result<void>::failure(document.error);
}

Result<LoadedSceneResource> decodeSceneDocument(const std::filesystem::path &path, const Json &document,
                                                const ComponentRegistry &registry, ComponentLoadContext &context) {
    context.clearMappings();
    auto nodes = document.find("nodes");
    if (nodes == document.end() || !nodes->is_array() || nodes->size() > maxSceneNodes)
        return Result<LoadedSceneResource>::failure("Invalid scene nodes at " + pathToUrsaString(path));
    Vector<NodeRecord> records;
    Set<String> nodeIds;
    Set<String> componentIds;
    size_t componentCount = 0;
    for (size_t index = 0; index < nodes->size(); ++index) {
        auto record =
            parseNode((*nodes)[index], pathToUrsaString(path) + ".nodes[" + toUrsaString(std::to_string(index)) + "]");
        if (!record)
            return Result<LoadedSceneResource>::failure(record.error);
        if (!nodeIds.insert(record.value.id.toString()).second)
            return Result<LoadedSceneResource>::failure("Duplicate node ID " + record.value.id.toString() + " in " +
                                                        pathToUrsaString(path));
        componentCount += record.value.components.size();
        if (componentCount > maxSceneComponents)
            return Result<LoadedSceneResource>::failure("Scene component limit exceeded at " + pathToUrsaString(path));
        records.push_back(std::move(record.value));
    }

    auto scene = std::make_unique<Scene>();
    for (const auto &record : records) {
        auto created = scene->createNode();
        if (!created)
            return Result<LoadedSceneResource>::failure(created.error);
        auto assigned = scene->assignNodePersistentId(created.value, record.id);
        if (!assigned)
            return Result<LoadedSceneResource>::failure(assigned.error);
        auto labeled = scene->setNodeLabel(created.value, record.label);
        if (!labeled)
            return Result<LoadedSceneResource>::failure(labeled.error);
        auto mapped = context.mapNode(record.id, created.value);
        if (!mapped)
            return Result<LoadedSceneResource>::failure(mapped.error + " in " + pathToUrsaString(path));
        auto placed = scene->setLocalTransform(created.value, record.local);
        if (!placed)
            return Result<LoadedSceneResource>::failure(placed.error);
        auto enabled = scene->setEnabled(created.value, record.enabled);
        if (!enabled)
            return Result<LoadedSceneResource>::failure(enabled.error);
    }
    for (const auto &record : records) {
        if (!record.parent)
            continue;
        auto node = context.node(record.id);
        auto parent = context.node(record.parent);
        if (!parent)
            return Result<LoadedSceneResource>::failure("Missing parent " + record.parent.toString() + " for node " +
                                                        record.id.toString() + " in " + pathToUrsaString(path));
        auto reparented = scene->setParent(node.value, parent.value);
        if (!reparented)
            return Result<LoadedSceneResource>::failure("Invalid parent for node " + record.id.toString() + " in " +
                                                        pathToUrsaString(path) + ": " + reparented.error);
    }

    struct PendingBind {
        const ComponentCodec *codec;
        ComponentId component;
        Json data;
        String location;
    };

    Vector<PendingBind> binds;
    for (const auto &record : records) {
        const auto owner = context.node(record.id);
        for (size_t index = 0; index < record.components.size(); ++index) {
            const auto &item = record.components[index];
            const auto location = pathToUrsaString(path) + ".node[" + record.id.toString() + "].components[" +
                                  toUrsaString(std::to_string(index)) + "]";
            if (!item.is_object())
                return Result<LoadedSceneResource>::failure("Invalid component at " + location);
            auto id = readId(item, "id", location);
            if (!id)
                return Result<LoadedSceneResource>::failure(id.error);
            if (!componentIds.insert(id.value.toString()).second)
                return Result<LoadedSceneResource>::failure("Duplicate component ID " + id.value.toString() + " at " +
                                                            location);
            auto type = item.find("type");
            auto version = item.find("version");
            auto enabled = item.find("enabled");
            auto data = item.find("data");
            if (type == item.end() || !type->is_string() || version == item.end() || !version->is_number_unsigned() ||
                enabled == item.end() || !enabled->is_boolean() || data == item.end())
                return Result<LoadedSceneResource>::failure("Invalid component metadata at " + location);
            const auto name = type->get<String>();
            const auto *codec = registry.find(name);
            if (!codec)
                return Result<LoadedSceneResource>::failure("Unregistered component type " + name + " at " + location);
            if (version->get<uint64_t>() != codec->version)
                return Result<LoadedSceneResource>::failure("Unsupported component version for " + name + " at " +
                                                            location);
            auto created = codec->create(*scene, owner.value, *data, context);
            if (!created)
                return Result<LoadedSceneResource>::failure("Cannot create component " + name + " at " + location +
                                                            ": " + created.error);
            auto assigned = scene->assignComponentPersistentId(created.value, id.value);
            if (!assigned)
                return Result<LoadedSceneResource>::failure(assigned.error + " at " + location);
            auto mapped = context.mapComponent(id.value, created.value);
            if (!mapped)
                return Result<LoadedSceneResource>::failure(mapped.error + " at " + location);
            auto changed = scene->setComponentEnabled(created.value, enabled->get<bool>());
            if (!changed)
                return Result<LoadedSceneResource>::failure(changed.error + " at " + location);
            binds.push_back({codec, created.value, *data, location});
        }
    }
    for (const auto &pending : binds)
        if (pending.codec->bind) {
            auto bound = pending.codec->bind(*scene, pending.component, pending.data, context);
            if (!bound)
                return Result<LoadedSceneResource>::failure("Cannot bind component at " + pending.location + ": " +
                                                            bound.error);
        }

    auto ambient = document.find("ambient");
    if (ambient == document.end() || !ambient->is_object())
        return Result<LoadedSceneResource>::failure("Invalid scene ambient light at " + pathToUrsaString(path));
    const auto color = readFloatArray(*ambient, "color", 4, pathToUrsaString(path) + ".ambient");
    auto intensity = ambient->find("intensity");
    if (!color || intensity == ambient->end() || !intensity->is_number())
        return Result<LoadedSceneResource>::failure("Invalid scene ambient light at " + pathToUrsaString(path));
    const double light = intensity->get<double>();
    if (!std::isfinite(light) || light < 0.0 || light > std::numeric_limits<float>::max())
        return Result<LoadedSceneResource>::failure("Invalid scene ambient intensity at " + pathToUrsaString(path));
    auto lit = scene->setAmbientLight({color.value[0], color.value[1], color.value[2], color.value[3]},
                                      static_cast<float>(light));
    if (!lit)
        return Result<LoadedSceneResource>::failure(lit.error + " at " + pathToUrsaString(path));
    auto activeCamera = document.find("activeCamera");
    if (activeCamera == document.end())
        return Result<LoadedSceneResource>::failure("Missing scene activeCamera at " + pathToUrsaString(path));
    if (!activeCamera->is_null()) {
        auto cameraId = readId(document, "activeCamera", pathToUrsaString(path));
        if (!cameraId)
            return Result<LoadedSceneResource>::failure(cameraId.error);
        auto camera = context.component(cameraId.value);
        if (!camera)
            return Result<LoadedSceneResource>::failure("Missing active camera component " + cameraId.value.toString() +
                                                        " at " + pathToUrsaString(path));
        auto selected = scene->setActiveCamera(camera.value);
        if (!selected)
            return Result<LoadedSceneResource>::failure(selected.error + " at " + pathToUrsaString(path));
    }
    auto id = readId(document, "id", pathToUrsaString(path));
    if (!id)
        return Result<LoadedSceneResource>::failure(id.error);
    return Result<LoadedSceneResource>::success({id.value, std::move(scene)});
}

Result<LoadedSceneResource> loadSceneResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                              ComponentLoadContext &context) {
    auto document = readJsonResource(path, ResourceKind::Scene);
    if (!document)
        return Result<LoadedSceneResource>::failure(document.error);
    if (document.value.contains("instances"))
        return Result<LoadedSceneResource>::failure("Linked scene at " + pathToUrsaString(path) +
                                                    " must be loaded with loadLinkedSceneResource");
    return decodeSceneDocument(path, document.value, registry, context);
}

Result<void> reloadSceneResource(const std::filesystem::path &path, const ComponentRegistry &registry,
                                 ComponentLoadContext &context, std::unique_ptr<Scene> &activeScene) {
    auto candidate = loadSceneResource(path, registry, context);
    if (!candidate)
        return Result<void>::failure(candidate.error);
    activeScene = std::move(candidate.value.scene);
    return {};
}
} // namespace ursa
