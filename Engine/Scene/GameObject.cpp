#include <Scene/GameObject.hpp>
#include <Scene/Scene.hpp>
#include <algorithm>
#include <cstring>

namespace Ursa
{
bool GameObject::ComponentAvailable(const Component& component) const
{
	return !scene || scene->ContainsComponent(component.Id());
}

std::shared_ptr<GameObject> Component::GetGameObject() const
{
	return gameObject ? gameObject->shared_from_this() : std::shared_ptr<GameObject>{};
}

Result<void> Component::SetEnabled(bool value)
{
	if (!SupportsEnabled())
	{
		return Result<void>::Failure("Component does not support an enabled state");
	}
	if (scene)
	{
		return scene->SetComponentEnabled(id, value);
	}
	enabled = value;
	return {};
}

GameObject::Ptr GameObject::Create(String objectName)
{
	return Ptr(new GameObject(std::move(objectName)));
}

GameObject::GameObject(String objectName) : id(ResourceId::Generate()), name(std::move(objectName))
{
}

Result<void> GameObject::SetId(ResourceId value)
{
	if (!alive || scene || !value)
	{
		return Result<void>::Failure("A GameObject ID can only be assigned while detached");
	}
	id = value;
	return {};
}

GameObject::~GameObject()
{
	if (alive)
	{
		DestroyDetachedTree();
	}
}

Result<String> GameObject::Name() const
{
	if (!alive)
	{
		return Result<String>::Failure("Cannot query the name of a destroyed GameObject");
	}
	return Result<String>::Success(name);
}

Result<void> GameObject::SetName(String value)
{
	if (!alive)
	{
		return Result<void>::Failure("Cannot rename a destroyed GameObject");
	}
	if (value.size() > 256 || value.find('\0') != String::npos)
	{
		return Result<void>::Failure("GameObject name must be at most 256 bytes and contain no NUL");
	}
	name = std::move(value);
	return {};
}

GameObject::Ptr GameObject::Parent() const
{
	return parent ? parent->shared_from_this() : Ptr{};
}

Result<UInt32> GameObject::Layer() const
{
	return alive ? Result<UInt32>::Success(layer)
	             : Result<UInt32>::Failure("Cannot query a destroyed GameObject layer");
}

Result<void> GameObject::SetLayer(UInt32 value)
{
	if (!alive || value > 31)
	{
		return Result<void>::Failure("GameObject layer must be between 0 and 31 on a live object");
	}
	layer = value;
	return {};
}

Result<void> GameObject::SetParent(Ptr newParentHandle)
{
	GameObject* newParent = newParentHandle.get();
	if (!alive || (newParent && !newParent->alive))
	{
		return Result<void>::Failure("Cannot change the parent of an invalid GameObject");
	}
	if (scene)
	{
		if (newParent && newParent->scene != scene)
		{
			return Result<void>::Failure("A GameObject parent and child must belong to the same Scene");
		}
		return scene->SetParent(shared_from_this(), std::move(newParentHandle));
	}
	return SetParentInternal(newParent);
}

Result<void> GameObject::SetParentInternal(GameObject* newParent)
{
	if (!alive || (newParent && !newParent->alive))
	{
		return Result<void>::Failure("Cannot change the parent of an invalid GameObject");
	}
	if (newParent)
	{
		if (newParent->scene != scene)
		{
			return Result<void>::Failure("A GameObject parent and child must belong to the same Scene");
		}
		for (GameObject* ancestor = newParent; ancestor; ancestor = ancestor->parent)
		{
			if (ancestor == this)
			{
				return Result<void>::Failure("Cannot create a cycle in the GameObject hierarchy");
			}
		}
	}
	if (parent == newParent)
	{
		return {};
	}
	Ptr self;
	try
	{
		self = shared_from_this();
	}
	catch (const std::bad_weak_ptr&)
	{
		return Result<void>::Failure("GameObject hierarchy members must be created with GameObject::Create");
	}
	if (parent)
	{
		auto& siblings = parent->children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), self), siblings.end());
	}
	parent = newParent;
	SetAssetTemplate(parent ? parent->assetTemplate : ResourceId{});
	if (parent)
	{
		parent->children.push_back(std::move(self));
	}
	return {};
}

Result<void> GameObject::Destroy()
{
	if (!alive)
	{
		return Result<void>::Failure("Cannot destroy an invalid GameObject");
	}
	if (scene)
	{
		return scene->DestroyGameObject(shared_from_this());
	}
	if (parent)
	{
		Ptr self;
		try
		{
			self = shared_from_this();
		}
		catch (const std::bad_weak_ptr&)
		{
			return Result<void>::Failure("GameObject hierarchy members must be created with GameObject::Create");
		}
		auto& siblings = parent->children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), self), siblings.end());
		parent = nullptr;
	}
	DestroyDetachedTree();
	return {};
}

Result<void> GameObject::SetLocalTransform(LocalTransform value)
{
	if (!alive)
	{
		return Result<void>::Failure("Cannot transform a destroyed GameObject");
	}
	localTransform = value;
	return {};
}

Result<LocalTransform> GameObject::GetLocalTransform() const
{
	if (!alive)
	{
		return Result<LocalTransform>::Failure("Cannot query a destroyed GameObject transform");
	}
	return Result<LocalTransform>::Success(localTransform);
}

Result<Mat4> GameObject::WorldTransform() const
{
	if (!alive)
	{
		return Result<Mat4>::Failure("Cannot query the world transform of a destroyed GameObject");
	}
	const Mat4 local = localTransform.Matrix();
	if (!parent)
	{
		return Result<Mat4>::Success(local);
	}
	auto parentWorld = parent->WorldTransform();
	if (!parentWorld)
	{
		return parentWorld;
	}
	return Result<Mat4>::Success(parentWorld.value * local);
}

Result<void> GameObject::SetActive(bool value)
{
	if (!alive)
	{
		return Result<void>::Failure("Cannot change the active state of a destroyed GameObject");
	}
	activeSelf = value;
	return {};
}

Result<bool> GameObject::ActiveSelf() const
{
	if (!alive)
	{
		return Result<bool>::Failure("Cannot query the activeSelf state of a destroyed GameObject");
	}
	return Result<bool>::Success(activeSelf);
}

Result<bool> GameObject::ActiveInHierarchy() const
{
	if (!alive)
	{
		return Result<bool>::Failure("Cannot query the active state of a destroyed GameObject");
	}
	if (scene)
	{
		return scene->ActiveInHierarchy(const_cast<GameObject*>(this)->shared_from_this());
	}
	for (const GameObject* current = this; current; current = current->parent)
	{
		if (!current->activeSelf)
		{
			return Result<bool>::Success(false);
		}
	}
	return Result<bool>::Success(true);
}

Result<void> GameObject::RemoveComponent(Component* component)
{
	if (!alive || !component || component->gameObject != this)
	{
		return Result<void>::Failure("Cannot remove a component from an invalid GameObject");
	}
	if (scene)
	{
		return scene->RemoveComponent(component->Id());
	}
	return RemoveComponentOwned(component);
}

Result<std::unique_ptr<Component>> GameObject::ReleaseDetachedComponent(Component* component)
{
	if (!alive || scene || !component || component->gameObject != this)
	{
		return Result<std::unique_ptr<Component>>::Failure(
		    "Only a component owned by a detached staging object can be released");
	}
	const auto found =
	    std::find_if(components.begin(), components.end(),
	                 [component](const std::unique_ptr<Component>& item) { return item.get() == component; });
	if (found == components.end())
	{
		return Result<std::unique_ptr<Component>>::Failure("Staged component does not belong to its owner");
	}
	auto released = std::move(*found);
	components.erase(found);
	released->gameObject = nullptr;
	return Result<std::unique_ptr<Component>>::Success(std::move(released));
}

Result<Component*> GameObject::AdoptDetachedComponent(std::unique_ptr<Component> component)
{
	if (!component || component->scene || component->gameObject)
	{
		return Result<Component*>::Failure("Only a detached component can be adopted");
	}
	return AddComponentOwned(std::move(component));
}

Result<void> GameObject::RemoveComponentOwned(Component* component)
{
	const auto found =
	    std::find_if(components.begin(), components.end(),
	                 [component](const std::unique_ptr<Component>& item) { return item.get() == component; });
	if (found == components.end())
	{
		return Result<void>::Failure("Component does not belong to this GameObject");
	}
	std::unique_ptr<Component> removed = std::move(*found);
	components.erase(found);
	removed->OnRemove();
	removed->gameObject = nullptr;
	return {};
}

Vector<Component*> GameObject::Components() const
{
	Vector<Component*> result;
	result.reserve(components.size());
	for (const auto& component : components)
	{
		result.push_back(component.get());
	}
	return result;
}

Result<Component*> GameObject::AddComponentOwned(std::unique_ptr<Component> component)
{
	if (!alive || !component)
	{
		return Result<Component*>::Failure("Cannot add a component to an invalid GameObject");
	}
	auto checked = ValidateComponent(*component);
	if (!checked)
	{
		return Result<Component*>::Failure(checked.error);
	}
	if (scene)
	{
		return scene->AttachComponent(*this, std::move(component));
	}
	component->gameObject = this;
	Component* result = component.get();
	components.push_back(std::move(component));
	return Result<Component*>::Success(result);
}

void GameObject::SetAssetTemplate(ResourceId value)
{
	assetTemplate = value;
	for (const auto& child : children)
	{
		child->SetAssetTemplate(value);
	}
}

Result<void> GameObject::ValidateComponent(const Component& candidate) const
{
	const char* group = candidate.UniquenessGroup();
	if (group)
	{
		for (const auto& component : components)
		{
			if (scene && !scene->ContainsComponent(component->Id()))
			{
				continue;
			}
			const char* existing = component->UniquenessGroup();
			if (existing && std::strcmp(group, existing) == 0)
			{
				return Result<void>::Failure(String("GameObject already contains component group ") + group);
			}
		}
	}
	return {};
}

void GameObject::DestroyDetachedTree()
{
	if (!alive)
	{
		return;
	}
	alive = false;
	parent = nullptr;
	Vector<Ptr> ownedChildren = std::move(children);
	for (const Ptr& child : ownedChildren)
	{
		child->parent = nullptr;
		child->DestroyDetachedTree();
	}
	for (auto& component : components)
	{
		component->OnRemove();
		component->gameObject = nullptr;
	}
	components.clear();
}

Result<void> GameObject::RequireDetached(const char* operation) const
{
	if (scene)
	{
		return Result<void>::Failure(String("Cannot ") + operation + " while GameObject is attached to a Scene");
	}
	return {};
}
} // namespace Ursa
