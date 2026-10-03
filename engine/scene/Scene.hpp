#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <scene/LocalTransform.hpp>
#include <scene/Component.hpp>
#include <scene/RenderingComponents.hpp>
#include <serialization/ResourceId.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ursa {
struct NodeId {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    explicit operator bool() const {
        return index != std::numeric_limits<uint32_t>::max();
    }
};

inline bool operator==(NodeId a, NodeId b) {
    return a.index == b.index && a.generation == b.generation;
}

inline bool operator!=(NodeId a, NodeId b) {
    return !(a == b);
}

class Scene final {
public:
    Scene() = default;

    ~Scene();

    Scene(const Scene &) = delete;

    Scene &operator=(const Scene &) = delete;

    Result<NodeId> createNode(NodeId parent = {});

    Result<void> destroyNode(NodeId node);

    Result<void> setParent(NodeId node, NodeId parent = {});

    bool contains(NodeId node) const;

    Result<NodeId> parentOf(NodeId node) const;

    Result<Vector<NodeId>> childrenOf(NodeId node) const;

    Vector<NodeId> rootNodes() const;

    Result<ResourceId> nodePersistentId(NodeId node) const;

    Result<String> nodeLabel(NodeId node) const;

    Result<void> setNodeLabel(NodeId node, String label);

    Result<void> assignNodePersistentId(NodeId node, ResourceId id);

    Result<NodeId> nodeByPersistentId(ResourceId id) const;

    Result<void> reorderNode(NodeId node, size_t index);

    Result<void> setLocalTransform(NodeId node, LocalTransform transform);

    Result<LocalTransform> localTransform(NodeId node) const;

    Result<Mat4> worldTransform(NodeId node) const;

    Result<void> setEnabled(NodeId node, bool enabled);

    Result<bool> isEnabled(NodeId node) const;

    Result<bool> isActive(NodeId node) const;

    template <class T, class... Args> Result<ComponentId> addComponent(NodeId node, Args &&...args) {
        static_assert(std::is_base_of<Component, T>::value, "Scene components must derive from Component");
        return addComponentOwned(node, std::make_unique<T>(std::forward<Args>(args)...));
    }

    template <class T> T *findComponent(NodeId node) const {
        if (!contains(node))
            return nullptr;
        for (ComponentId id : nodes_[node.index].components)
            if (containsComponent(id))
                if (auto *component = dynamic_cast<T *>(components_[id.index].object.get()))
                    return component;
        return nullptr;
    }

    template <class T> Vector<T *> findComponents(NodeId node) const {
        Vector<T *> found;
        if (!contains(node))
            return found;
        for (ComponentId id : nodes_[node.index].components)
            if (containsComponent(id))
                if (auto *component = dynamic_cast<T *>(components_[id.index].object.get()))
                    found.push_back(component);
        return found;
    }

    bool containsComponent(ComponentId id) const;

    Result<void> removeComponent(ComponentId id);

    Result<void> setComponentEnabled(ComponentId id, bool enabled);

    Result<bool> isComponentActive(ComponentId id) const;

    Result<bool> isComponentEnabled(ComponentId id) const;

    Result<NodeId> componentNode(ComponentId id) const;

    Result<Vector<ComponentId>> componentsOf(NodeId node) const;

    Component *component(ComponentId id) const;

    Result<ResourceId> componentPersistentId(ComponentId id) const;

    Result<void> assignComponentPersistentId(ComponentId component, ResourceId id);

    Result<ComponentId> componentByPersistentId(ResourceId id) const;

    Result<void> setComponentNode(ComponentId component, NodeId node);

    Result<void> reorderComponent(ComponentId component, size_t index);

    Result<void> setActiveCamera(ComponentId id);

    ComponentId activeCamera() const {
        return activeCamera_;
    }

    Result<void> setAmbientLight(Color color, float intensity);

    Color ambientLightColor() const {
        return ambientColor_;
    }

    float ambientLightIntensity() const {
        return ambientIntensity_;
    }

    Result<SceneRenderData> collectRenderData(float aspect) const;

    // Uses a caller-owned camera without invoking component lifecycle callbacks.
    Result<SceneRenderData> collectPreviewRenderData(const Camera &camera, float aspect) const;

    void beginFrame();

    Result<void> update(const FrameContext &context);

    Result<void> fixedUpdate(const FrameContext &context);

    Result<void> lateUpdate(const FrameContext &context);

    Result<void> commitChanges();

    void clear();

private:
    struct NodeSlot {
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
        mutable Mat4 world = Mat4::identity();
        mutable bool worldDirty = true;
    };

    struct ComponentSlot {
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

    enum class Phase { Update, Fixed, Late };

    Result<ComponentId> addComponentOwned(NodeId node, std::unique_ptr<Component> object);

    void removeComponentNow(ComponentId id);

    void destroyNodeNow(NodeId node);

    Result<void> setParentNow(NodeId node, NodeId parent);

    void markPendingDestroy(NodeId node);

    Result<void> runPhase(Phase phase, const FrameContext &context);

    void collectComponents(NodeId node, Vector<ComponentId> &out) const;

    void collectRendering(NodeId node, SceneRenderData &out) const;

    Result<SceneRenderData> collectWithCamera(const Mat4 &viewProjection, Vec3 cameraPosition) const;

    void destroySubtree(NodeId node);

    void markWorldDirty(NodeId node);

    Mat4 computeWorld(NodeId node) const;

    Vector<NodeSlot> nodes_;
    Vector<NodeId> rootOrder_;
    Vector<uint32_t> freeIndices_;
    Vector<ComponentSlot> components_;
    Vector<uint32_t> freeComponentIndices_;
    uint64_t frameSerial_ = 0;
    bool inPhase_ = false;
    Vector<std::function<Result<void>()>> commands_;
    ComponentId activeCamera_;
    Color ambientColor_{1.0f, 1.0f, 1.0f, 1.0f};
    float ambientIntensity_ = 0.1f;
};
} // namespace ursa
