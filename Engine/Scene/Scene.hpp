#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/Component.hpp>
#include <Scene/GameObject.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/ResourceId.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace Ursa
{
class Scene final : public Serializable
{
public:
	String SerializationType() const override
	{
		return "ursa.Scene";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Scene() = default;

	~Scene();

	Scene(const Scene&) = delete;

	Scene& operator=(const Scene&) = delete;

	Result<GameObject::Ptr> CreateGameObject(String name = {}, GameObject::Ptr parent = {});

	Result<GameObject::Ptr> CreateGameObject(GameObject::Ptr parent);

	std::function<void()> SetRuntimePreparation(std::function<Result<void>()> callback, bool clearOnRevoke = false);

	Result<void> AttachGameObject(const GameObject::Ptr& object, GameObject::Ptr parent = {});

	Result<void> DestroyGameObject(const GameObject::Ptr& object);

	bool Contains(const GameObject::Ptr& object) const;

	Result<void> SetParent(const GameObject::Ptr& object, GameObject::Ptr parent = {});

	Result<GameObject::Ptr> ParentOf(const GameObject::Ptr& object) const;

	Result<Vector<GameObject::Ptr>> ChildrenOf(const GameObject::Ptr& object) const;

	Vector<GameObject::Ptr> RootGameObjects() const;

	Result<void> AssignGameObjectId(const GameObject::Ptr& object, ResourceId id);

	Result<ResourceId> PersistentId(const GameObject::Ptr& object) const;

	Result<String> GameObjectName(const GameObject::Ptr& object) const;

	Result<void> SetGameObjectName(const GameObject::Ptr& object, String name);

	Result<GameObject::Ptr> GameObjectById(ResourceId id) const;

	Result<void> ReorderGameObject(const GameObject::Ptr& object, Ursa::Size index);

	Result<void> SetLocalTransform(const GameObject::Ptr& object, LocalTransform transform);

	Result<LocalTransform> GetLocalTransform(const GameObject::Ptr& object) const;

	Result<Mat4> WorldTransform(const GameObject::Ptr& object) const;

	Result<void> SetActive(const GameObject::Ptr& object, bool active);

	Result<bool> ActiveSelf(const GameObject::Ptr& object) const;

	Result<bool> ActiveInHierarchy(const GameObject::Ptr& object) const;

	template <class T, class... Args> Result<ComponentId> AddComponent(const GameObject::Ptr& object, Args&&... args)
	{
		static_assert(std::is_base_of<Component, T>::value, "Scene components must derive from Component");
		return AddComponentOwned(object, std::make_unique<T>(std::forward<Args>(args)...));
	}

	template <class T> T* FindComponent(const GameObject::Ptr& object) const
	{
		if (!Contains(object))
		{
			return nullptr;
		}
		for (const auto& component : object->components)
		{
			if (component && ContainsComponent(component->id) && !components[component->id.index].pendingAdd)
			{
				if (auto* typed = dynamic_cast<T*>(component.get()))
				{
					return typed;
				}
			}
		}
		return nullptr;
	}

	template <class T> Vector<T*> FindComponents(const GameObject::Ptr& object) const
	{
		Vector<T*> found;
		if (!Contains(object))
		{
			return found;
		}
		for (const auto& component : object->components)
		{
			if (component && ContainsComponent(component->id) && !components[component->id.index].pendingAdd)
			{
				if (auto* typed = dynamic_cast<T*>(component.get()))
				{
					found.push_back(typed);
				}
			}
		}
		return found;
	}

	bool ContainsComponent(ComponentId id) const;

	Result<void> RemoveComponent(ComponentId id);

	Result<void> SetComponentEnabled(ComponentId id, bool enabled);

	Result<bool> IsComponentActive(ComponentId id) const;

	Result<bool> IsComponentEnabled(ComponentId id) const;

	Result<GameObject::Ptr> ComponentOwner(ComponentId id) const;

	Result<Vector<ComponentId>> ComponentsOf(const GameObject::Ptr& object) const;

	Component* GetComponent(ComponentId id) const;

	Result<ResourceId> ComponentPersistentId(ComponentId id) const;

	Result<void> AssignComponentPersistentId(ComponentId component, ResourceId id);

	Result<ComponentId> ComponentByPersistentId(ResourceId id) const;

	Result<void> SetComponentOwner(ComponentId component, const GameObject::Ptr& object);

	Result<void> ReorderComponent(ComponentId component, Ursa::Size index);

	Result<void> SetActiveCamera(ComponentId id);

	ComponentId ActiveCamera() const
	{
		return activeCamera;
	}

	Result<void> SetAmbientLight(Color color, Ursa::Float32 intensity);

	Color AmbientLightColor() const
	{
		return ambientColor;
	}

	Ursa::Float32 AmbientLightIntensity() const
	{
		return ambientIntensity;
	}

	Result<SceneRenderData> CollectRenderData(Ursa::Float32 aspect) const;

	// Uses a caller-owned camera without invoking component lifecycle callbacks.
	Result<SceneRenderData> CollectPreviewRenderData(const Camera& camera, Ursa::Float32 aspect) const;

	void BeginFrame();

	Result<void> Update(const FrameContext& context);

	Result<void> FixedUpdate(const FrameContext& context);

	Result<void> LateUpdate(const FrameContext& context);

	Result<void> CommitChanges();

	void Clear();

private:
	friend class GameObject;

	struct ObjectHandle
	{
		Ursa::UInt32 index = std::numeric_limits<Ursa::UInt32>::max();
		Ursa::UInt32 generation = 0;

		explicit operator bool() const
		{
			return index != std::numeric_limits<Ursa::UInt32>::max();
		}

		bool operator==(const ObjectHandle& other) const
		{
			return index == other.index && generation == other.generation;
		}

		bool operator!=(const ObjectHandle& other) const
		{
			return !(*this == other);
		}
	};

	struct ObjectSlot
	{
		Ursa::UInt32 generation = 1;
		GameObject* object = nullptr;
		bool alive = false;
		bool pendingCreate = false;
		bool pendingDestroy = false;
		ObjectHandle pendingParent;
		bool hasPendingParent = false;
	};

	struct ComponentSlot
	{
		Ursa::UInt32 generation = 1;
		ResourceId persistentId;
		ObjectHandle owner;
		Component* object = nullptr;
		bool enabled = true;
		bool awakeCalled = false;
		bool enableCalled = false;
		bool started = false;
		bool wasActive = false;
		bool pendingAdd = false;
		bool pendingRemoval = false;
		Ursa::UInt64 availableFrame = 0;
	};

	enum class Phase
	{
		Update,
		Fixed,
		Late
	};

	Result<ComponentId> AddComponentOwned(const GameObject::Ptr& object, std::unique_ptr<Component> component);

	Result<Component*> AttachComponent(GameObject& owner, std::unique_ptr<Component> component);

	ObjectHandle HandleOf(const GameObject* object) const;

	Result<ObjectHandle> RegisterGameObject(const GameObject::Ptr& object, bool pendingCreate);

	Result<void> AttachGameObjectNow(const GameObject::Ptr& object, GameObject::Ptr parent, bool deferred = false);

	bool Contains(ObjectHandle object) const;

	void RemoveComponentNow(ComponentId id);

	void DestroyObjectNow(ObjectHandle object);

	Result<void> SetParentNow(ObjectHandle object, ObjectHandle parent);

	void MarkPendingDestroy(ObjectHandle object);

	Result<void> RunPhase(Phase phase, const FrameContext& context);

	Result<void> SynchronizeLifecycle(const FrameContext& context);

	bool IsGameObjectActive(const GameObject* object) const;

	Result<void> InitializeAttachedComponent(ComponentId id);

	void CollectComponents(ObjectHandle object, Vector<ComponentId>& out) const;

	void CollectRendering(ObjectHandle object, SceneRenderData& out) const;

	Result<SceneRenderData> CollectWithCamera(const Mat4& viewProjection, Vec3 cameraPosition) const;

	void DestroySubtree(ObjectHandle object);

	Vector<ObjectSlot> objects;
	Vector<GameObject::Ptr> rootObjects;
	Vector<Ursa::UInt32> freeObjectIndices;
	Vector<ComponentSlot> components;
	Vector<Ursa::UInt32> freeComponentIndices;
	Ursa::UInt64 frameSerial = 0;

	struct RuntimePreparation
	{
		std::function<Result<void>()> callback;
		bool preparing = false;
	};

	std::shared_ptr<RuntimePreparation> runtimePreparation;

	Result<void> PrepareRuntime();

	FrameContext runtimeFrameContext;
	bool hasRuntimeFrame = false;
	UInt32 attachmentLifecycleDepth = 0;
	bool inPhase = false;
	bool inLifecycle = false;
	Vector<std::function<Result<void>()>> commands;
	ComponentId activeCamera;
	Color ambientColor{1.0f, 1.0f, 1.0f, 1.0f};
	Ursa::Float32 ambientIntensity = 0.1f;
};
} // namespace Ursa
