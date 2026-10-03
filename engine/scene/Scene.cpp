#include <scene/Scene.hpp>
#include <algorithm>

namespace ursa {
NodeId Component::node() const {
    return {nodeIndex_, nodeGeneration_};
}

Scene::~Scene() {
    clear();
}

Result<NodeId> Scene::createNode(NodeId parent) {
    if (parent && !contains(parent))
        return Result<NodeId>::failure("Cannot create node under an invalid parent");

    uint32_t index;
    if (freeIndices_.empty()) {
        index = static_cast<uint32_t>(nodes_.size());
        nodes_.emplace_back();
    } else {
        index = freeIndices_.back();
        freeIndices_.pop_back();
    }
    NodeSlot &slot = nodes_[index];
    slot.persistentId = ResourceId::generate();
    slot.label.clear();
    slot.alive = true;
    slot.pendingCreate = inPhase_;
    slot.pendingDestroy = false;
    slot.parent = inPhase_ ? NodeId{} : parent;
    slot.pendingParent = parent;
    slot.hasPendingParent = inPhase_;
    slot.children.clear();
    slot.components.clear();
    slot.local = {};
    slot.enabled = true;
    slot.worldDirty = true;
    const NodeId result{index, slot.generation};
    if (inPhase_) {
        commands_.push_back([this, result, parent]() {
            if (parent && !contains(parent)) {
                destroySubtree(result);
                return Result<void>::failure("Cannot commit node creation: parent is no longer valid");
            }
            nodes_[result.index].pendingCreate = false;
            nodes_[result.index].hasPendingParent = false;
            nodes_[result.index].parent = parent;
            if (parent)
                nodes_[parent.index].children.push_back(result);
            else
                rootOrder_.push_back(result);
            return Result<void>{};
        });
    } else if (parent)
        nodes_[parent.index].children.push_back(result);
    else
        rootOrder_.push_back(result);
    return Result<NodeId>::success(result);
}

Result<String> Scene::nodeLabel(NodeId node) const {
    if (!contains(node))
        return Result<String>::failure("Cannot query an invalid node label");
    return Result<String>::success(nodes_[node.index].label);
}

Result<void> Scene::setNodeLabel(NodeId node, String label) {
    if (!contains(node))
        return Result<void>::failure("Cannot label an invalid node");
    if (label.size() > 256 || label.find('\0') != String::npos)
        return Result<void>::failure("Node label must be at most 256 bytes and contain no NUL");
    nodes_[node.index].label = std::move(label);
    return {};
}

Result<void> Scene::destroyNode(NodeId node) {
    if (!contains(node))
        return Result<void>::failure("Cannot destroy an invalid node");
    if (inPhase_) {
        markPendingDestroy(node);
        commands_.push_back([this, node]() {
            if (nodes_[node.index].alive && nodes_[node.index].generation == node.generation)
                destroyNodeNow(node);
            return Result<void>{};
        });
        return {};
    }
    destroyNodeNow(node);
    return {};
}

void Scene::destroyNodeNow(NodeId node) {
    markPendingDestroy(node);
    const NodeId parent = nodes_[node.index].parent;
    if (parent) {
        auto &siblings = nodes_[parent.index].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
    } else
        rootOrder_.erase(std::remove(rootOrder_.begin(), rootOrder_.end(), node), rootOrder_.end());
    destroySubtree(node);
}

Result<void> Scene::setParent(NodeId node, NodeId parent) {
    if (!contains(node))
        return Result<void>::failure("Cannot reparent an invalid node");
    if (parent && !contains(parent))
        return Result<void>::failure("Cannot reparent to an invalid parent");
    for (NodeId ancestor = parent; ancestor;) {
        if (ancestor == node)
            return Result<void>::failure("Cannot create a cycle in the scene hierarchy");
        const NodeSlot &slot = nodes_[ancestor.index];
        ancestor = slot.hasPendingParent ? slot.pendingParent : slot.parent;
    }

    if (inPhase_) {
        nodes_[node.index].pendingParent = parent;
        nodes_[node.index].hasPendingParent = true;
        commands_.push_back([this, node, parent]() {
            if (!contains(node) || (parent && !contains(parent)))
                return Result<void>::failure("Cannot commit reparenting: node or parent is no longer valid");
            return setParentNow(node, parent);
        });
        return {};
    }
    return setParentNow(node, parent);
}

Result<void> Scene::setParentNow(NodeId node, NodeId parent) {
    NodeSlot &slot = nodes_[node.index];
    slot.hasPendingParent = false;
    if (slot.parent == parent)
        return {};
    if (slot.parent) {
        auto &siblings = nodes_[slot.parent.index].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
    } else
        rootOrder_.erase(std::remove(rootOrder_.begin(), rootOrder_.end(), node), rootOrder_.end());
    slot.parent = parent;
    if (parent)
        nodes_[parent.index].children.push_back(node);
    else
        rootOrder_.push_back(node);
    markWorldDirty(node);
    return {};
}

bool Scene::contains(NodeId node) const {
    return node && node.index < nodes_.size() && nodes_[node.index].alive && !nodes_[node.index].pendingDestroy &&
           nodes_[node.index].generation == node.generation;
}

Result<NodeId> Scene::parentOf(NodeId node) const {
    if (!contains(node))
        return Result<NodeId>::failure("Cannot query the parent of an invalid node");
    return Result<NodeId>::success(nodes_[node.index].parent);
}

Result<Vector<NodeId>> Scene::childrenOf(NodeId node) const {
    if (!contains(node))
        return Result<Vector<NodeId>>::failure("Cannot query children of an invalid node");
    return Result<Vector<NodeId>>::success(nodes_[node.index].children);
}

Vector<NodeId> Scene::rootNodes() const {
    Vector<NodeId> roots;
    for (NodeId node : rootOrder_)
        if (contains(node) && !nodes_[node.index].pendingCreate)
            roots.push_back(node);
    return roots;
}

Result<ResourceId> Scene::nodePersistentId(NodeId node) const {
    if (!contains(node))
        return Result<ResourceId>::failure("Cannot query persistent ID of an invalid node");
    return Result<ResourceId>::success(nodes_[node.index].persistentId);
}

Result<void> Scene::assignNodePersistentId(NodeId node, ResourceId id) {
    if (!contains(node) || !id)
        return Result<void>::failure("Cannot assign an empty ID to an invalid node");
    for (uint32_t index = 0; index < nodes_.size(); ++index)
        if (index != node.index && nodes_[index].alive && !nodes_[index].pendingDestroy &&
            nodes_[index].persistentId == id)
            return Result<void>::failure("Duplicate persistent node ID " + id.toString());
    nodes_[node.index].persistentId = id;
    return {};
}

Result<NodeId> Scene::nodeByPersistentId(ResourceId id) const {
    for (uint32_t index = 0; index < nodes_.size(); ++index)
        if (nodes_[index].persistentId == id) {
            const NodeId node{index, nodes_[index].generation};
            if (contains(node))
                return Result<NodeId>::success(node);
        }
    return Result<NodeId>::failure("Missing persistent node ID " + id.toString());
}

Result<void> Scene::reorderNode(NodeId node, size_t index) {
    if (!contains(node))
        return Result<void>::failure("Cannot reorder an invalid node");
    if (inPhase_)
        return Result<void>::failure("Cannot reorder a node during an update phase");
    const NodeId parent = nodes_[node.index].parent;
    auto &siblings = parent ? nodes_[parent.index].children : rootOrder_;
    if (index >= siblings.size())
        return Result<void>::failure("Node order index is out of range");
    const auto found = std::find(siblings.begin(), siblings.end(), node);
    if (found == siblings.end())
        return Result<void>::failure("Node is missing from its sibling list");
    siblings.erase(found);
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), node);
    return {};
}

Result<void> Scene::setLocalTransform(NodeId node, LocalTransform transform) {
    if (!contains(node))
        return Result<void>::failure("Cannot set the transform of an invalid node");
    nodes_[node.index].local = transform;
    markWorldDirty(node);
    return {};
}

Result<LocalTransform> Scene::localTransform(NodeId node) const {
    if (!contains(node))
        return Result<LocalTransform>::failure("Cannot query the transform of an invalid node");
    return Result<LocalTransform>::success(nodes_[node.index].local);
}

Result<Mat4> Scene::worldTransform(NodeId node) const {
    if (!contains(node))
        return Result<Mat4>::failure("Cannot query the world transform of an invalid node");
    return Result<Mat4>::success(computeWorld(node));
}

Result<void> Scene::setEnabled(NodeId node, bool enabled) {
    if (!contains(node))
        return Result<void>::failure("Cannot enable or disable an invalid node");
    nodes_[node.index].enabled = enabled;
    return {};
}

Result<bool> Scene::isEnabled(NodeId node) const {
    if (!contains(node))
        return Result<bool>::failure("Cannot query the state of an invalid node");
    return Result<bool>::success(nodes_[node.index].enabled);
}

Result<bool> Scene::isActive(NodeId node) const {
    if (!contains(node))
        return Result<bool>::failure("Cannot query the active state of an invalid node");
    for (NodeId current = node; current; current = nodes_[current.index].parent)
        if (!nodes_[current.index].enabled)
            return Result<bool>::success(false);
    return Result<bool>::success(true);
}

Result<ComponentId> Scene::addComponentOwned(NodeId node, std::unique_ptr<Component> object) {
    if (!contains(node))
        return Result<ComponentId>::failure("Cannot attach a component to an invalid node");
    uint32_t index;
    if (freeComponentIndices_.empty()) {
        index = static_cast<uint32_t>(components_.size());
        components_.emplace_back();
    } else {
        index = freeComponentIndices_.back();
        freeComponentIndices_.pop_back();
    }
    ComponentSlot &slot = components_[index];
    const ComponentId id{index, slot.generation};
    slot.persistentId = ResourceId::generate();
    object->scene_ = this;
    object->nodeIndex_ = node.index;
    object->nodeGeneration_ = node.generation;
    object->id_ = id;
    slot.node = node;
    slot.object = std::move(object);
    slot.enabled = true;
    slot.started = false;
    slot.pendingAdd = inPhase_;
    slot.pendingRemoval = false;
    slot.availableFrame = frameSerial_ + (inPhase_ ? 1 : 0);
    if (inPhase_) {
        commands_.push_back([this, id, node]() {
            if (!contains(node)) {
                removeComponentNow(id);
                return Result<void>::failure("Cannot commit component creation: owner was destroyed");
            }
            components_[id.index].pendingAdd = false;
            nodes_[node.index].components.push_back(id);
            return Result<void>{};
        });
    } else {
        nodes_[node.index].components.push_back(id);
    }
    return Result<ComponentId>::success(id);
}

bool Scene::containsComponent(ComponentId id) const {
    return id && id.index < components_.size() && components_[id.index].object &&
           !components_[id.index].pendingRemoval && components_[id.index].generation == id.generation;
}

Result<void> Scene::removeComponent(ComponentId id) {
    if (!containsComponent(id))
        return Result<void>::failure("Cannot remove an invalid component");
    if (inPhase_) {
        components_[id.index].pendingRemoval = true;
        commands_.push_back([this, id]() {
            if (id.index < components_.size() && components_[id.index].object &&
                components_[id.index].generation == id.generation)
                removeComponentNow(id);
            return Result<void>{};
        });
        return {};
    }
    removeComponentNow(id);
    return {};
}

void Scene::removeComponentNow(ComponentId id) {
    ComponentSlot &slot = components_[id.index];
    slot.pendingRemoval = true;
    if (contains(slot.node)) {
        auto &list = nodes_[slot.node.index].components;
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
    }
    std::unique_ptr<Component> object = std::move(slot.object);
    slot.node = {};
    slot.pendingAdd = false;
    ++slot.generation;
    if (slot.generation == 0)
        slot.generation = 1;
    freeComponentIndices_.push_back(id.index);
    object->onRemove();
}

Result<void> Scene::setComponentEnabled(ComponentId id, bool enabled) {
    if (!containsComponent(id))
        return Result<void>::failure("Cannot enable or disable an invalid component");
    components_[id.index].enabled = enabled;
    return {};
}

Result<bool> Scene::isComponentActive(ComponentId id) const {
    if (!containsComponent(id))
        return Result<bool>::failure("Cannot query the state of an invalid component");
    const ComponentSlot &slot = components_[id.index];
    return Result<bool>::success(slot.enabled && isActive(slot.node).value);
}

Result<bool> Scene::isComponentEnabled(ComponentId id) const {
    if (!containsComponent(id))
        return Result<bool>::failure("Cannot query the enabled state of an invalid component");
    return Result<bool>::success(components_[id.index].enabled);
}

Result<NodeId> Scene::componentNode(ComponentId id) const {
    if (!containsComponent(id))
        return Result<NodeId>::failure("Cannot query the owner of an invalid component");
    return Result<NodeId>::success(components_[id.index].node);
}

Result<Vector<ComponentId>> Scene::componentsOf(NodeId node) const {
    if (!contains(node))
        return Result<Vector<ComponentId>>::failure("Cannot query components of an invalid node");
    Vector<ComponentId> result;
    for (ComponentId id : nodes_[node.index].components)
        if (containsComponent(id))
            result.push_back(id);
    return Result<Vector<ComponentId>>::success(std::move(result));
}

Component *Scene::component(ComponentId id) const {
    return containsComponent(id) ? components_[id.index].object.get() : nullptr;
}

Result<ResourceId> Scene::componentPersistentId(ComponentId id) const {
    if (!containsComponent(id))
        return Result<ResourceId>::failure("Cannot query persistent ID of an invalid component");
    return Result<ResourceId>::success(components_[id.index].persistentId);
}

Result<void> Scene::assignComponentPersistentId(ComponentId component, ResourceId id) {
    if (!containsComponent(component) || !id)
        return Result<void>::failure("Cannot assign an empty ID to an invalid component");
    for (uint32_t index = 0; index < components_.size(); ++index)
        if (index != component.index && components_[index].object && !components_[index].pendingRemoval &&
            components_[index].persistentId == id)
            return Result<void>::failure("Duplicate persistent component ID " + id.toString());
    components_[component.index].persistentId = id;
    return {};
}

Result<ComponentId> Scene::componentByPersistentId(ResourceId id) const {
    for (uint32_t index = 0; index < components_.size(); ++index)
        if (components_[index].persistentId == id) {
            const ComponentId component{index, components_[index].generation};
            if (containsComponent(component))
                return Result<ComponentId>::success(component);
        }
    return Result<ComponentId>::failure("Missing persistent component ID " + id.toString());
}

Result<void> Scene::setComponentNode(ComponentId component, NodeId node) {
    if (!containsComponent(component) || !contains(node))
        return Result<void>::failure("Cannot move an invalid component or target node");
    if (inPhase_)
        return Result<void>::failure("Cannot move a component during an update phase");
    ComponentSlot &slot = components_[component.index];
    if (slot.node == node)
        return {};
    auto &previous = nodes_[slot.node.index].components;
    previous.erase(std::remove(previous.begin(), previous.end(), component), previous.end());
    nodes_[node.index].components.push_back(component);
    slot.node = node;
    slot.object->nodeIndex_ = node.index;
    slot.object->nodeGeneration_ = node.generation;
    return {};
}

Result<void> Scene::reorderComponent(ComponentId component, size_t index) {
    if (!containsComponent(component))
        return Result<void>::failure("Cannot reorder an invalid component");
    if (inPhase_)
        return Result<void>::failure("Cannot reorder a component during an update phase");
    auto &siblings = nodes_[components_[component.index].node.index].components;
    if (index >= siblings.size())
        return Result<void>::failure("Component order index is out of range");
    const auto found = std::find(siblings.begin(), siblings.end(), component);
    if (found == siblings.end())
        return Result<void>::failure("Component is missing from its node");
    siblings.erase(found);
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), component);
    return {};
}

void Scene::beginFrame() {
    ++frameSerial_;
}

Result<void> Scene::update(const FrameContext &context) {
    return runPhase(Phase::Update, context);
}

Result<void> Scene::fixedUpdate(const FrameContext &context) {
    return runPhase(Phase::Fixed, context);
}

Result<void> Scene::lateUpdate(const FrameContext &context) {
    return runPhase(Phase::Late, context);
}

Result<void> Scene::runPhase(Phase phase, const FrameContext &context) {
    Vector<ComponentId> order;
    for (NodeId root : rootNodes())
        collectComponents(root, order);
    inPhase_ = true;
    Result<void> phaseResult;
    for (ComponentId id : order) {
        if (!containsComponent(id))
            continue;
        ComponentSlot &slot = components_[id.index];
        if (slot.availableFrame > frameSerial_ || !isComponentActive(id).value)
            continue;
        if (!slot.started) {
            slot.started = true;
            auto started = slot.object->onStart(context);
            if (!started) {
                phaseResult = Result<void>::failure("Component start failed: " + started.error);
                break;
            }
            if (!containsComponent(id))
                continue;
        }
        Component *object = components_[id.index].object.get();
        Result<void> result;
        switch (phase) {
        case Phase::Update:
            result = object->onUpdate(context);
            break;
        case Phase::Fixed:
            result = object->onFixedUpdate(context);
            break;
        case Phase::Late:
            result = object->onLateUpdate(context);
            break;
        }
        if (!result) {
            phaseResult = Result<void>::failure("Component callback failed: " + result.error);
            break;
        }
    }
    inPhase_ = false;
    auto committed = commitChanges();
    if (!phaseResult)
        return phaseResult;
    return committed;
}

Result<void> Scene::commitChanges() {
    if (inPhase_)
        return Result<void>::failure("Cannot commit scene changes during an update phase");
    auto commands = std::move(commands_);
    commands_.clear();
    Result<void> result;
    for (auto &command : commands) {
        auto committed = command();
        if (!committed && result)
            result = committed;
    }
    return result;
}

void Scene::collectComponents(NodeId node, Vector<ComponentId> &out) const {
    const NodeSlot &slot = nodes_[node.index];
    out.insert(out.end(), slot.components.begin(), slot.components.end());
    for (NodeId child : slot.children)
        collectComponents(child, out);
}

void Scene::clear() {
    commands_.clear();
    for (uint32_t index = 0; index < nodes_.size(); ++index)
        if (nodes_[index].alive && !nodes_[index].parent)
            destroySubtree({index, nodes_[index].generation});
    for (uint32_t index = 0; index < components_.size(); ++index)
        if (components_[index].object)
            removeComponentNow({index, components_[index].generation});
    rootOrder_.clear();
}

void Scene::destroySubtree(NodeId node) {
    const auto children = nodes_[node.index].children;
    for (NodeId child : children)
        destroySubtree(child);
    const auto components = nodes_[node.index].components;
    for (ComponentId id : components)
        if (id.index < components_.size() && components_[id.index].object &&
            components_[id.index].generation == id.generation)
            removeComponentNow(id);
    NodeSlot &slot = nodes_[node.index];
    slot.children.clear();
    slot.parent = {};
    slot.alive = false;
    slot.pendingCreate = false;
    slot.pendingDestroy = false;
    slot.hasPendingParent = false;
    ++slot.generation;
    if (slot.generation == 0)
        slot.generation = 1;
    freeIndices_.push_back(node.index);
}

void Scene::markPendingDestroy(NodeId node) {
    NodeSlot &slot = nodes_[node.index];
    slot.pendingDestroy = true;
    for (ComponentId id : slot.components)
        if (containsComponent(id))
            components_[id.index].pendingRemoval = true;
    for (NodeId child : slot.children)
        markPendingDestroy(child);
}

void Scene::markWorldDirty(NodeId node) {
    NodeSlot &slot = nodes_[node.index];
    slot.worldDirty = true;
    for (NodeId child : slot.children)
        markWorldDirty(child);
}

Mat4 Scene::computeWorld(NodeId node) const {
    const NodeSlot &slot = nodes_[node.index];
    if (slot.worldDirty) {
        slot.world = slot.parent ? computeWorld(slot.parent) * slot.local.matrix() : slot.local.matrix();
        slot.worldDirty = false;
    }
    return slot.world;
}
} // namespace ursa
