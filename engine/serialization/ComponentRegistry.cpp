#include <core/StringInterop.hpp>
#include <serialization/ComponentRegistry.hpp>
#include <utility>

namespace ursa {
Result<NodeId> ComponentLoadContext::node(ResourceId id) const {
    auto found = nodes_.find(id.toString());
    return found == nodes_.end() ? Result<NodeId>::failure("Missing persistent node ID " + id.toString())
                                 : Result<NodeId>::success(found->second);
}

Result<ComponentId> ComponentLoadContext::component(ResourceId id) const {
    auto found = components_.find(id.toString());
    return found == components_.end() ? Result<ComponentId>::failure("Missing persistent component ID " + id.toString())
                                      : Result<ComponentId>::success(found->second);
}

Result<void> ComponentLoadContext::mapNode(ResourceId id, NodeId node) {
    if (!id || !node || !nodes_.emplace(id.toString(), node).second)
        return Result<void>::failure("Duplicate or invalid persistent node ID " + id.toString());
    return {};
}

Result<void> ComponentLoadContext::mapComponent(ResourceId id, ComponentId component) {
    if (!id || !component || !components_.emplace(id.toString(), component).second)
        return Result<void>::failure("Duplicate or invalid persistent component ID " + id.toString());
    return {};
}

void ComponentLoadContext::clearMappings() {
    nodes_.clear();
    components_.clear();
}

Result<void> ComponentRegistry::registerCodec(ComponentCodec codec) {
    if (codec.type.empty() || !codec.version || !codec.matches || !codec.save || !codec.create)
        return Result<void>::failure("Component codec needs a type, version, matcher, saver and factory");
    const auto name = codec.type;
    if (!codecs_.emplace(name, std::move(codec)).second)
        return Result<void>::failure("Duplicate component codec type " + name);
    return {};
}

Result<const ComponentCodec *> ComponentRegistry::findFor(const Component &component) const {
    const ComponentCodec *selected = nullptr;
    for (const auto &entry : codecs_)
        if (entry.second.matches(component)) {
            if (selected)
                return Result<const ComponentCodec *>::failure("Multiple component codecs match one component");
            selected = &entry.second;
        }
    if (!selected)
        return Result<const ComponentCodec *>::failure("No registered codec for component type " +
                                                       String(typeid(component).name()));
    return Result<const ComponentCodec *>::success(selected);
}

const ComponentCodec *ComponentRegistry::find(const String &type) const {
    const auto found = codecs_.find(type);
    return found == codecs_.end() ? nullptr : &found->second;
}
} // namespace ursa
