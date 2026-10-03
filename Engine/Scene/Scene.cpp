#include <Scene/Scene.hpp>
#include <algorithm>

namespace Ursa
{
NodeId Component::Node() const
{
	return {nodeIndex, nodeGeneration};
}

Scene::~Scene()
{
	Clear();
}

Result<NodeId> Scene::CreateNode(NodeId parent)
{
	if (parent && !Contains(parent))
	{
		return Result<NodeId>::Failure("Cannot create node under an invalid parent");
	}

	uint32_t index;
	if (freeIndices.empty())
	{
		index = static_cast<uint32_t>(nodes.size());
		nodes.emplace_back();
	}
	else
	{
		index = freeIndices.back();
		freeIndices.pop_back();
	}
	NodeSlot& slot = nodes[index];
	slot.persistentId = ResourceId::Generate();
	slot.label.clear();
	slot.alive = true;
	slot.pendingCreate = inPhase;
	slot.pendingDestroy = false;
	slot.parent = inPhase ? NodeId{} : parent;
	slot.pendingParent = parent;
	slot.hasPendingParent = inPhase;
	slot.children.clear();
	slot.components.clear();
	slot.local = {};
	slot.enabled = true;
	slot.worldDirty = true;
	const NodeId result{index, slot.generation};
	if (inPhase)
	{
		commands.push_back(
		    [this, result, parent]()
		    {
			    if (parent && !Contains(parent))
			    {
				    DestroySubtree(result);
				    return Result<void>::Failure("Cannot commit node creation: parent is no longer valid");
			    }
			    nodes[result.index].pendingCreate = false;
			    nodes[result.index].hasPendingParent = false;
			    nodes[result.index].parent = parent;
			    if (parent)
			    {
				    nodes[parent.index].children.push_back(result);
			    }
			    else
			    {
				    rootOrder.push_back(result);
			    }
			    return Result<void>{};
		    });
	}
	else if (parent)
	{
		nodes[parent.index].children.push_back(result);
	}
	else
	{
		rootOrder.push_back(result);
	}
	return Result<NodeId>::Success(result);
}

Result<String> Scene::NodeLabel(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<String>::Failure("Cannot query an invalid node label");
	}
	return Result<String>::Success(nodes[node.index].label);
}

Result<void> Scene::SetNodeLabel(NodeId node, String label)
{
	if (!Contains(node))
	{
		return Result<void>::Failure("Cannot label an invalid node");
	}
	if (label.size() > 256 || label.find('\0') != String::npos)
	{
		return Result<void>::Failure("Node label must be at most 256 bytes and contain no NUL");
	}
	nodes[node.index].label = std::move(label);
	return {};
}

Result<void> Scene::DestroyNode(NodeId node)
{
	if (!Contains(node))
	{
		return Result<void>::Failure("Cannot destroy an invalid node");
	}
	if (inPhase)
	{
		MarkPendingDestroy(node);
		commands.push_back(
		    [this, node]()
		    {
			    if (nodes[node.index].alive && nodes[node.index].generation == node.generation)
			    {
				    DestroyNodeNow(node);
			    }
			    return Result<void>{};
		    });
		return {};
	}
	DestroyNodeNow(node);
	return {};
}

void Scene::DestroyNodeNow(NodeId node)
{
	MarkPendingDestroy(node);
	const NodeId parent = nodes[node.index].parent;
	if (parent)
	{
		auto& siblings = nodes[parent.index].children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
	}
	else
	{
		rootOrder.erase(std::remove(rootOrder.begin(), rootOrder.end(), node), rootOrder.end());
	}
	DestroySubtree(node);
}

Result<void> Scene::SetParent(NodeId node, NodeId parent)
{
	if (!Contains(node))
	{
		return Result<void>::Failure("Cannot reparent an invalid node");
	}
	if (parent && !Contains(parent))
	{
		return Result<void>::Failure("Cannot reparent to an invalid parent");
	}
	for (NodeId ancestor = parent; ancestor;)
	{
		if (ancestor == node)
		{
			return Result<void>::Failure("Cannot create a cycle in the scene hierarchy");
		}
		const NodeSlot& slot = nodes[ancestor.index];
		ancestor = slot.hasPendingParent ? slot.pendingParent : slot.parent;
	}

	if (inPhase)
	{
		nodes[node.index].pendingParent = parent;
		nodes[node.index].hasPendingParent = true;
		commands.push_back(
		    [this, node, parent]()
		    {
			    if (!Contains(node) || (parent && !Contains(parent)))
			    {
				    return Result<void>::Failure("Cannot commit reparenting: node or parent is no longer valid");
			    }
			    return SetParentNow(node, parent);
		    });
		return {};
	}
	return SetParentNow(node, parent);
}

Result<void> Scene::SetParentNow(NodeId node, NodeId parent)
{
	NodeSlot& slot = nodes[node.index];
	slot.hasPendingParent = false;
	if (slot.parent == parent)
	{
		return {};
	}
	if (slot.parent)
	{
		auto& siblings = nodes[slot.parent.index].children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
	}
	else
	{
		rootOrder.erase(std::remove(rootOrder.begin(), rootOrder.end(), node), rootOrder.end());
	}
	slot.parent = parent;
	if (parent)
	{
		nodes[parent.index].children.push_back(node);
	}
	else
	{
		rootOrder.push_back(node);
	}
	MarkWorldDirty(node);
	return {};
}

bool Scene::Contains(NodeId node) const
{
	return node && node.index < nodes.size() && nodes[node.index].alive && !nodes[node.index].pendingDestroy &&
	       nodes[node.index].generation == node.generation;
}

Result<NodeId> Scene::ParentOf(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<NodeId>::Failure("Cannot query the parent of an invalid node");
	}
	return Result<NodeId>::Success(nodes[node.index].parent);
}

Result<Vector<NodeId>> Scene::ChildrenOf(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<Vector<NodeId>>::Failure("Cannot query children of an invalid node");
	}
	return Result<Vector<NodeId>>::Success(nodes[node.index].children);
}

Vector<NodeId> Scene::RootNodes() const
{
	Vector<NodeId> roots;
	for (NodeId node : rootOrder)
	{
		if (Contains(node) && !nodes[node.index].pendingCreate)
		{
			roots.push_back(node);
		}
	}
	return roots;
}

Result<ResourceId> Scene::NodePersistentId(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<ResourceId>::Failure("Cannot query persistent ID of an invalid node");
	}
	return Result<ResourceId>::Success(nodes[node.index].persistentId);
}

Result<void> Scene::AssignNodePersistentId(NodeId node, ResourceId id)
{
	if (!Contains(node) || !id)
	{
		return Result<void>::Failure("Cannot assign an empty ID to an invalid node");
	}
	for (uint32_t index = 0; index < nodes.size(); ++index)
	{
		if (index != node.index && nodes[index].alive && !nodes[index].pendingDestroy &&
		    nodes[index].persistentId == id)
		{
			return Result<void>::Failure("Duplicate persistent node ID " + id.ToString());
		}
	}
	nodes[node.index].persistentId = id;
	return {};
}

Result<NodeId> Scene::NodeByPersistentId(ResourceId id) const
{
	for (uint32_t index = 0; index < nodes.size(); ++index)
	{
		if (nodes[index].persistentId == id)
		{
			const NodeId node{index, nodes[index].generation};
			if (Contains(node))
			{
				return Result<NodeId>::Success(node);
			}
		}
	}
	return Result<NodeId>::Failure("Missing persistent node ID " + id.ToString());
}

Result<void> Scene::ReorderNode(NodeId node, size_t index)
{
	if (!Contains(node))
	{
		return Result<void>::Failure("Cannot reorder an invalid node");
	}
	if (inPhase)
	{
		return Result<void>::Failure("Cannot reorder a node during an update phase");
	}
	const NodeId parent = nodes[node.index].parent;
	auto& siblings = parent ? nodes[parent.index].children : rootOrder;
	if (index >= siblings.size())
	{
		return Result<void>::Failure("Node order index is out of range");
	}
	const auto found = std::find(siblings.begin(), siblings.end(), node);
	if (found == siblings.end())
	{
		return Result<void>::Failure("Node is missing from its sibling list");
	}
	siblings.erase(found);
	siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), node);
	return {};
}

Result<void> Scene::SetLocalTransform(NodeId node, LocalTransform transform)
{
	if (!Contains(node))
	{
		return Result<void>::Failure("Cannot set the transform of an invalid node");
	}
	nodes[node.index].local = transform;
	MarkWorldDirty(node);
	return {};
}

Result<LocalTransform> Scene::GetLocalTransform(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<LocalTransform>::Failure("Cannot query the transform of an invalid node");
	}
	return Result<LocalTransform>::Success(nodes[node.index].local);
}

Result<Mat4> Scene::WorldTransform(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<Mat4>::Failure("Cannot query the world transform of an invalid node");
	}
	return Result<Mat4>::Success(ComputeWorld(node));
}

Result<void> Scene::SetEnabled(NodeId node, bool enabled)
{
	if (!Contains(node))
	{
		return Result<void>::Failure("Cannot enable or disable an invalid node");
	}
	nodes[node.index].enabled = enabled;
	return {};
}

Result<bool> Scene::IsEnabled(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<bool>::Failure("Cannot query the state of an invalid node");
	}
	return Result<bool>::Success(nodes[node.index].enabled);
}

Result<bool> Scene::IsActive(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<bool>::Failure("Cannot query the active state of an invalid node");
	}
	for (NodeId current = node; current; current = nodes[current.index].parent)
	{
		if (!nodes[current.index].enabled)
		{
			return Result<bool>::Success(false);
		}
	}
	return Result<bool>::Success(true);
}

Result<ComponentId> Scene::AddComponentOwned(NodeId node, std::unique_ptr<Component> object)
{
	if (!Contains(node))
	{
		return Result<ComponentId>::Failure("Cannot attach a component to an invalid node");
	}
	uint32_t index;
	if (freeComponentIndices.empty())
	{
		index = static_cast<uint32_t>(components.size());
		components.emplace_back();
	}
	else
	{
		index = freeComponentIndices.back();
		freeComponentIndices.pop_back();
	}
	ComponentSlot& slot = components[index];
	const ComponentId id{index, slot.generation};
	slot.persistentId = ResourceId::Generate();
	object->scene = this;
	object->nodeIndex = node.index;
	object->nodeGeneration = node.generation;
	object->id = id;
	slot.node = node;
	slot.object = std::move(object);
	slot.enabled = true;
	slot.started = false;
	slot.pendingAdd = inPhase;
	slot.pendingRemoval = false;
	slot.availableFrame = frameSerial + (inPhase ? 1 : 0);
	if (inPhase)
	{
		commands.push_back(
		    [this, id, node]()
		    {
			    if (!Contains(node))
			    {
				    RemoveComponentNow(id);
				    return Result<void>::Failure("Cannot commit component creation: owner was destroyed");
			    }
			    components[id.index].pendingAdd = false;
			    nodes[node.index].components.push_back(id);
			    return Result<void>{};
		    });
	}
	else
	{
		nodes[node.index].components.push_back(id);
	}
	return Result<ComponentId>::Success(id);
}

bool Scene::ContainsComponent(ComponentId id) const
{
	return id && id.index < components.size() && components[id.index].object && !components[id.index].pendingRemoval &&
	       components[id.index].generation == id.generation;
}

Result<void> Scene::RemoveComponent(ComponentId id)
{
	if (!ContainsComponent(id))
	{
		return Result<void>::Failure("Cannot remove an invalid component");
	}
	if (inPhase)
	{
		components[id.index].pendingRemoval = true;
		commands.push_back(
		    [this, id]()
		    {
			    if (id.index < components.size() && components[id.index].object &&
			        components[id.index].generation == id.generation)
			    {
				    RemoveComponentNow(id);
			    }
			    return Result<void>{};
		    });
		return {};
	}
	RemoveComponentNow(id);
	return {};
}

void Scene::RemoveComponentNow(ComponentId id)
{
	ComponentSlot& slot = components[id.index];
	slot.pendingRemoval = true;
	if (Contains(slot.node))
	{
		auto& list = nodes[slot.node.index].components;
		list.erase(std::remove(list.begin(), list.end(), id), list.end());
	}
	std::unique_ptr<Component> object = std::move(slot.object);
	slot.node = {};
	slot.pendingAdd = false;
	++slot.generation;
	if (slot.generation == 0)
	{
		slot.generation = 1;
	}
	freeComponentIndices.push_back(id.index);
	object->OnRemove();
}

Result<void> Scene::SetComponentEnabled(ComponentId id, bool enabled)
{
	if (!ContainsComponent(id))
	{
		return Result<void>::Failure("Cannot enable or disable an invalid component");
	}
	components[id.index].enabled = enabled;
	return {};
}

Result<bool> Scene::IsComponentActive(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<bool>::Failure("Cannot query the state of an invalid component");
	}
	const ComponentSlot& slot = components[id.index];
	return Result<bool>::Success(slot.enabled && IsActive(slot.node).value);
}

Result<bool> Scene::IsComponentEnabled(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<bool>::Failure("Cannot query the enabled state of an invalid component");
	}
	return Result<bool>::Success(components[id.index].enabled);
}

Result<NodeId> Scene::ComponentNode(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<NodeId>::Failure("Cannot query the owner of an invalid component");
	}
	return Result<NodeId>::Success(components[id.index].node);
}

Result<Vector<ComponentId>> Scene::ComponentsOf(NodeId node) const
{
	if (!Contains(node))
	{
		return Result<Vector<ComponentId>>::Failure("Cannot query components of an invalid node");
	}
	Vector<ComponentId> result;
	for (ComponentId id : nodes[node.index].components)
	{
		if (ContainsComponent(id))
		{
			result.push_back(id);
		}
	}
	return Result<Vector<ComponentId>>::Success(std::move(result));
}

Component* Scene::GetComponent(ComponentId id) const
{
	return ContainsComponent(id) ? components[id.index].object.get() : nullptr;
}

Result<ResourceId> Scene::ComponentPersistentId(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<ResourceId>::Failure("Cannot query persistent ID of an invalid component");
	}
	return Result<ResourceId>::Success(components[id.index].persistentId);
}

Result<void> Scene::AssignComponentPersistentId(ComponentId component, ResourceId id)
{
	if (!ContainsComponent(component) || !id)
	{
		return Result<void>::Failure("Cannot assign an empty ID to an invalid component");
	}
	for (uint32_t index = 0; index < components.size(); ++index)
	{
		if (index != component.index && components[index].object && !components[index].pendingRemoval &&
		    components[index].persistentId == id)
		{
			return Result<void>::Failure("Duplicate persistent component ID " + id.ToString());
		}
	}
	components[component.index].persistentId = id;
	return {};
}

Result<ComponentId> Scene::ComponentByPersistentId(ResourceId id) const
{
	for (uint32_t index = 0; index < components.size(); ++index)
	{
		if (components[index].persistentId == id)
		{
			const ComponentId component{index, components[index].generation};
			if (ContainsComponent(component))
			{
				return Result<ComponentId>::Success(component);
			}
		}
	}
	return Result<ComponentId>::Failure("Missing persistent component ID " + id.ToString());
}

Result<void> Scene::SetComponentNode(ComponentId component, NodeId node)
{
	if (!ContainsComponent(component) || !Contains(node))
	{
		return Result<void>::Failure("Cannot move an invalid component or target node");
	}
	if (inPhase)
	{
		return Result<void>::Failure("Cannot move a component during an update phase");
	}
	ComponentSlot& slot = components[component.index];
	if (slot.node == node)
	{
		return {};
	}
	auto& previous = nodes[slot.node.index].components;
	previous.erase(std::remove(previous.begin(), previous.end(), component), previous.end());
	nodes[node.index].components.push_back(component);
	slot.node = node;
	slot.object->nodeIndex = node.index;
	slot.object->nodeGeneration = node.generation;
	return {};
}

Result<void> Scene::ReorderComponent(ComponentId component, size_t index)
{
	if (!ContainsComponent(component))
	{
		return Result<void>::Failure("Cannot reorder an invalid component");
	}
	if (inPhase)
	{
		return Result<void>::Failure("Cannot reorder a component during an update phase");
	}
	auto& siblings = nodes[components[component.index].node.index].components;
	if (index >= siblings.size())
	{
		return Result<void>::Failure("Component order index is out of range");
	}
	const auto found = std::find(siblings.begin(), siblings.end(), component);
	if (found == siblings.end())
	{
		return Result<void>::Failure("Component is missing from its node");
	}
	siblings.erase(found);
	siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), component);
	return {};
}

void Scene::BeginFrame()
{
	++frameSerial;
}

Result<void> Scene::Update(const FrameContext& context)
{
	return RunPhase(Phase::update, context);
}

Result<void> Scene::FixedUpdate(const FrameContext& context)
{
	return RunPhase(Phase::fixed, context);
}

Result<void> Scene::LateUpdate(const FrameContext& context)
{
	return RunPhase(Phase::late, context);
}

Result<void> Scene::RunPhase(Phase phase, const FrameContext& context)
{
	Vector<ComponentId> order;
	for (NodeId root : RootNodes())
	{
		CollectComponents(root, order);
	}
	inPhase = true;
	Result<void> phaseResult;
	for (ComponentId id : order)
	{
		if (!ContainsComponent(id))
		{
			continue;
		}
		ComponentSlot& slot = components[id.index];
		if (slot.availableFrame > frameSerial || !IsComponentActive(id).value)
		{
			continue;
		}
		if (!slot.started)
		{
			slot.started = true;
			auto started = slot.object->OnStart(context);
			if (!started)
			{
				phaseResult = Result<void>::Failure("Component start failed: " + started.error);
				break;
			}
			if (!ContainsComponent(id))
			{
				continue;
			}
		}
		Component* object = components[id.index].object.get();
		Result<void> result;
		switch (phase)
		{
			case Phase::update:
				result = object->OnUpdate(context);
				break;
			case Phase::fixed:
				result = object->OnFixedUpdate(context);
				break;
			case Phase::late:
				result = object->OnLateUpdate(context);
				break;
		}
		if (!result)
		{
			phaseResult = Result<void>::Failure("Component callback failed: " + result.error);
			break;
		}
	}
	inPhase = false;
	auto committed = CommitChanges();
	if (!phaseResult)
	{
		return phaseResult;
	}
	return committed;
}

Result<void> Scene::CommitChanges()
{
	if (inPhase)
	{
		return Result<void>::Failure("Cannot commit scene changes during an update phase");
	}
	auto pendingCommands = std::move(commands);
	commands.clear();
	Result<void> result;
	for (auto& command : pendingCommands)
	{
		auto committed = command();
		if (!committed && result)
		{
			result = committed;
		}
	}
	return result;
}

void Scene::CollectComponents(NodeId node, Vector<ComponentId>& out) const
{
	const NodeSlot& slot = nodes[node.index];
	out.insert(out.end(), slot.components.begin(), slot.components.end());
	for (NodeId child : slot.children)
	{
		CollectComponents(child, out);
	}
}

void Scene::Clear()
{
	commands.clear();
	for (uint32_t index = 0; index < nodes.size(); ++index)
	{
		if (nodes[index].alive && !nodes[index].parent)
		{
			DestroySubtree({index, nodes[index].generation});
		}
	}
	for (uint32_t index = 0; index < components.size(); ++index)
	{
		if (components[index].object)
		{
			RemoveComponentNow({index, components[index].generation});
		}
	}
	rootOrder.clear();
}

void Scene::DestroySubtree(NodeId node)
{
	const auto children = nodes[node.index].children;
	for (NodeId child : children)
	{
		DestroySubtree(child);
	}
	const auto componentIds = nodes[node.index].components;
	for (ComponentId id : componentIds)
	{
		if (id.index < components.size() && components[id.index].object &&
		    components[id.index].generation == id.generation)
		{
			RemoveComponentNow(id);
		}
	}
	NodeSlot& slot = nodes[node.index];
	slot.children.clear();
	slot.parent = {};
	slot.alive = false;
	slot.pendingCreate = false;
	slot.pendingDestroy = false;
	slot.hasPendingParent = false;
	++slot.generation;
	if (slot.generation == 0)
	{
		slot.generation = 1;
	}
	freeIndices.push_back(node.index);
}

void Scene::MarkPendingDestroy(NodeId node)
{
	NodeSlot& slot = nodes[node.index];
	slot.pendingDestroy = true;
	for (ComponentId id : slot.components)
	{
		if (ContainsComponent(id))
		{
			components[id.index].pendingRemoval = true;
		}
	}
	for (NodeId child : slot.children)
	{
		MarkPendingDestroy(child);
	}
}

void Scene::MarkWorldDirty(NodeId node)
{
	NodeSlot& slot = nodes[node.index];
	slot.worldDirty = true;
	for (NodeId child : slot.children)
	{
		MarkWorldDirty(child);
	}
}

Mat4 Scene::ComputeWorld(NodeId node) const
{
	const NodeSlot& slot = nodes[node.index];
	if (slot.worldDirty)
	{
		slot.world = slot.parent ? ComputeWorld(slot.parent) * slot.local.Matrix() : slot.local.Matrix();
		slot.worldDirty = false;
	}
	return slot.world;
}
} // namespace Ursa
