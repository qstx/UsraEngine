#pragma once

#include <Math/Camera.hpp>
#include <Scene/ViewportLayout.hpp>

namespace Ursa::Editor
{
bool IsSceneViewCameraValid(const Camera& camera);
bool RecoverSceneViewCamera(Camera& camera);
void AdvanceSceneViewMovementSpeed(bool moving, Float32 deltaTime, Float32& targetSpeed, Float32& currentSpeed,
	                               bool accelerationEnabled = true, Float32 accelerationSpeed = 0.8f,
	                               bool easingEnabled = true, Float32 easingDuration = 0.4f);

void OrbitSceneView(Camera& camera, ViewportPoint mouseDelta);

void PanSceneView(Camera& camera, ViewportPoint mouseDelta, Float32 viewportHeight);

void ZoomSceneView(Camera& camera, Float32 wheelDelta, ViewportPoint mousePosition, ViewportPoint viewportSize);
void ZoomSceneViewByDrag(Camera& camera, Float32 zoomDelta, Float32 startZoom, Float32 zoomSpeed,
                         Float32& totalMotion);
void ZoomSceneViewByWheel(Camera& camera, Float32 wheelDelta, ViewportPoint mousePosition, ViewportPoint viewportSize);

void FlythroughSceneView(Camera& camera, ViewportPoint mouseDelta, ViewportPoint movement, Float32 verticalMovement,
	                     Float32 deltaTime, Float32 speed, bool fast, Float32 fastSpeedModifier = 5.f);

void MoveSceneView(Camera& camera, ViewportPoint movement, Float32 deltaTime, Float32 speed, bool fast,
	               bool panVerticalInOrthographic = false, Float32 fastSpeedModifier = 5.f);

void FocusSceneView(Camera& camera, Vec3 target, Float32 radius, Float32 aspect);
} // namespace Ursa::Editor
