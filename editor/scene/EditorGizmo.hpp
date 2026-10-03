#pragma once

#include <scene/Scene.hpp>
#include <math/Camera.hpp>

namespace ursa::editor {
enum class GizmoOperation { Translate, Rotate, Scale };

struct ViewportRect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

// Converts a manipulated world matrix into the selected node's local TRS.
// Shear, reflection, singular scale, and non-finite values are rejected.
Result<void> applyGizmoWorldTransform(Scene &scene, NodeId node, const Mat4 &world);

// Call during an ImGui frame after the viewport image has been placed.
Result<bool> drawGizmo(Scene &scene, NodeId node, const Camera &camera, ViewportRect rect,
                       GizmoOperation operation);
} // namespace ursa::editor
