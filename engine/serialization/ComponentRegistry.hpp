#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <scene/Scene.hpp>
#include <serialization/ResourceSession.hpp>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <typeindex>

namespace ursa {
using MaterialIdResolver = std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance> &)>;

struct ComponentSaveContext {
    const Scene &scene;
    MaterialIdResolver materialId;

    Result<ResourceId> nodeId(NodeId node) const {
        return scene.nodePersistentId(node);
    }

    Result<ResourceId> componentId(ComponentId component) const {
        return scene.componentPersistentId(component);
    }
};

class ComponentLoadContext final {
public:
    explicit ComponentLoadContext(ResourceSession &resources) : resources_(resources) {
    }

    ResourceSession &resources() const {
        return resources_;
    }

    template <class T> void provide(T &service) {
        services_[std::type_index(typeid(T))] = &service;
    }

    template <class T> Result<T *> service() const {
        auto found = services_.find(std::type_index(typeid(T)));
        if (found == services_.end())
            return Result<T *>::failure("Missing runtime service " + String(typeid(T).name()));
        return Result<T *>::success(static_cast<T *>(found->second));
    }

    Result<NodeId> node(ResourceId id) const;

    Result<ComponentId> component(ResourceId id) const;

    Result<void> mapNode(ResourceId id, NodeId node);

    Result<void> mapComponent(ResourceId id, ComponentId component);

    void clearMappings();

private:
    ResourceSession &resources_;
    Map<std::type_index, void *> services_;
    Map<String, NodeId> nodes_;
    Map<String, ComponentId> components_;
};

struct ComponentCodec {
    String type;
    uint32_t version = 1;
    std::function<bool(const Component &)> matches;
    std::function<Result<nlohmann::json>(const Component &, const ComponentSaveContext &)> save;
    std::function<Result<ComponentId>(Scene &, NodeId, const nlohmann::json &, ComponentLoadContext &)> create;
    std::function<Result<void>(Scene &, ComponentId, const nlohmann::json &, ComponentLoadContext &)> bind;
};

class ComponentRegistry final {
public:
    Result<void> registerCodec(ComponentCodec codec);

    Result<const ComponentCodec *> findFor(const Component &component) const;

    const ComponentCodec *find(const String &type) const;

private:
    Map<String, ComponentCodec> codecs_;
};

Result<void> registerBuiltinComponents(ComponentRegistry &registry);
} // namespace ursa
