#include <Core/Types.hpp>
#include <Scene/Scene.hpp>
#include <algorithm>

namespace Ursa
{

Scene::~Scene()
{
	Clear();
}

Scene::ObjectHandle Scene::HandleOf(const GameObject* object) const
{
	if (!object || object->scene != this || object->sceneNodeIndex >= objects.size())
	{
		return {};
	}
	const ObjectHandle handle{object->sceneNodeIndex, object->sceneNodeGeneration};
	const ObjectSlot& slot = objects[handle.index];
	return slot.alive && slot.generation == handle.generation && slot.object == object ? handle : ObjectHandle{};
}

Result<Scene::ObjectHandle> Scene::RegisterGameObject(const GameObject::Ptr& object, bool pendingCreate)
{
	if (!object || !object->alive || object->parent || object->scene)
	{
		return Result<ObjectHandle>::Failure("Scene can only attach a valid detached GameObject root");
	}
	std::function<Result<void>(GameObject*)> validate = [&](GameObject* current) -> Result<void>
	{
		if (!current || !current->alive || current->scene)
		{
			return Result<void>::Failure("GameObject tree contains an invalid or already attached object");
		}
		for (const auto& child : current->children)
		{
			if (!child || child->parent != current)
			{
				return Result<void>::Failure("GameObject tree contains an inconsistent parent link");
			}
			auto result = validate(child.get());
			if (!result)
			{
				return result;
			}
		}
		return {};
	};
	auto valid = validate(object.get());
	if (!valid)
	{
		return Result<ObjectHandle>::Failure(valid.error);
	}

	ObjectHandle root;
	std::function<ObjectHandle(GameObject*)> registerObject = [&](GameObject* current) -> ObjectHandle
	{
		Ursa::UInt32 index;
		if (freeObjectIndices.empty())
		{
			index = static_cast<Ursa::UInt32>(objects.size());
			objects.emplace_back();
		}
		else
		{
			index = freeObjectIndices.back();
			freeObjectIndices.pop_back();
		}
		ObjectSlot& slot = objects[index];
		slot.object = current;
		slot.alive = true;
		slot.pendingCreate = pendingCreate;
		slot.pendingDestroy = false;
		slot.pendingParent = {};
		slot.hasPendingParent = false;
		current->scene = this;
		current->sceneNodeIndex = index;
		current->sceneNodeGeneration = slot.generation;
		const ObjectHandle handle{index, slot.generation};
		if (!root)
		{
			root = handle;
		}
		for (const auto& component : current->components)
		{
			Ursa::UInt32 componentIndex;
			if (freeComponentIndices.empty())
			{
				componentIndex = static_cast<Ursa::UInt32>(components.size());
				components.emplace_back();
			}
			else
			{
				componentIndex = freeComponentIndices.back();
				freeComponentIndices.pop_back();
			}
			ComponentSlot& componentSlot = components[componentIndex];
			const ComponentId componentId{componentIndex, componentSlot.generation};
			componentSlot.persistentId = ResourceId::Generate();
			componentSlot.owner = handle;
			componentSlot.object = component.get();
			componentSlot.enabled = true;
			componentSlot.started = false;
			componentSlot.pendingAdd = pendingCreate;
			componentSlot.pendingRemoval = false;
			componentSlot.availableFrame = frameSerial + (pendingCreate ? 1 : 0);
			component->scene = this;
			component->id = componentId;
		}
		for (const auto& child : current->children)
		{
			registerObject(child.get());
		}
		return handle;
	};
	registerObject(object.get());
	return Result<ObjectHandle>::Success(root);
}

Result<void> Scene::AttachGameObject(const GameObject::Ptr& object, GameObject::Ptr parent)
{
	if (!object || !object->alive || object->parent)
	{
		return Result<void>::Failure("Scene can only attach a valid detached GameObject root");
	}
	if (parent && !Contains(parent))
	{
		return Result<void>::Failure("Cannot attach a GameObject under an invalid Scene parent");
	}
	if (inPhase)
	{
		commands.push_back([this, object, parent] { return AttachGameObjectNow(object, std::move(parent), true); });
		return {};
	}
	return AttachGameObjectNow(object, std::move(parent));
}

Result<void> Scene::AttachGameObjectNow(const GameObject::Ptr& object, GameObject::Ptr parent, bool deferred)
{
	if (parent && !Contains(parent))
	{
		return Result<void>::Failure("Cannot commit GameObject attachment: parent is no longer valid");
	}
	auto registered = RegisterGameObject(object, false);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	if (deferred)
	{
		std::function<void(GameObject*)> defer = [&](GameObject* current)
		{
			for (const auto& component : current->components)
			{
				const ComponentId componentId = component->id;
				if (componentId && componentId.index < components.size() &&
				    components[componentId.index].generation == componentId.generation)
				{
					components[componentId.index].availableFrame = frameSerial + 1;
				}
			}
			for (const auto& child : current->children)
			{
				defer(child.get());
			}
		};
		defer(object.get());
	}
	if (parent)
	{
		auto attached = object->SetParentInternal(parent.get());
		if (!attached)
		{
			DestroySubtree(registered.value);
			return attached;
		}
	}
	else
	{
		rootObjects.push_back(object);
	}
	return {};
}

Result<GameObject::Ptr> Scene::CreateGameObject(String name, GameObject::Ptr parent)
{
	auto object = GameObject::Create(std::move(name));
	if (parent && !Contains(parent))
	{
		return Result<GameObject::Ptr>::Failure("Cannot create a GameObject under an invalid Scene parent");
	}
	auto registered = RegisterGameObject(object, inPhase);
	if (!registered)
	{
		return Result<GameObject::Ptr>::Failure(registered.error);
	}
	const ObjectHandle objectHandle = registered.value;
	const ObjectHandle parentHandle = HandleOf(parent.get());
	if (inPhase)
	{
		objects[objectHandle.index].pendingParent = parentHandle;
		objects[objectHandle.index].hasPendingParent = true;
		rootObjects.push_back(object);
		commands.push_back(
		    [this, objectHandle, parentHandle]()
		    {
			    if (parentHandle && !Contains(parentHandle))
			    {
				    DestroySubtree(objectHandle);
				    return Result<void>::Failure("Cannot commit GameObject creation: parent is no longer valid");
			    }
			    return SetParentNow(objectHandle, parentHandle);
		    });
	}
	else if (parent)
	{
		auto attached = object->SetParentInternal(parent.get());
		if (!attached)
		{
			DestroySubtree(objectHandle);
			return Result<GameObject::Ptr>::Failure(attached.error);
		}
	}
	else
	{
		rootObjects.push_back(object);
	}
	return Result<GameObject::Ptr>::Success(std::move(object));
}

Result<GameObject::Ptr> Scene::CreateGameObject(GameObject::Ptr parent)
{
	return CreateGameObject({}, std::move(parent));
}

Result<void> Scene::DestroyGameObject(const GameObject::Ptr& object)
{
	const ObjectHandle handle = HandleOf(object.get());
	if (!handle || !Contains(handle))
	{
		return Result<void>::Failure("Cannot destroy a GameObject that is not in this Scene");
	}
	if (inPhase)
	{
		MarkPendingDestroy(handle);
		commands.push_back(
		    [this, handle]()
		    {
			    if (handle.index < objects.size() && objects[handle.index].alive && objects[handle.index].object &&
			        objects[handle.index].generation == handle.generation)
			    {
				    DestroyObjectNow(handle);
			    }
			    return Result<void>{};
		    });
		return {};
	}
	DestroyObjectNow(handle);
	return {};
}

bool Scene::Contains(const GameObject::Ptr& object) const
{
	const ObjectHandle handle = HandleOf(object.get());
	return handle && Contains(handle);
}

bool Scene::Contains(ObjectHandle object) const
{
	return object && object.index < objects.size() && objects[object.index].alive && objects[object.index].object &&
	       objects[object.index].object->alive && objects[object.index].object->scene == this &&
	       !objects[object.index].pendingDestroy && objects[object.index].generation == object.generation;
}

Result<void> Scene::SetParent(const GameObject::Ptr& object, GameObject::Ptr parent)
{
	const ObjectHandle childHandle = HandleOf(object.get());
	const ObjectHandle parentHandle = HandleOf(parent.get());
	if (!childHandle || !Contains(childHandle))
	{
		return Result<void>::Failure("Cannot reparent an invalid GameObject");
	}
	if (parent && (!parentHandle || !Contains(parentHandle)))
	{
		return Result<void>::Failure("Cannot reparent to a GameObject outside this Scene");
	}
	for (GameObject* ancestor = parent.get(); ancestor;)
	{
		if (ancestor == object.get())
		{
			return Result<void>::Failure("Cannot create a cycle in the GameObject hierarchy");
		}
		const ObjectHandle ancestorHandle = HandleOf(ancestor);
		const ObjectSlot& slot = objects[ancestorHandle.index];
		ancestor = slot.hasPendingParent ? (slot.pendingParent ? objects[slot.pendingParent.index].object : nullptr)
		                                 : ancestor->parent;
	}
	if (inPhase)
	{
		objects[childHandle.index].pendingParent = parentHandle;
		objects[childHandle.index].hasPendingParent = true;
		commands.push_back(
		    [this, childHandle, parentHandle]()
		    {
			    if (!Contains(childHandle) || (parentHandle && !Contains(parentHandle)))
			    {
				    return Result<void>::Failure("Cannot commit reparenting: GameObject or parent is no longer valid");
			    }
			    return SetParentNow(childHandle, parentHandle);
		    });
		return {};
	}
	return SetParentNow(childHandle, parentHandle);
}

void Scene::DestroyObjectNow(ObjectHandle handle)
{
	MarkPendingDestroy(handle);
	ObjectSlot& slot = objects[handle.index];
	GameObject* object = slot.object;
	const GameObject::Ptr keepAlive = object->shared_from_this();
	if (object->parent)
	{
		object->SetParentInternal(nullptr);
	}
	else
	{
		const auto found = std::find(rootObjects.begin(), rootObjects.end(), keepAlive);
		if (found != rootObjects.end())
		{
			rootObjects.erase(found);
		}
	}
	DestroySubtree(handle);
}

Result<void> Scene::SetParentNow(ObjectHandle objectHandle, ObjectHandle parentHandle)
{
	ObjectSlot& slot = objects[objectHandle.index];
	slot.hasPendingParent = false;
	slot.pendingCreate = false;
	GameObject* object = slot.object;
	GameObject* newParent = parentHandle ? objects[parentHandle.index].object : nullptr;
	const GameObject::Ptr keepAlive = object->shared_from_this();
	if (object->parent == newParent)
	{
		if (!newParent)
		{
			const auto found = std::find(rootObjects.begin(), rootObjects.end(), keepAlive);
			if (found == rootObjects.end())
			{
				rootObjects.push_back(keepAlive);
			}
		}
		return {};
	}
	const bool wasRoot = object->parent == nullptr;
	if (wasRoot)
	{
		const auto found = std::find(rootObjects.begin(), rootObjects.end(), keepAlive);
		if (found != rootObjects.end())
		{
			rootObjects.erase(found);
		}
	}
	auto reparented = object->SetParentInternal(newParent);
	if (!reparented)
	{
		if (wasRoot)
		{
			rootObjects.push_back(keepAlive);
		}
		return reparented;
	}
	if (!newParent)
	{
		rootObjects.push_back(keepAlive);
	}
	return {};
}

Result<GameObject::Ptr> Scene::ParentOf(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<GameObject::Ptr>::Failure("Cannot query the parent of a GameObject outside this Scene");
	}
	GameObject* parent = object->parent;
	return Result<GameObject::Ptr>::Success(parent ? parent->shared_from_this() : GameObject::Ptr{});
}

Result<Vector<GameObject::Ptr>> Scene::ChildrenOf(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<Vector<GameObject::Ptr>>::Failure("Cannot query children of a GameObject outside this Scene");
	}
	return Result<Vector<GameObject::Ptr>>::Success(object->Children());
}

Vector<GameObject::Ptr> Scene::RootGameObjects() const
{
	Vector<GameObject::Ptr> roots;
	for (const GameObject::Ptr& object : rootObjects)
	{
		if (Contains(object) && !objects[HandleOf(object.get()).index].pendingCreate)
		{
			roots.push_back(object);
		}
	}
	return roots;
}

Result<void> Scene::AssignGameObjectId(const GameObject::Ptr& object, ResourceId id)
{
	if (!Contains(object) || !id)
	{
		return Result<void>::Failure("Cannot assign an empty ID to a GameObject outside this Scene");
	}
	for (const ObjectSlot& slot : objects)
	{
		if (slot.object && slot.object != object.get() && !slot.pendingDestroy && slot.object->Id() == id)
		{
			return Result<void>::Failure("Duplicate GameObject ID " + id.ToString());
		}
	}
	object->id = id;
	return {};
}

Result<ResourceId> Scene::PersistentId(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<ResourceId>::Failure("Cannot query the ID of a GameObject outside this Scene");
	}
	return Result<ResourceId>::Success(object->Id());
}

Result<String> Scene::GameObjectName(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<String>::Failure("Cannot query the name of a GameObject outside this Scene");
	}
	return object->Name();
}

Result<void> Scene::SetGameObjectName(const GameObject::Ptr& object, String name)
{
	if (!Contains(object))
	{
		return Result<void>::Failure("Cannot rename a GameObject outside this Scene");
	}
	return object->SetName(std::move(name));
}

Result<GameObject::Ptr> Scene::GameObjectById(ResourceId id) const
{
	for (const ObjectSlot& slot : objects)
	{
		if (slot.object && slot.object->Id() == id)
		{
			const ObjectHandle handle{slot.object->sceneNodeIndex, slot.object->sceneNodeGeneration};
			if (Contains(handle))
			{
				return Result<GameObject::Ptr>::Success(slot.object->shared_from_this());
			}
		}
	}
	return Result<GameObject::Ptr>::Failure("Missing GameObject ID " + id.ToString());
}

Result<void> Scene::ReorderGameObject(const GameObject::Ptr& object, Ursa::Size index)
{
	if (!Contains(object))
	{
		return Result<void>::Failure("Cannot reorder a GameObject outside this Scene");
	}
	if (inPhase)
	{
		return Result<void>::Failure("Cannot reorder a GameObject during an update phase");
	}
	GameObject* rawObject = object.get();
	auto& siblings = rawObject->parent ? rawObject->parent->children : rootObjects;
	if (index >= siblings.size())
	{
		return Result<void>::Failure("GameObject order index is out of range");
	}
	const auto found = std::find(siblings.begin(), siblings.end(), object);
	if (found == siblings.end())
	{
		return Result<void>::Failure("GameObject is missing from its sibling list");
	}
	siblings.erase(found);
	siblings.insert(siblings.begin() + static_cast<Ursa::PtrDiff>(index), object);
	return {};
}

Result<void> Scene::SetLocalTransform(const GameObject::Ptr& object, LocalTransform transform)
{
	if (!Contains(object))
	{
		return Result<void>::Failure("Cannot transform a GameObject outside this Scene");
	}
	return object->SetLocalTransform(transform);
}

Result<LocalTransform> Scene::GetLocalTransform(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<LocalTransform>::Failure("Cannot query a GameObject transform outside this Scene");
	}
	return object->GetLocalTransform();
}

Result<Mat4> Scene::WorldTransform(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<Mat4>::Failure("Cannot query a GameObject world transform outside this Scene");
	}
	return object->WorldTransform();
}

Result<void> Scene::SetEnabled(const GameObject::Ptr& object, bool enabled)
{
	if (!Contains(object))
	{
		return Result<void>::Failure("Cannot change the state of a GameObject outside this Scene");
	}
	return object->SetEnabled(enabled);
}

Result<bool> Scene::IsEnabled(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<bool>::Failure("Cannot query a GameObject outside this Scene");
	}
	return object->IsEnabled();
}

Result<bool> Scene::IsActive(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<bool>::Failure("Cannot query a GameObject outside this Scene");
	}
	return object->IsActive();
}

Result<ComponentId> Scene::AddComponentOwned(const GameObject::Ptr& object, std::unique_ptr<Component> component)
{
	if (!Contains(object))
	{
		return Result<ComponentId>::Failure("Cannot attach a component to a GameObject outside this Scene");
	}
	auto attached = AttachComponent(*object, std::move(component));
	if (!attached)
	{
		return Result<ComponentId>::Failure(attached.error);
	}
	return Result<ComponentId>::Success(attached.value->Id());
}

Result<Component*> Scene::AttachComponent(GameObject& owner, std::unique_ptr<Component> object)
{
	const ObjectHandle node = HandleOf(&owner);
	if (!node || !object)
	{
		return Result<Component*>::Failure("Cannot attach a component to a GameObject outside this Scene");
	}
	Ursa::UInt32 index;
	if (freeComponentIndices.empty())
	{
		index = static_cast<Ursa::UInt32>(components.size());
		components.emplace_back();
	}
	else
	{
		index = freeComponentIndices.back();
		freeComponentIndices.pop_back();
	}
	ComponentSlot& slot = components[index];
	const ComponentId id{index, slot.generation};
	object->gameObject = &owner;
	Component* component = object.get();
	owner.components.push_back(std::move(object));
	slot.persistentId = ResourceId::Generate();
	component->scene = this;
	component->id = id;
	slot.owner = node;
	slot.object = component;
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
			    return Result<void>{};
		    });
	}
	return Result<Component*>::Success(component);
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
	Component* object = slot.object;
	slot.owner = {};
	slot.object = nullptr;
	slot.pendingAdd = false;
	++slot.generation;
	if (slot.generation == 0)
	{
		slot.generation = 1;
	}
	freeComponentIndices.push_back(id.index);
	if (object && object->gameObject)
	{
		object->gameObject->RemoveComponentOwned(object);
	}
}

Result<void> Scene::SetComponentEnabled(ComponentId id, bool enabled)
{
	if (!ContainsComponent(id))
	{
		return Result<void>::Failure("Cannot enable or disable an invalid component");
	}
	components[id.index].enabled = enabled;
	components[id.index].object->enabled = enabled;
	return {};
}

Result<bool> Scene::IsComponentActive(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<bool>::Failure("Cannot query the state of an invalid component");
	}
	const ComponentSlot& slot = components[id.index];
	GameObject* owner = slot.owner ? objects[slot.owner.index].object : nullptr;
	for (GameObject* current = owner; current; current = current->parent)
	{
		if (!current->enabled)
		{
			return Result<bool>::Success(false);
		}
	}
	return Result<bool>::Success(slot.enabled && owner != nullptr);
}

Result<bool> Scene::IsComponentEnabled(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<bool>::Failure("Cannot query the enabled state of an invalid component");
	}
	return Result<bool>::Success(components[id.index].enabled);
}

Result<GameObject::Ptr> Scene::ComponentOwner(ComponentId id) const
{
	if (!ContainsComponent(id))
	{
		return Result<GameObject::Ptr>::Failure("Cannot query the owner of an invalid component");
	}
	GameObject* owner = components[id.index].object->gameObject;
	return owner ? Result<GameObject::Ptr>::Success(owner->shared_from_this())
	             : Result<GameObject::Ptr>::Failure("Component has no GameObject owner");
}

Result<Vector<ComponentId>> Scene::ComponentsOf(const GameObject::Ptr& object) const
{
	if (!Contains(object))
	{
		return Result<Vector<ComponentId>>::Failure("Cannot query components of a GameObject outside this Scene");
	}
	Vector<ComponentId> result;
	for (const auto& component : object->components)
	{
		if (component && ContainsComponent(component->id))
		{
			result.push_back(component->id);
		}
	}
	return Result<Vector<ComponentId>>::Success(std::move(result));
}

Component* Scene::GetComponent(ComponentId id) const
{
	return ContainsComponent(id) ? components[id.index].object : nullptr;
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
	for (Ursa::UInt32 index = 0; index < components.size(); ++index)
	{
		if (index != component.index && components[index].object && !components[index].pendingRemoval &&
		    components[index].persistentId == id)
		{
			return Result<void>::Failure("Duplicate persistent component ID " + id.ToString());
		}
	}
	components[component.index].persistentId = id;
	components[component.index].object->persistentId = id;
	return {};
}

Result<ComponentId> Scene::ComponentByPersistentId(ResourceId id) const
{
	for (Ursa::UInt32 index = 0; index < components.size(); ++index)
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

Result<void> Scene::SetComponentOwner(ComponentId component, const GameObject::Ptr& object)
{
	if (!ContainsComponent(component) || !Contains(object))
	{
		return Result<void>::Failure("Cannot move a component to an invalid GameObject");
	}
	if (inPhase)
	{
		return Result<void>::Failure("Cannot move a component during an update phase");
	}
	ComponentSlot& slot = components[component.index];
	const ObjectHandle ownerHandle = HandleOf(object.get());
	if (slot.owner == ownerHandle)
	{
		return {};
	}
	GameObject* oldOwner = objects[slot.owner.index].object;
	GameObject* newOwner = object.get();
	const auto found =
	    std::find_if(oldOwner->components.begin(), oldOwner->components.end(),
	                 [object = slot.object](const std::unique_ptr<Component>& item) { return item.get() == object; });
	if (found == oldOwner->components.end())
	{
		return Result<void>::Failure("Component is missing from its GameObject owner");
	}
	std::unique_ptr<Component> moved = std::move(*found);
	oldOwner->components.erase(found);
	moved->gameObject = newOwner;
	newOwner->components.push_back(std::move(moved));
	slot.owner = ownerHandle;
	return {};
}

Result<void> Scene::ReorderComponent(ComponentId component, Ursa::Size index)
{
	if (!ContainsComponent(component))
	{
		return Result<void>::Failure("Cannot reorder an invalid component");
	}
	if (inPhase)
	{
		return Result<void>::Failure("Cannot reorder a component during an update phase");
	}
	auto& siblings = objects[components[component.index].owner.index].object->components;
	if (index >= siblings.size())
	{
		return Result<void>::Failure("Component order index is out of range");
	}
	const auto found =
	    std::find_if(siblings.begin(), siblings.end(),
	                 [component](const std::unique_ptr<Component>& item) { return item->id == component; });
	if (found == siblings.end())
	{
		return Result<void>::Failure("Component is missing from its node");
	}
	std::unique_ptr<Component> moved = std::move(*found);
	siblings.erase(found);
	siblings.insert(siblings.begin() + static_cast<Ursa::PtrDiff>(index), std::move(moved));
	return {};
}

void Scene::BeginFrame()
{
	++frameSerial;
}

Result<void> Scene::Update(const FrameContext& context)
{
	return RunPhase(Phase::Update, context);
}

Result<void> Scene::FixedUpdate(const FrameContext& context)
{
	return RunPhase(Phase::Fixed, context);
}

Result<void> Scene::LateUpdate(const FrameContext& context)
{
	return RunPhase(Phase::Late, context);
}

Result<void> Scene::RunPhase(Phase phase, const FrameContext& context)
{
	Vector<ComponentId> order;
	for (const GameObject::Ptr& root : RootGameObjects())
	{
		CollectComponents(HandleOf(root.get()), order);
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
		Component* object = components[id.index].object;
		Result<void> result;
		switch (phase)
		{
			case Phase::Update:
				result = object->OnUpdate(context);
				break;
			case Phase::Fixed:
				result = object->OnFixedUpdate(context);
				break;
			case Phase::Late:
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

void Scene::CollectComponents(ObjectHandle node, Vector<ComponentId>& out) const
{
	const GameObject* object = objects[node.index].object;
	for (const auto& component : object->components)
	{
		if (component && ContainsComponent(component->id))
		{
			out.push_back(component->id);
		}
	}
	for (const auto& child : object->children)
	{
		const ObjectHandle childHandle = HandleOf(child.get());
		if (childHandle)
		{
			CollectComponents(childHandle, out);
		}
	}
}

void Scene::Clear()
{
	commands.clear();
	const Vector<GameObject::Ptr> roots = rootObjects;
	for (const GameObject::Ptr& root : roots)
	{
		const ObjectHandle node = HandleOf(root.get());
		if (node)
		{
			DestroyObjectNow(node);
		}
	}
	for (Ursa::UInt32 index = 0; index < components.size(); ++index)
	{
		if (components[index].object)
		{
			RemoveComponentNow({index, components[index].generation});
		}
	}
	rootObjects.clear();
	activeCamera = {};
}

void Scene::DestroySubtree(ObjectHandle node)
{
	ObjectSlot& slot = objects[node.index];
	GameObject* object = slot.object;
	if (!object)
	{
		return;
	}
	const Vector<GameObject::Ptr> children = object->children;
	for (const GameObject::Ptr& child : children)
	{
		child->parent = nullptr;
		const ObjectHandle childHandle = HandleOf(child.get());
		if (childHandle)
		{
			DestroySubtree(childHandle);
		}
	}
	const Vector<Component*> ownedComponents = object->Components();
	for (Component* component : ownedComponents)
	{
		const ComponentId id = component->Id();
		if (id.index < components.size() && components[id.index].object == component &&
		    components[id.index].generation == id.generation)
		{
			RemoveComponentNow(id);
		}
	}
	object->children.clear();
	object->parent = nullptr;
	object->scene = nullptr;
	object->sceneNodeIndex = std::numeric_limits<Ursa::UInt32>::max();
	object->sceneNodeGeneration = 0;
	object->DestroyDetachedTree();
	slot.object = nullptr;
	slot.alive = false;
	slot.pendingCreate = false;
	slot.pendingDestroy = false;
	slot.hasPendingParent = false;
	++slot.generation;
	if (slot.generation == 0)
	{
		slot.generation = 1;
	}
	freeObjectIndices.push_back(node.index);
}

void Scene::MarkPendingDestroy(ObjectHandle node)
{
	ObjectSlot& slot = objects[node.index];
	slot.pendingDestroy = true;
	GameObject* object = slot.object;
	if (!object)
	{
		return;
	}
	for (const auto& component : object->components)
	{
		const ComponentId id = component->id;
		if (ContainsComponent(id))
		{
			components[id.index].pendingRemoval = true;
		}
	}
	for (const auto& child : object->children)
	{
		const ObjectHandle childHandle = HandleOf(child.get());
		if (childHandle)
		{
			MarkPendingDestroy(childHandle);
		}
	}
}

} // namespace Ursa
