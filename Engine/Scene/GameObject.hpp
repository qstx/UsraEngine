#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Math/Mat4.hpp>
#include <Scene/Component.hpp>
#include <Scene/LocalTransform.hpp>
#include <Serialization/ResourceId.hpp>
#include <memory>
#include <limits>
#include <type_traits>
#include <utility>

namespace Ursa
{
class GameObject final : public std::enable_shared_from_this<GameObject>
{
public:
	using Ptr = std::shared_ptr<GameObject>;

	static Ptr Create(String objectName = {});

	~GameObject();

	GameObject(const GameObject&) = delete;

	GameObject& operator=(const GameObject&) = delete;

	ResourceId Id() const
	{
		return id;
	}

	Result<void> SetId(ResourceId value);

	bool IsValid() const
	{
		return alive;
	}

	Result<String> Name() const;

	Result<void> SetName(String value);

	Result<void> SetParent(Ptr newParent);

	Ptr Parent() const;

	Vector<Ptr> Children() const
	{
		return children;
	}

	Result<void> Destroy();

	Result<void> SetLocalTransform(LocalTransform value);

	Result<LocalTransform> GetLocalTransform() const;

	Result<Mat4> WorldTransform() const;

	Result<void> SetEnabled(bool value);

	Result<bool> IsEnabled() const;

	Result<bool> IsActive() const;

	template <class T, class... Args> Result<T*> AddComponent(Args&&... args)
	{
		static_assert(std::is_base_of<Component, T>::value, "GameObject components must derive from Component");
		auto added = AddComponentOwned(std::make_unique<T>(std::forward<Args>(args)...));
		if (!added)
		{
			return Result<T*>::Failure(added.error);
		}
		return Result<T*>::Success(static_cast<T*>(added.value));
	}

	template <class T> T* FindComponent() const
	{
		for (const auto& component : components)
		{
			if (auto* typed = dynamic_cast<T*>(component.get()))
			{
				return typed;
			}
		}
		return nullptr;
	}

	template <class T> Vector<T*> FindComponents() const
	{
		Vector<T*> found;
		for (const auto& component : components)
		{
			if (auto* typed = dynamic_cast<T*>(component.get()))
			{
				found.push_back(typed);
			}
		}
		return found;
	}

	Result<void> RemoveComponent(Component* component);

	Vector<Component*> Components() const;

private:
	friend class Scene;
	friend class PrefabAsset;

	explicit GameObject(String objectName);

	Result<Component*> AddComponentOwned(std::unique_ptr<Component> component);

	Result<void> RemoveComponentOwned(Component* component);

	Result<void> SetParentInternal(GameObject* newParent);

	void DestroyDetachedTree();

	Result<void> RequireDetached(const char* operation) const;

	ResourceId id;
	String name;
	GameObject* parent = nullptr;
	Vector<Ptr> children;
	Vector<std::unique_ptr<Component>> components;
	LocalTransform localTransform;
	bool enabled = true;
	bool alive = true;
	Scene* scene = nullptr;
	Ursa::UInt32 sceneNodeIndex = std::numeric_limits<Ursa::UInt32>::max();
	Ursa::UInt32 sceneNodeGeneration = 0;
};
} // namespace Ursa
