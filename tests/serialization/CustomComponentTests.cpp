#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <serialization/SceneResource.hpp>
#include <fstream>
#include <iostream>
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

struct RuntimeService {
    int updates = 0;
};

struct CounterComponent : ursa::Component {
    CounterComponent(int &total, int step) : total(total), step(step) {
    }

    int &total;
    int step;
};

ursa::Result<void> registerCounter(ursa::ComponentRegistry &registry) {
    ursa::ComponentCodec codec;
    codec.type = "game.Counter";
    codec.version = 1;
    codec.matches = [](const ursa::Component &value) {
        return dynamic_cast<const CounterComponent *>(&value) != nullptr;
    };
    codec.save = [](const ursa::Component &value, const ursa::ComponentSaveContext &) {
        return ursa::Result<nlohmann::json>::success({{"step", static_cast<const CounterComponent &>(value).step}});
    };
    codec.create = [](ursa::Scene &scene, ursa::NodeId node, const nlohmann::json &data,
                      ursa::ComponentLoadContext &context) {
        if (!data.is_object() || !data.contains("step") || !data["step"].is_number_integer())
            return ursa::Result<ursa::ComponentId>::failure("Invalid Counter step");
        auto total = context.service<int>();
        if (!total)
            return ursa::Result<ursa::ComponentId>::failure(total.error);
        return scene.addComponent<CounterComponent>(node, *total.value, data["step"].get<int>());
    };
    return registry.registerCodec(std::move(codec));
}

class LinkComponent final : public ursa::Component {
public:
    LinkComponent(RuntimeService &service, int value) : service_(service), value(value) {
    }

    ursa::Result<void> onUpdate(const ursa::FrameContext &) override {
        ++service_.updates;
        return {};
    }

    ursa::NodeId targetNode;
    ursa::ComponentId targetComponent;
    int value = 0;

private:
    RuntimeService &service_;
};

ursa::ComponentCodec makeCodec() {
    ursa::ComponentCodec codec;
    codec.type = "test.Link";
    codec.version = 2;
    codec.matches = [](const ursa::Component &component) {
        return dynamic_cast<const LinkComponent *>(&component) != nullptr;
    };
    codec.save = [](const ursa::Component &component, const ursa::ComponentSaveContext &context) {
        const auto &link = static_cast<const LinkComponent &>(component);
        nlohmann::json data{{"value", link.value}, {"node", nullptr}, {"component", nullptr}};
        if (link.targetNode)
            data["node"] = ursa::toStdString(take(context.nodeId(link.targetNode)).toString());
        if (link.targetComponent)
            data["component"] = ursa::toStdString(take(context.componentId(link.targetComponent)).toString());
        return ursa::Result<nlohmann::json>::success(std::move(data));
    };
    codec.create = [](ursa::Scene &scene, ursa::NodeId node, const nlohmann::json &data,
                      ursa::ComponentLoadContext &context) {
        if (!data.is_object() || !data.contains("value") || !data["value"].is_number_integer())
            return ursa::Result<ursa::ComponentId>::failure("Invalid link value");
        auto service = context.service<RuntimeService>();
        if (!service)
            return ursa::Result<ursa::ComponentId>::failure(service.error);
        return scene.addComponent<LinkComponent>(node, *service.value, data["value"].get<int>());
    };
    codec.bind = [](ursa::Scene &scene, ursa::ComponentId component, const nlohmann::json &data,
                    ursa::ComponentLoadContext &context) -> ursa::Result<void> {
        auto &link = *static_cast<LinkComponent *>(scene.component(component));
        for (const char *field : {"node", "component"}) {
            if (!data.contains(field) || (!data[field].is_null() && !data[field].is_string()))
                return ursa::Result<void>::failure(ursa::String("Invalid link ") + field);
            if (data[field].is_null())
                continue;
            auto id = ursa::ResourceId::parse(ursa::toUrsaString(data[field].get<std::string>()));
            if (!id)
                return ursa::Result<void>::failure(id.error);
            if (ursa::String(field) == "node") {
                auto target = context.node(id.value);
                if (!target)
                    return ursa::Result<void>::failure(target.error);
                link.targetNode = target.value;
            } else {
                auto target = context.component(id.value);
                if (!target)
                    return ursa::Result<void>::failure(target.error);
                link.targetComponent = target.value;
            }
        }
        return {};
    };
    return codec;
}

void testCustomComponents(const std::filesystem::path &root) {
    RuntimeService service;
    ursa::ComponentRegistry registry;
    done(registry.registerCodec(makeCodec()));
    done(registerCounter(registry));
    auto duplicate = registry.registerCodec(makeCodec());
    expect(!duplicate && duplicate.error.find("Duplicate") != ursa::String::npos,
           "Duplicate component type was accepted");
    ursa::Scene source;
    const auto first = take(source.createNode());
    const auto second = take(source.createNode());
    const auto target = take(source.addComponent<LinkComponent>(second, service, 7));
    const auto linkId = take(source.addComponent<LinkComponent>(first, service, 42));
    auto *link = static_cast<LinkComponent *>(source.component(linkId));
    link->targetNode = second;
    link->targetComponent = target;
    const auto firstPersistent = take(source.nodePersistentId(first));
    const auto secondPersistent = take(source.nodePersistentId(second));
    const auto linkPersistent = take(source.componentPersistentId(linkId));
    const auto targetPersistent = take(source.componentPersistentId(target));
    const auto path = root / "custom.ursascene";
    done(ursa::saveSceneResource(path, ursa::ResourceId::generate(), source, registry));

    auto session = take(ursa::ResourceSession::open({root}));
    ursa::ComponentLoadContext context(*session);
    context.provide(service);
    auto loaded = take(ursa::loadSceneResource(path, registry, context));
    auto restoredNode = take(loaded.scene->nodeByPersistentId(firstPersistent));
    auto restoredTargetNode = take(loaded.scene->nodeByPersistentId(secondPersistent));
    auto *restored = static_cast<LinkComponent *>(
        loaded.scene->component(take(loaded.scene->componentByPersistentId(linkPersistent))));
    expect(restored && restored != link && restored->value == 42 && restored->node() == restoredNode &&
               restored->targetNode == restoredTargetNode &&
               restored->targetComponent == take(loaded.scene->componentByPersistentId(targetPersistent)),
           "Custom fields or cross-object references were not restored");
    loaded.scene->beginFrame();
    done(loaded.scene->update({}));
    expect(service.updates == 2, "Runtime service was not rebound to loaded components");

    ursa::ComponentRegistry emptyRegistry;
    ursa::ComponentLoadContext missingTypeContext(*session);
    auto missingType = ursa::loadSceneResource(path, emptyRegistry, missingTypeContext);
    expect(!missingType && missingType.error.find(ursa::pathToUrsaString(path)) != ursa::String::npos &&
               missingType.error.find(firstPersistent.toString()) != ursa::String::npos &&
               missingType.error.find("test.Link") != ursa::String::npos,
           "Unregistered type error lacks file, node, or type");
    ursa::ComponentLoadContext missingServiceContext(*session);
    auto missingService = ursa::loadSceneResource(path, registry, missingServiceContext);
    expect(!missingService && missingService.error.find("Missing runtime service") != ursa::String::npos,
           "Missing runtime service was accepted");
    nlohmann::json document;
    {
        std::ifstream input(path);
        input >> document;
    }
    expect(document.dump().find("service_") == ursa::String::npos &&
               document.dump().find(ursa::toStdString(secondPersistent.toString())) != ursa::String::npos,
           "Scene stored a process dependency or omitted persistent references");
    document["nodes"][0]["components"][0]["version"] = 99;
    const auto unsupported = root / "unsupported.ursascene";
    {
        std::ofstream output(unsupported);
        output << document;
    }
    ursa::ComponentLoadContext versionContext(*session);
    versionContext.provide(service);
    auto badVersion = ursa::loadSceneResource(unsupported, registry, versionContext);
    expect(!badVersion && badVersion.error.find(ursa::pathToUrsaString(unsupported)) != ursa::String::npos &&
               badVersion.error.find(firstPersistent.toString()) != ursa::String::npos &&
               badVersion.error.find("version") != ursa::String::npos,
           "Unsupported version error lacks file or node");
    std::filesystem::remove(path);
    std::filesystem::remove(unsupported);
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          ("UrsaCustomComponentTests-" + ursa::toStdString(ursa::ResourceId::generate().toString()));
        std::filesystem::create_directory(root);
        testCustomComponents(root);
        std::filesystem::remove(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
