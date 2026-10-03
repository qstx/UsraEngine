#pragma once

#include <serialization/ComponentRegistry.hpp>
#include <imgui.h>

namespace ursa::editor {
class EditorHierarchy final {
public:
    Result<void> select(const Scene &scene, NodeId node);

    void clearSelection();

    Result<NodeId> selectedNode(const Scene &scene) const;

    ResourceId selectedId() const {
        return selected_;
    }

    // Returns true if a persistent scene property changed.
    Result<bool> draw(Scene &scene, const ComponentRegistry &registry);

private:
    Result<bool> drawNode(Scene &scene, NodeId node, const ComponentRegistry &registry);

    ResourceId selected_;
};
} // namespace ursa::editor
