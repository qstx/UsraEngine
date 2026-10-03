#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <serialization/SceneResource.hpp>
#include <serialization/MaterialResource.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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

void roundTripScene(const std::filesystem::path &path, ursa::ResourceSession &resources) {
    ursa::ComponentRegistry registry;
    if (!ursa::registerBuiltinComponents(registry))
        return;
    ursa::Scene source;
    auto node = source.createNode();
    if (!node || !source.addComponent<ursa::CameraComponent>(node.value))
        return;
    if (!ursa::saveSceneResource(path, ursa::ResourceId::generate(), source, registry))
        return;
    ursa::ComponentLoadContext context(resources);
    auto loaded = ursa::loadSceneResource(path, registry, context);
    if (!loaded)
        return;
    std::unique_ptr<ursa::Scene> active = std::move(loaded.value.scene);
    auto reloaded = ursa::reloadSceneResource(path, registry, context, active);
    if (!reloaded)
        throw std::runtime_error(ursa::toStdString(reloaded.error));
}

void testScene(const std::filesystem::path &root) {
    ursa::ComponentRegistry registry;
    done(ursa::registerBuiltinComponents(registry));
    ursa::Scene scene;
    const auto first = take(scene.createNode());
    const auto child = take(scene.createNode(first));
    const auto second = take(scene.createNode());
    done(scene.reorderNode(second, 0));
    ursa::LocalTransform transform;
    transform.position = {2.0f, 3.0f, 4.0f};
    transform.scale = {1.2f, 0.8f, 2.0f};
    done(scene.setLocalTransform(child, transform));
    done(scene.setEnabled(child, false));
    done(scene.setAmbientLight({0.3f, 0.4f, 0.5f, 1.0f}, 0.25f));
    const auto camera = take(scene.addComponent<ursa::CameraComponent>(first));
    auto *cameraObject = static_cast<ursa::CameraComponent *>(scene.component(camera));
    cameraObject->verticalFovRadians = 0.9f;
    cameraObject->nearPlane = 0.2f;
    cameraObject->farPlane = 80.0f;
    done(scene.setActiveCamera(camera));
    const auto box = take(scene.addComponent<ursa::BoxRenderComponent>(child, ursa::Color{0.9f, 0.1f, 0.2f, 1.0f}));
    done(scene.setComponentEnabled(box, false));
    const auto sun = take(scene.addComponent<ursa::DirectionalLightComponent>(second));
    auto *sunObject = static_cast<ursa::DirectionalLightComponent *>(scene.component(sun));
    done(sunObject->setColor({0.8f, 0.7f, 0.6f, 1.0f}));
    done(sunObject->setIntensity(2.0f));
    const auto lamp = take(scene.addComponent<ursa::PointLightComponent>(second));
    auto *lampObject = static_cast<ursa::PointLightComponent *>(scene.component(lamp));
    done(lampObject->setRange(7.0f));
    take(scene.addComponent<ursa::PointLightComponent>(second));
    auto material = std::make_shared<ursa::MaterialInstance>(ursa::MaterialType::Unlit);
    done(material->setBaseColor({0.2f, 0.5f, 0.8f, 1.0f}));
    const auto materialId = ursa::ResourceId::generate();
    const auto materialPath = root / "box.ursamat";
    done(ursa::saveMaterialResource(materialPath, materialId, *material, {}));
    const auto visibleBox = take(scene.addComponent<ursa::BoxRenderComponent>(first));
    static_cast<ursa::BoxRenderComponent *>(scene.component(visibleBox))->material = material;
    const auto firstId = take(scene.nodePersistentId(first));
    const auto childId = take(scene.nodePersistentId(child));
    const auto secondId = take(scene.nodePersistentId(second));
    const auto cameraId = take(scene.componentPersistentId(camera));
    const auto sunId = take(scene.componentPersistentId(sun));
    const auto sceneId = ursa::ResourceId::generate();
    const auto path = root / "sample.ursascene";
    done(ursa::saveSceneResource(path, sceneId, scene, registry,
                                 [material, materialId](const std::shared_ptr<ursa::MaterialInstance> &selected) {
                                     return selected == material
                                                ? ursa::Result<ursa::ResourceId>::success(materialId)
                                                : ursa::Result<ursa::ResourceId>::failure("Unknown material");
                                 }));
    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext context(*session);
    auto loaded = take(ursa::loadSceneResource(path, registry, context));
    expect(loaded.id == sceneId && loaded.scene->rootNodes().size() == 2 &&
               take(loaded.scene->nodePersistentId(loaded.scene->rootNodes()[0])) == secondId,
           "Scene root identity or order was lost");
    const auto restoredFirst = take(loaded.scene->nodeByPersistentId(firstId));
    const auto restoredChild = take(loaded.scene->nodeByPersistentId(childId));
    expect(take(loaded.scene->parentOf(restoredChild)) == restoredFirst &&
               take(loaded.scene->localTransform(restoredChild)).position.x == 2.0f &&
               !take(loaded.scene->isEnabled(restoredChild)),
           "Scene hierarchy, transform, or enabled state was lost");
    expect(take(loaded.scene->componentPersistentId(loaded.scene->activeCamera())) == cameraId &&
               loaded.scene->ambientLightColor().green == 0.4f && loaded.scene->ambientLightIntensity() == 0.25f &&
               !take(loaded.scene->isComponentEnabled(take(loaded.scene->componentsOf(restoredChild))[0])),
           "Scene camera, ambient light, or component enabled state was lost");
    expect(take(loaded.scene->componentsOf(take(loaded.scene->nodeByPersistentId(secondId)))).size() == 3 &&
               loaded.scene->component(take(loaded.scene->componentByPersistentId(sunId))) != nullptr,
           "Scene components were not restored");
    const auto restoredCamera = loaded.scene->findComponent<ursa::CameraComponent>(restoredFirst);
    const auto frame = take(loaded.scene->collectRenderData(1.0f));
    expect(restoredCamera && restoredCamera->verticalFovRadians == 0.9f && restoredCamera->farPlane == 80.0f &&
               frame.boxes.size() == 1 && frame.boxes[0].material &&
               frame.boxes[0].material->parameters().baseColor.blue == 0.8f && frame.lighting.directional.size() == 1 &&
               frame.lighting.directional[0].intensity == 2.0f && frame.lighting.point.size() == 2 &&
               frame.lighting.point[0].range == 7.0f,
           "Built-in component fields or render snapshot changed after load");

    nlohmann::json document;
    {
        std::ifstream input(path);
        input >> document;
    }
    const auto originalId = document["nodes"][1]["id"];
    document["nodes"][1]["id"] = document["nodes"][0]["id"];
    const auto duplicate = root / "duplicate.ursascene";
    {
        std::ofstream output(duplicate);
        output << document;
    }
    ursa::ComponentLoadContext duplicateContext(*session);
    auto repeated = ursa::loadSceneResource(duplicate, registry, duplicateContext);
    expect(!repeated && repeated.error.find("Duplicate node ID") != ursa::String::npos,
           "Duplicate node ID was accepted");
    document["nodes"][1]["id"] = originalId;
    document["nodes"][1]["parent"] = ursa::toStdString(ursa::ResourceId::generate().toString());
    const auto missing = root / "missing.ursascene";
    {
        std::ofstream output(missing);
        output << document;
    }
    ursa::ComponentLoadContext missingContext(*session);
    auto dangling = ursa::loadSceneResource(missing, registry, missingContext);
    expect(!dangling && dangling.error.find("Missing parent") != ursa::String::npos, "Missing parent ID was accepted");
    std::filesystem::remove(path);
    std::filesystem::remove(duplicate);
    std::filesystem::remove(missing);
    std::filesystem::remove(materialPath);
}

struct ProbeCounters {
    int starts = 0;
    int removals = 0;
};

class ProbeComponent final : public ursa::Component {
public:
    explicit ProbeComponent(ProbeCounters &counters) : counters_(counters) {
    }

    ursa::Result<void> onStart(const ursa::FrameContext &) override {
        ++counters_.starts;
        return {};
    }

    void onRemove() override {
        ++counters_.removals;
    }

private:
    ProbeCounters &counters_;
};

void testReload(const std::filesystem::path &root) {
    ProbeCounters counters;
    ursa::ComponentRegistry registry;
    ursa::ComponentCodec codec;
    codec.type = "test.Probe";
    codec.matches = [](const ursa::Component &component) {
        return dynamic_cast<const ProbeComponent *>(&component) != nullptr;
    };
    codec.save = [](const ursa::Component &, const ursa::ComponentSaveContext &) {
        return ursa::Result<nlohmann::json>::success(nlohmann::json::object());
    };
    codec.create = [](ursa::Scene &scene, ursa::NodeId node, const nlohmann::json &,
                      ursa::ComponentLoadContext &context) {
        auto service = context.service<ProbeCounters>();
        if (!service)
            return ursa::Result<ursa::ComponentId>::failure(service.error);
        return scene.addComponent<ProbeComponent>(node, *service.value);
    };
    done(registry.registerCodec(std::move(codec)));
    auto active = std::make_unique<ursa::Scene>();
    const auto node = take(active->createNode());
    take(active->addComponent<ProbeComponent>(node, counters));
    const auto path = root / "reload.ursascene";
    done(ursa::saveSceneResource(path, ursa::ResourceId::generate(), *active, registry));
    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext context(*session);
    context.provide(counters);
    auto *original = active.get();
    const auto invalid = root / "invalid.ursascene";
    {
        std::ofstream output(invalid);
        output << "{broken";
    }
    const auto failed = ursa::reloadSceneResource(invalid, registry, context, active);
    expect(!failed && active.get() == original && counters.removals == 0, "Failed reload discarded the active scene");
    done(ursa::reloadSceneResource(path, registry, context, active));
    expect(active.get() != original && counters.removals == 1 && counters.starts == 0,
           "Successful reload did not replace the scene or started components early");
    active->beginFrame();
    done(active->update({}));
    expect(counters.starts == 1, "Reloaded component did not start in the normal frame");
    active.reset();
    std::filesystem::remove(path);
    std::filesystem::remove(invalid);
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          ("UrsaScenePersistenceTests-" + ursa::toStdString(ursa::ResourceId::generate().toString()));
        std::filesystem::create_directory(root);
        testScene(root);
        testReload(root);
        auto session = take(ursa::ResourceSession::open({root}));
        roundTripScene(root / "docs.ursascene", *session);
        std::filesystem::remove(root / "docs.ursascene");
        std::filesystem::remove(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
