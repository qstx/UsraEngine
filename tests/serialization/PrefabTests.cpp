#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <serialization/PrefabResource.hpp>
#include <serialization/JsonResource.hpp>
#include <serialization/MaterialResource.hpp>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
    return std::move(result.value);
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

class UnregisteredComponent final : public ursa::Component {};

void placePrefab(const std::filesystem::path &root, ursa::Scene &source, ursa::NodeId subtree) {
    ursa::ComponentRegistry registry;
    if (!ursa::registerBuiltinComponents(registry))
        return;
    const auto prefabId = ursa::ResourceId::generate();
    if (!ursa::savePrefabResource(root / "tile.ursaprefab", prefabId, source, subtree, registry))
        return;
    ursa::Scene empty;
    const auto scenePath = root / "level.ursascene";
    auto document = ursa::encodeSceneDocument(scenePath, ursa::ResourceId::generate(), empty, registry);
    if (!document)
        return;
    const auto instanceId = ursa::ResourceId::generate();
    if (!ursa::addPrefabInstance(document.value, prefabId, instanceId) ||
        !ursa::writeJsonResource(scenePath, document.value))
        return;
    auto resources = ursa::ResourceSession::open({root});
    if (!resources)
        return;
    ursa::ComponentLoadContext context(*resources.value);
    auto linked = ursa::loadLinkedSceneResource(scenePath, registry, context);
    if (!linked)
        return;
    auto sourceId = source.nodePersistentId(subtree);
    if (!sourceId ||
        !ursa::setPrefabPropertyOverride(linked.value, instanceId, sourceId.value, "/transform/position/0", 2.0f) ||
        !ursa::saveLinkedSceneResource(scenePath, linked.value, registry))
        return;
    auto refreshed = resources.value->refreshed();
    if (!refreshed)
        return;
    ursa::ComponentLoadContext reloadContext(*refreshed.value);
    if (!ursa::reloadLinkedSceneResource(scenePath, registry, reloadContext, linked.value))
        return;
    if (!ursa::clearPrefabPropertyOverride(linked.value, instanceId, sourceId.value, "/transform/position/0") ||
        !ursa::saveLinkedSceneResource(scenePath, linked.value, registry))
        return;
    ursa::reloadLinkedSceneResource(scenePath, registry, reloadContext, linked.value);
}

void testInstances(const std::filesystem::path &root) {
    ursa::ComponentRegistry registry;
    done(ursa::registerBuiltinComponents(registry));
    ursa::Scene source;
    auto prefabRoot = take(source.createNode());
    auto child = take(source.createNode(prefabRoot));
    take(source.addComponent<ursa::BoxRenderComponent>(child, ursa::Color{0.2f, 0.3f, 0.4f, 1.0f}));
    auto unrelated = take(source.createNode());
    take(source.addComponent<UnregisteredComponent>(unrelated));
    const auto prefabId = ursa::ResourceId::generate();
    const auto prefabPath = root / "box.ursaprefab";
    done(ursa::savePrefabResource(prefabPath, prefabId, source, prefabRoot, registry));
    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext prefabContext(*session);
    auto direct = take(ursa::loadPrefabResource(prefabPath, registry, prefabContext));
    expect(direct.scene->rootNodes().size() == 1 &&
               take(direct.scene->childrenOf(direct.scene->rootNodes()[0])).size() == 1,
           "Standalone Prefab subtree was not restored");

    ursa::Scene plain;
    auto outside = take(plain.createNode());
    const auto outsideId = take(plain.nodePersistentId(outside));
    const auto firstPath = root / "first.ursascene";
    auto first = take(ursa::encodeSceneDocument(firstPath, ursa::ResourceId::generate(), plain, registry));
    const auto firstInstance = ursa::ResourceId::generate();
    const auto secondInstance = ursa::ResourceId::generate();
    done(ursa::addPrefabInstance(first, prefabId, firstInstance, outsideId));
    done(ursa::addPrefabInstance(first, prefabId, secondInstance, outsideId));
    done(ursa::writeJsonResource(firstPath, first));
    const auto secondPath = root / "second.ursascene";
    auto second = take(ursa::encodeSceneDocument(secondPath, ursa::ResourceId::generate(), plain, registry));
    done(ursa::addPrefabInstance(second, prefabId, ursa::ResourceId::generate(), outsideId));
    done(ursa::writeJsonResource(secondPath, second));
    auto refreshed = take(session->refreshed());
    ursa::ComponentLoadContext plainContext(*refreshed);
    auto incompatible = ursa::loadSceneResource(firstPath, registry, plainContext);
    expect(!incompatible && incompatible.error.find("loadLinkedSceneResource") != ursa::String::npos,
           "Plain scene loader silently discarded Prefab instances");
    ursa::ComponentLoadContext firstContext(*refreshed);
    ursa::ComponentLoadContext secondContext(*refreshed);
    auto firstLoaded = take(ursa::loadLinkedSceneResource(firstPath, registry, firstContext));
    auto secondLoaded = take(ursa::loadLinkedSceneResource(secondPath, registry, secondContext));
    auto firstRoot = take(firstLoaded.scene->nodeByPersistentId(outsideId));
    auto secondRoot = take(secondLoaded.scene->nodeByPersistentId(outsideId));
    const auto firstChildren = take(firstLoaded.scene->childrenOf(firstRoot));
    const auto secondChildren = take(secondLoaded.scene->childrenOf(secondRoot));
    expect(firstChildren.size() == 2 && secondChildren.size() == 1 &&
               take(firstLoaded.scene->nodePersistentId(firstChildren[0])) !=
                   take(firstLoaded.scene->nodePersistentId(firstChildren[1])) &&
               firstLoaded.document["instances"][0]["prefab"] == ursa::toStdString(prefabId.toString()) &&
               secondLoaded.document["instances"][0]["prefab"] == ursa::toStdString(prefabId.toString()),
           "Prefab instances lost their independent identity or source reference");
    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
    std::filesystem::remove(prefabPath);
}

void testProperties(const std::filesystem::path &root) {
    ursa::ComponentRegistry registry;
    done(ursa::registerBuiltinComponents(registry));
    auto initialMaterial = std::make_shared<ursa::MaterialInstance>(ursa::MaterialType::Unlit);
    auto changedMaterial = std::make_shared<ursa::MaterialInstance>(ursa::MaterialType::Unlit);
    done(initialMaterial->setBaseColor({0.8f, 0.1f, 0.1f, 1.0f}));
    done(changedMaterial->setBaseColor({0.1f, 0.8f, 0.1f, 1.0f}));
    const auto initialMaterialId = ursa::ResourceId::generate();
    const auto changedMaterialId = ursa::ResourceId::generate();
    done(ursa::saveMaterialResource(root / "initial.ursamat", initialMaterialId, *initialMaterial, {}));
    done(ursa::saveMaterialResource(root / "changed.ursamat", changedMaterialId, *changedMaterial, {}));
    ursa::Scene source;
    const auto sourceRoot = take(source.createNode());
    const auto sourceChild = take(source.createNode(sourceRoot));
    ursa::LocalTransform initialTransform;
    initialTransform.position = {1.0f, 1.0f, 0.0f};
    done(source.setLocalTransform(sourceRoot, initialTransform));
    const auto boxId =
        take(source.addComponent<ursa::BoxRenderComponent>(sourceChild, ursa::Color{0.2f, 0.3f, 0.4f, 1.0f}));
    auto *box = static_cast<ursa::BoxRenderComponent *>(source.component(boxId));
    box->material = initialMaterial;
    const auto sourceRootId = take(source.nodePersistentId(sourceRoot));
    const auto sourceBoxId = take(source.componentPersistentId(boxId));
    const auto prefabId = ursa::ResourceId::generate();
    const auto prefabPath = root / "properties.ursaprefab";
    const auto materialResolver = [&](const std::shared_ptr<ursa::MaterialInstance> &material) {
        return ursa::Result<ursa::ResourceId>::success(
            material == initialMaterial || material->parameters().baseColor.red == 0.8f ? initialMaterialId
                                                                                        : changedMaterialId);
    };
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry, materialResolver));
    const auto path = root / "properties.ursascene";
    ursa::Scene empty;
    auto document = take(ursa::encodeSceneDocument(path, ursa::ResourceId::generate(), empty, registry));
    const auto firstInstance = ursa::ResourceId::generate();
    done(ursa::addPrefabInstance(document, prefabId, firstInstance));
    done(ursa::addPrefabInstance(document, prefabId, ursa::ResourceId::generate()));
    done(ursa::writeJsonResource(path, document));
    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext context(*session);
    auto loaded = take(ursa::loadLinkedSceneResource(path, registry, context));
    done(ursa::setPrefabPropertyOverride(loaded, firstInstance, sourceRootId, "/transform/position/0", 9.0f));
    done(ursa::setPrefabPropertyOverride(loaded, firstInstance, sourceBoxId, "/data/color/0", 0.9f));
    done(ursa::setPrefabPropertyOverride(loaded, firstInstance, sourceBoxId, "/data/material",
                                         ursa::toStdString(initialMaterialId.toString())));
    done(ursa::saveLinkedSceneResource(path, loaded, registry));

    initialTransform.position = {2.0f, 3.0f, 0.0f};
    done(source.setLocalTransform(sourceRoot, initialTransform));
    box->color = {0.5f, 0.7f, 0.4f, 1.0f};
    box->material = changedMaterial;
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry, materialResolver));
    auto refreshed = take(session->refreshed());
    ursa::ComponentLoadContext refreshContext(*refreshed);
    done(ursa::reloadLinkedSceneResource(path, registry, refreshContext, loaded));
    const auto roots = loaded.scene->rootNodes();
    expect(roots.size() == 2, "Updated Prefab lost an instance");
    const auto firstTransform = take(loaded.scene->localTransform(roots[0]));
    const auto secondTransform = take(loaded.scene->localTransform(roots[1]));
    auto firstChild = take(loaded.scene->childrenOf(roots[0]))[0];
    auto secondChild = take(loaded.scene->childrenOf(roots[1]))[0];
    auto *firstBox = loaded.scene->findComponent<ursa::BoxRenderComponent>(firstChild);
    auto *secondBox = loaded.scene->findComponent<ursa::BoxRenderComponent>(secondChild);
    expect(firstTransform.position.x == 9.0f && firstTransform.position.y == 3.0f &&
               secondTransform.position.x == 2.0f && secondTransform.position.y == 3.0f && firstBox && secondBox &&
               firstBox->color.red == 0.9f && firstBox->color.green == 0.7f && secondBox->color.red == 0.5f &&
               firstBox->material != secondBox->material && firstBox->material->parameters().baseColor.red == 0.8f &&
               secondBox->material->parameters().baseColor.green == 0.8f,
           "Prefab property inheritance or explicit material override failed");
    done(ursa::clearPrefabPropertyOverride(loaded, firstInstance, sourceBoxId, "/data/material"));
    done(ursa::saveLinkedSceneResource(path, loaded, registry));
    done(ursa::reloadLinkedSceneResource(path, registry, refreshContext, loaded));
    firstChild = take(loaded.scene->childrenOf(loaded.scene->rootNodes()[0]))[0];
    firstBox = loaded.scene->findComponent<ursa::BoxRenderComponent>(firstChild);
    expect(firstBox && firstBox->material->parameters().baseColor.green == 0.8f,
           "Clearing the explicit material override did not restore source inheritance");
    std::filesystem::remove(path);
    std::filesystem::remove(prefabPath);
    std::filesystem::remove(root / "initial.ursamat");
    std::filesystem::remove(root / "changed.ursamat");
}

void testStructure(const std::filesystem::path &root) {
    ursa::ComponentRegistry registry;
    done(ursa::registerBuiltinComponents(registry));
    ursa::Scene source;
    const auto sourceRoot = take(source.createNode());
    const auto first = take(source.createNode(sourceRoot));
    const auto removed = take(source.createNode(sourceRoot));
    const auto moved = take(source.createNode(sourceRoot));
    const auto firstComponent = take(source.addComponent<ursa::BoxRenderComponent>(first));
    const auto removedComponent = take(source.addComponent<ursa::BoxRenderComponent>(first));
    const auto rootId = ursa::toStdString(take(source.nodePersistentId(sourceRoot)).toString());
    const auto firstId = ursa::toStdString(take(source.nodePersistentId(first)).toString());
    const auto removedId = ursa::toStdString(take(source.nodePersistentId(removed)).toString());
    const auto movedId = ursa::toStdString(take(source.nodePersistentId(moved)).toString());
    const auto firstComponentId = ursa::toStdString(take(source.componentPersistentId(firstComponent)).toString());
    const auto removedComponentId = ursa::toStdString(take(source.componentPersistentId(removedComponent)).toString());
    const auto addedId = ursa::toStdString(ursa::ResourceId::generate().toString());
    const auto addedComponentId = ursa::toStdString(ursa::ResourceId::generate().toString());
    const auto secondAddedComponentId = ursa::toStdString(ursa::ResourceId::generate().toString());
    const auto prefabId = ursa::ResourceId::generate();
    const auto prefabPath = root / "structure.ursaprefab";
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
    const auto path = root / "structure.ursascene";
    ursa::Scene plain;
    auto document = take(ursa::encodeSceneDocument(path, ursa::ResourceId::generate(), plain, registry));
    const auto instanceId = ursa::ResourceId::generate();
    done(ursa::addPrefabInstance(document, prefabId, instanceId));
    done(ursa::writeJsonResource(path, document));
    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext context(*session);
    auto linked = take(ursa::loadLinkedSceneResource(path, registry, context));
    const nlohmann::json addedNode{
        {"id", addedId},
        {"parent", rootId},
        {"transform",
         {{"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f, 1.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}},
        {"enabled", true},
        {"components", nlohmann::json::array()}};
    const auto componentRecord = [](const std::string &id, float red) {
        return nlohmann::json{{"id", id},
                              {"type", "ursa.BoxRender"},
                              {"version", 1},
                              {"enabled", true},
                              {"data", {{"color", {red, 0.0f, 0.0f, 1.0f}}, {"material", nullptr}}}};
    };
    done(ursa::setPrefabStructureOverride(linked, instanceId, "deletedNodes", {removedId}));
    done(ursa::setPrefabStructureOverride(linked, instanceId, "addedNodes", nlohmann::json::array({addedNode})));
    done(ursa::setPrefabStructureOverride(linked, instanceId, "nodeParents", {{movedId, firstId}}));
    done(ursa::setPrefabStructureOverride(linked, instanceId, "nodeOrders", {{rootId, {addedId, firstId}}}));
    done(ursa::setPrefabStructureOverride(linked, instanceId, "deletedComponents", {removedComponentId}));
    done(ursa::setPrefabStructureOverride(
        linked, instanceId, "addedComponents",
        nlohmann::json::array({{{"owner", addedId}, {"component", componentRecord(addedComponentId, 0.3f)}},
                               {{"owner", movedId}, {"component", componentRecord(secondAddedComponentId, 0.8f)}}})));
    done(ursa::setPrefabStructureOverride(linked, instanceId, "componentOwners", {{firstComponentId, movedId}}));
    done(ursa::setPrefabStructureOverride(linked, instanceId, "componentOrders",
                                          {{movedId, {secondAddedComponentId, firstComponentId}}}));
    done(ursa::saveLinkedSceneResource(path, linked, registry));
    const auto sourceNew = take(source.createNode(sourceRoot));
    const auto sourceNewId = take(source.nodePersistentId(sourceNew));
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
    auto refreshed = take(session->refreshed());
    ursa::ComponentLoadContext refreshContext(*refreshed);
    done(ursa::reloadLinkedSceneResource(path, registry, refreshContext, linked));
    const auto runtimeRoot = linked.scene->rootNodes()[0];
    const auto rootChildren = take(linked.scene->childrenOf(runtimeRoot));
    expect(rootChildren.size() == 3, "Structural merge lost source addition or instance addition");
    const auto addedRuntime = rootChildren[0];
    const auto firstRuntime = rootChildren[1];
    const auto movedRuntime = take(linked.scene->childrenOf(firstRuntime))[0];
    expect(take(linked.scene->nodePersistentId(rootChildren[2])) != sourceNewId &&
               take(linked.scene->childrenOf(firstRuntime)).size() == 1 &&
               take(linked.scene->componentsOf(firstRuntime)).empty() &&
               take(linked.scene->componentsOf(addedRuntime)).size() == 1 &&
               take(linked.scene->componentsOf(movedRuntime)).size() == 2,
           "Prefab node or component structural overrides were not applied");
    const auto movedComponents = take(linked.scene->componentsOf(movedRuntime));
    auto *ordered = static_cast<ursa::BoxRenderComponent *>(linked.scene->component(movedComponents[0]));
    expect(ordered && ordered->color.red == 0.8f, "Prefab component order override was not preserved");
    auto *edited = static_cast<ursa::BoxRenderComponent *>(linked.scene->component(movedComponents[1]));
    edited->color.red = 0.6f;
    const auto runtimeAdded = take(linked.scene->createNode(runtimeRoot));
    take(linked.scene->addComponent<ursa::BoxRenderComponent>(runtimeAdded, ursa::Color{0.4f, 0.5f, 0.6f, 1.0f}));
    done(ursa::saveLinkedSceneResource(path, linked, registry));
    done(ursa::saveLinkedSceneResource(path, linked, registry));
    done(ursa::reloadLinkedSceneResource(path, registry, refreshContext, linked));
    const auto capturedRoot = linked.scene->rootNodes()[0];
    const auto capturedChildren = take(linked.scene->childrenOf(capturedRoot));
    expect(capturedChildren.size() == 4, "Directly added instance node was not captured on save");
    const auto capturedMoved = take(linked.scene->childrenOf(capturedChildren[1]))[0];
    const auto capturedComponents = take(linked.scene->componentsOf(capturedMoved));
    edited = static_cast<ursa::BoxRenderComponent *>(linked.scene->component(capturedComponents[1]));
    expect(edited && edited->color.red == 0.6f, "Directly edited component field was not captured on save");
    done(linked.scene->destroyNode(capturedChildren[3]));
    done(ursa::saveLinkedSceneResource(path, linked, registry));
    done(ursa::reloadLinkedSceneResource(path, registry, refreshContext, linked));
    expect(take(linked.scene->childrenOf(linked.scene->rootNodes()[0])).size() == 3,
           "Deleting a directly added Prefab node left a stale structural override");
    std::filesystem::remove(path);
    std::filesystem::remove(prefabPath);
}

void testConflicts(const std::filesystem::path &root) {
    ursa::ComponentRegistry registry;
    done(ursa::registerBuiltinComponents(registry));
    ursa::Scene source;
    const auto sourceRoot = take(source.createNode());
    const auto first = take(source.createNode(sourceRoot));
    const auto second = take(source.createNode(sourceRoot));
    const auto rootId = take(source.nodePersistentId(sourceRoot));
    const auto firstId = take(source.nodePersistentId(first));
    const auto secondId = take(source.nodePersistentId(second));
    const auto prefabId = ursa::ResourceId::generate();
    const auto prefabPath = root / "conflict.ursaprefab";
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
    const auto path = root / "conflict.ursascene";
    ursa::Scene empty;
    auto base = take(ursa::encodeSceneDocument(path, ursa::ResourceId::generate(), empty, registry));
    const auto firstInstance = ursa::ResourceId::generate();
    const auto secondInstance = ursa::ResourceId::generate();
    done(ursa::addPrefabInstance(base, prefabId, firstInstance));
    done(ursa::addPrefabInstance(base, prefabId, secondInstance));
    done(ursa::writeJsonResource(path, base));
    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext context(*session);
    auto active = take(ursa::loadLinkedSceneResource(path, registry, context));
    auto *original = active.scene.get();
    const auto checkFailure = [&](const nlohmann::json &document, const std::string &expected) {
        done(ursa::writeJsonResource(path, document));
        std::ifstream beforeFile(path, std::ios::binary);
        const std::string before(std::istreambuf_iterator<char>{beforeFile}, std::istreambuf_iterator<char>{});
        auto failed = ursa::reloadLinkedSceneResource(path, registry, context, active);
        std::ifstream afterFile(path, std::ios::binary);
        const std::string after(std::istreambuf_iterator<char>{afterFile}, std::istreambuf_iterator<char>{});
        expect(!failed && active.scene.get() == original && before == after &&
                   failed.error.find(prefabId.toString()) != ursa::String::npos &&
                   failed.error.find(firstInstance.toString()) != ursa::String::npos &&
                   failed.error.find(ursa::toUrsaString(expected)) != ursa::String::npos,
               "Prefab conflict did not preserve the active scene and saved patch with a precise error");
    };

    auto missing = base;
    missing["instances"][0]["overrides"]["properties"][ursa::toStdString(firstId.toString())]["/enabled"] = false;
    done(source.destroyNode(first));
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));
    checkFailure(missing, "missing property target");
    const auto restoredFirst = take(source.createNode(sourceRoot));
    done(source.assignNodePersistentId(restoredFirst, firstId));
    done(ursa::savePrefabResource(prefabPath, prefabId, source, sourceRoot, registry));

    auto duplicate = base;
    duplicate["instances"][0]["overrides"]["addedNodes"] =
        nlohmann::json::array({{{"id", ursa::toStdString(secondId.toString())},
                                {"parent", ursa::toStdString(rootId.toString())},
                                {"components", nlohmann::json::array()}}});
    checkFailure(duplicate, "Duplicate added node ID");

    auto cycle = base;
    cycle["instances"][0]["overrides"]["nodeParents"] = {
        {ursa::toStdString(firstId.toString()), ursa::toStdString(secondId.toString())},
        {ursa::toStdString(secondId.toString()), ursa::toStdString(firstId.toString())}};
    checkFailure(cycle, "cycle");

    auto crossInstance = base;
    const auto otherRoot = active.scene->rootNodes()[1];
    crossInstance["instances"][0]["overrides"]["nodeParents"] = {
        {ursa::toStdString(firstId.toString()),
         ursa::toStdString(take(active.scene->nodePersistentId(otherRoot)).toString())}};
    checkFailure(crossInstance, "Cross-instance");
    done(ursa::writeJsonResource(path, base));
    done(ursa::reloadLinkedSceneResource(path, registry, context, active));
    expect(active.scene.get() != original, "Valid explicit reload did not replace the active scene");
    std::filesystem::remove(path);
    std::filesystem::remove(prefabPath);
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          ("UrsaPrefabTests-" + ursa::toStdString(ursa::ResourceId::generate().toString()));
        std::filesystem::create_directory(root);
        testInstances(root);
        testProperties(root);
        testStructure(root);
        testConflicts(root);
        ursa::Scene docsSource;
        const auto docsRoot = take(docsSource.createNode());
        placePrefab(root, docsSource, docsRoot);
        std::filesystem::remove(root / "tile.ursaprefab");
        std::filesystem::remove(root / "level.ursascene");
        std::filesystem::remove(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
