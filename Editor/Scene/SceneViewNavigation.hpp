#pragma once

#include <Math/Camera.hpp>
#include <Scene/ViewportLayout.hpp>

namespace Ursa::Editor
{
void OrbitSceneView(Camera& camera, ViewportPoint mouseDelta);

void PanSceneView(Camera& camera, ViewportPoint mouseDelta, Float32 viewportHeight);

void ZoomSceneView(Camera& camera, Float32 wheelDelta, ViewportPoint mousePosition, ViewportPoint viewportSize);
void ZoomSceneViewByWheel(Camera& camera, Float32 wheelDelta, ViewportPoint mousePosition, ViewportPoint viewportSize);

void FlythroughSceneView(Camera& camera, ViewportPoint mouseDelta, ViewportPoint movement, Float32 verticalMovement,
                         Float32 deltaTime, Float32 speed, bool fast);

void MoveSceneView(Camera& camera, ViewportPoint movement, Float32 deltaTime, Float32 speed, bool fast,
                   bool panVerticalInOrthographic = false);

void FocusSceneView(Camera& camera, Vec3 target, Float32 radius);
} // namespace Ursa::Editor
