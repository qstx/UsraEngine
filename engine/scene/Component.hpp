#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <window/Window.hpp>
#include <array>
#include <cstdint>
#include <limits>

namespace ursa {
class Scene;

struct NodeId;

struct ComponentId {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    explicit operator bool() const {
        return index != std::numeric_limits<uint32_t>::max();
    }
};

inline bool operator==(ComponentId a, ComponentId b) {
    return a.index == b.index && a.generation == b.generation;
}

inline bool operator!=(ComponentId a, ComponentId b) {
    return !(a == b);
}

struct FrameInput {
    Array<bool, 10> keys{};

    bool keyDown(Key key) const {
        return keys.at(static_cast<size_t>(key));
    }

    static FrameInput capture(const Window &window) {
        FrameInput input;
        for (size_t i = 0; i < input.keys.size(); ++i)
            input.keys[i] = window.keyDown(static_cast<Key>(i));
        return input;
    }
};

struct FrameContext {
    float deltaSeconds = 0.0f;
    float fixedDeltaSeconds = 0.0f;
    double elapsedSeconds = 0.0;
    uint64_t frameIndex = 0;
    Window *window = nullptr;
    FrameInput input;
};

class Component {
public:
    virtual ~Component() = default;

    Scene &scene() const {
        return *scene_;
    }

    NodeId node() const;

    ComponentId id() const {
        return id_;
    }

    virtual Result<void> onStart(const FrameContext &) {
        return {};
    }

    virtual Result<void> onUpdate(const FrameContext &) {
        return {};
    }

    virtual Result<void> onFixedUpdate(const FrameContext &) {
        return {};
    }

    virtual Result<void> onLateUpdate(const FrameContext &) {
        return {};
    }

    virtual void onRemove() {
    }

private:
    friend class Scene;
    Scene *scene_ = nullptr;
    uint32_t nodeIndex_ = 0;
    uint32_t nodeGeneration_ = 0;
    ComponentId id_;
};
} // namespace ursa
