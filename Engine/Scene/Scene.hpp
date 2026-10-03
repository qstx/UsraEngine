#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Scene/LocalTransform.hpp>
#include <Scene/Component.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/ResourceId.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Ursa
{
struct NodeId
{
	uint32_t index = std::numeric_limits<uint32_t>::max();
	uint32_t generation = 0;

	explicit operator bool() const
	{
		return index != std::numeric_limits<uint32_t>::max();
	}
};

inline bool operator==(NodeId a, NodeId b)
{
	return a.index == b.index && a.generation == b.generation;
}

inline bool operator!=(NodeId a, NodeId b)
{
	return !(a == b);
}

class Scene final
{
public:
	Scene() = default;

	~Scene();

	Scene(const Scene&) = delete;

	Scene& operator=(const Scene&) = delete;

	Result<NodeId> CreateNode(NodeId parent = {});

	Result<void> DestroyNode(NodeId node);

	Result<void> SetParent(NodeId node, NodeId parent = {});

	bool Contains(NodeId node) const;

	Result<NodeId> ParentOf(NodeId node) const;

	Result<Vector<NodeId>> ChildrenOf(NodeId node) const;

	Vector<NodeId> RootNodes() const;

	Result<ResourceId> NodePersistentId(NodeId node) const;

	Result<String> NodeLabel(NodeId node) const;

	Result<void> SetNodeLabel(NodeId node, String label);

	Result<void> AssignNodePersistentId(NodeId node, ResourceId id);

	Result<NodeId> NodeByPersistentId(ResourceId id) const;

	Result<void> ReorderNode(NodeId node, size_t index);

	Result<void> SetLocalTransform(NodeId node, LocalTransform transform);

	Result<LocalTransform> GetLocalTransform(NodeId node) const;

	Result<Mat4> WorldTransform(NodeId node) const;

	Result<void> SetEnabled(NodeId node, bool enabled);

	Result<bool> IsEnabled(NodeId node) const;

	Result<bool> IsActive(NodeId node) const;

	template <class T, class... Args> Result<ComponentId> AddComponent(NodeId node, Args&&... args)
	{
		static_assert(std::is_base_of<Component, T>::value, "Scene components must derive from Component");
		return AddComponentOwned(node, std::make_unique<T>(std::forward<Args>(args)...));
	}

	template <class T> T* FindComponent(NodeId node) const
	{
		if (!Contains(node))
		{
			return nullptr;
		}
		for (ComponentId id : nodes[node.index].components)
		{
			if (ContainsComponent(id))
			{
				if (auto* component = dynamic_cast<T*>(components[id.index].object.get()))
				{
					return component;
				}
			}
		}
		return nullptr;
	}

	template <class T> Vector<T*> FindComponents(NodeId node) const
	{
		Vector<T*> found;
		if (!Contains(node))
		{
			return found;
		}
		for (ComponentId id : nodes[node.index].components)
		{
			if (ContainsComponent(id))
			{
				if (auto* component = dynamic_cast<T*>(components[id.index].object.get()))
				{
					found.push_back(component);
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

	Result<NodeId> ComponentNode(ComponentId id) const;

	Result<Vector<ComponentId>> ComponentsOf(NodeId node) const;

	Component* GetComponent(ComponentId id) const;

	Result<ResourceId> ComponentPersistentId(ComponentId id) const;

	Result<void> AssignComponentPersistentId(ComponentId component, ResourceId id);

	Result<ComponentId> ComponentByPersistentId(ResourceId id) const;

	Result<void> SetComponentNode(ComponentId component, NodeId node);

	Result<void> ReorderComponent(ComponentId component, size_t index);

	Result<void> SetActiveCamera(ComponentId id);

	ComponentId ActiveCamera() const
	{
		return activeCamera;
	}

	Result<void> SetAmbientLight(Color color, float intensity);

	Color AmbientLightColor() const
	{
		return ambientColor;
	}

	float AmbientLightIntensity() const
	{
		return ambientIntensity;
	}

	Result<SceneRenderData> CollectRenderData(float aspect) const;

	// Uses a caller-owned camera without invoking component lifecycle callbacks.
	Result<SceneRenderData> CollectPreviewRenderData(const Camera& camera, float aspect) const;

	void BeginFrame();

	Result<void> Update(const FrameContext& context);

	Result<void> FixedUpdate(const FrameContext& context);

	Result<void> LateUpdate(const FrameContext& context);

	Result<void> CommitChanges();

	void Clear();

private:
	struct NodeSlot
	{
		uint32_t generation = 1;
		ResourceId persistentId;
		String label;
		bool alive = false;
		bool pendingCreate = false;
		bool pendingDestroy = false;
		NodeId parent;
		NodeId pendingParent;
		bool hasPendingParent = false;
		Vector<NodeId> children;
		Vector<ComponentId> components;
		LocalTransform local;
		bool enabled = true;
		mutable Mat4 world = Mat4::Identity();
		mutable bool worldDirty = true;
	};

	struct ComponentSlot
	{
		uint32_t generation = 1;
		ResourceId persistentId;
		NodeId node;
		std::unique_ptr<Component> object;
		bool enabled = true;
		bool started = false;
		bool pendingAdd = false;
		bool pendingRemoval = false;
		uint64_t availableFrame = 0;
	};

	enum class Phase
	{
		update,
		fixed,
		late
	};

	Result<ComponentId> AddComponentOwned(NodeId node, std::unique_ptr<Component> object);

	void RemoveComponentNow(ComponentId id);

	void DestroyNodeNow(NodeId node);

	Result<void> SetParentNow(NodeId node, NodeId parent);

	void MarkPendingDestroy(NodeId node);

	Result<void> RunPhase(Phase phase, const FrameContext& context);

	void CollectComponents(NodeId node, Vector<ComponentId>& out) const;

	void CollectRendering(NodeId node, SceneRenderData& out) const;

	Result<SceneRenderData> CollectWithCamera(const Mat4& viewProjection, Vec3 cameraPosition) const;

	void DestroySubtree(NodeId node);

	void MarkWorldDirty(NodeId node);

	Mat4 ComputeWorld(NodeId node) const;

	Vector<NodeSlot> nodes;
	Vector<NodeId> rootOrder;
	Vector<uint32_t> freeIndices;
	Vector<ComponentSlot> components;
	Vector<uint32_t> freeComponentIndices;
	uint64_t frameSerial = 0;
	bool inPhase = false;
	Vector<std::function<Result<void>()>> commands;
	ComponentId activeCamera;
	Color ambientColor{1.0f, 1.0f, 1.0f, 1.0f};
	float ambientIntensity = 0.1f;
};
} // namespace Ursa
