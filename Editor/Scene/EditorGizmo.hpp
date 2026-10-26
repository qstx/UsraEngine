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

enum class GizmoPivotMode
{
	Pivot,
	Center
};

enum class GizmoSpace
{
	Global,
	Local
};

struct GizmoSnapSettings
{
	Float32 moveX = 1.f;
	Float32 moveY = 1.f;
	Float32 moveZ = 1.f;
	Float32 rotation = 15.f;
	Float32 scale = 0.1f;
	bool enabled = false;
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
Result<bool> DrawGizmo(Scene& scene, const Vector<GameObject::Ptr>& nodes, GameObject::Ptr primary,
                       const Camera& camera, ViewportRect rect, GizmoOperation operation,
                       GizmoPivotMode pivotMode = GizmoPivotMode::Pivot, GizmoSpace space = GizmoSpace::Local,
                       const GizmoSnapSettings& snapSettings = {}, GizmoInteractionReport* interaction = nullptr);
} // namespace Ursa::Editor
