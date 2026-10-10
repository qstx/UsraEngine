#pragma once

#include <Core/Types.hpp>
#include <Scene/Scene.hpp>
#include <Math/Camera.hpp>
#include <Scene/ViewportLayout.hpp>

namespace Ursa::Editor
{
enum class GizmoOperation
{
	Translate,
	Rotate,
	Scale
};

struct GizmoInteractionReport
{
	bool isOver = false;
	bool isUsing = false;
	bool matrixChanged = false;
};

// Converts a manipulated world matrix into the selected node's local TRS.
// Shear, reflection, singular scale, and non-finite values are rejected.
Result<void> ApplyGizmoWorldTransform(Scene& scene, GameObject::Ptr node, const Mat4& world);

// Call during an ImGui frame after the viewport image has been placed.
Result<bool> DrawGizmo(Scene& scene, GameObject::Ptr node, const Camera& camera, ViewportRect rect,
                       GizmoOperation operation, GizmoInteractionReport* interaction = nullptr);
} // namespace Ursa::Editor
