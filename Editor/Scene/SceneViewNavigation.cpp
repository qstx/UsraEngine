#include <Scene/SceneViewNavigation.hpp>

#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
namespace
{
constexpr Float32 OrbitRadiansPerPixel = 0.01f;
constexpr Float32 MinimumDistance = 0.05f;
constexpr Float32 MaximumDistance = 1'000'000.f;
constexpr Float32 Pi = 3.14159265358979323846f;

Vec3 Add(Vec3 left, Vec3 right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Scale(Vec3 value, Float32 scalar)
{
	return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Float32 Length(Vec3 value)
{
	return std::sqrt(Dot(value, value));
}

void SetOrbitPosition(Camera& camera, Float32 yaw, Float32 pitch, Float32 distance)
{
	const Float32 horizontal = std::cos(pitch) * distance;
	camera.position =
	    Add(camera.target, {std::sin(yaw) * horizontal, std::sin(pitch) * distance, std::cos(yaw) * horizontal});
}
} // namespace

void OrbitSceneView(Camera& camera, ViewportPoint mouseDelta)
{
	const Vec3 offset = camera.position - camera.target;
	const Float32 distance = Length(offset);
	if (!std::isfinite(distance) || distance <= MinimumDistance || !std::isfinite(mouseDelta.x) ||
	    !std::isfinite(mouseDelta.y))
	{
		return;
	}
	const Float32 yaw = std::atan2(offset.x, offset.z) - mouseDelta.x * OrbitRadiansPerPixel;
	const Float32 pitch =
	    std::clamp(std::asin(std::clamp(offset.y / distance, -1.f, 1.f)) + mouseDelta.y * OrbitRadiansPerPixel,
	               -Pi * 0.495f, Pi * 0.495f);
	SetOrbitPosition(camera, yaw, pitch, distance);
}

void PanSceneView(Camera& camera, ViewportPoint mouseDelta, Float32 viewportHeight)
{
	const Vec3 viewDirection = camera.target - camera.position;
	const Float32 distance = Length(viewDirection);
	if (!std::isfinite(distance) || distance <= MinimumDistance || !std::isfinite(viewportHeight) ||
	    viewportHeight <= 0.f || !std::isfinite(mouseDelta.x) || !std::isfinite(mouseDelta.y))
	{
		return;
	}
	const Vec3 forward = Scale(viewDirection, 1.f / distance);
	const Vec3 rightVector = Cross(forward, camera.up);
	const Float32 rightLength = Length(rightVector);
	if (!std::isfinite(rightLength) || rightLength <= 1.0e-5f)
	{
		return;
	}
	const Vec3 right = Scale(rightVector, 1.f / rightLength);
	const Vec3 screenUp = Normalized(Cross(right, forward));
	const Float32 halfHeight =
	    camera.orthographic ? camera.orthographicSize : distance * std::tan(camera.verticalFovRadians * 0.5f);
	const Float32 unitsPerPixel = 2.f * halfHeight / viewportHeight;
	const Vec3 shift = Add(Scale(right, -mouseDelta.x * unitsPerPixel), Scale(screenUp, mouseDelta.y * unitsPerPixel));
	camera.position = Add(camera.position, shift);
	camera.target = Add(camera.target, shift);
}

void ZoomSceneView(Camera& camera, Float32 wheelDelta, ViewportPoint mousePosition, ViewportPoint viewportSize)
{
	const Vec3 offset = camera.position - camera.target;
	const Float32 distance = Length(offset);
	if (!std::isfinite(distance) || distance <= 0.f || !std::isfinite(wheelDelta) || !std::isfinite(mousePosition.x) ||
	    !std::isfinite(mousePosition.y) || !std::isfinite(viewportSize.x) || !std::isfinite(viewportSize.y) ||
	    viewportSize.x <= 0.f || viewportSize.y <= 0.f || !std::isfinite(camera.verticalFovRadians) ||
	    camera.verticalFovRadians <= 0.f || camera.verticalFovRadians >= Pi)
	{
		return;
	}
	const Vec3 forward = Scale(camera.target - camera.position, 1.f / distance);
	const Vec3 rightVector = Cross(forward, camera.up);
	const Float32 rightLength = Length(rightVector);
	if (!std::isfinite(rightLength) || rightLength <= 1.0e-5f)
	{
		return;
	}
	const Vec3 right = Scale(rightVector, 1.f / rightLength);
	const Vec3 screenUp = Normalized(Cross(right, forward));
	if (camera.orthographic)
	{
		if (!std::isfinite(camera.orthographicSize) || camera.orthographicSize <= 0.f)
		{
			return;
		}
		const Float32 nextSize =
		    std::clamp(camera.orthographicSize * std::exp(-wheelDelta * 0.12f), MinimumDistance, MaximumDistance);
		const Float32 sizeRatio = nextSize / camera.orthographicSize;
		const Float32 normalizedX = mousePosition.x / viewportSize.x * 2.f - 1.f;
		const Float32 normalizedY = 1.f - mousePosition.y / viewportSize.y * 2.f;
		const Vec3 pointerOffset =
		    Add(Scale(right, normalizedX * camera.orthographicSize * viewportSize.x / viewportSize.y),
		        Scale(screenUp, normalizedY * camera.orthographicSize));
		const Vec3 targetShift = Scale(pointerOffset, 1.f - sizeRatio);
		camera.target = Add(camera.target, targetShift);
		camera.position = Add(camera.position, targetShift);
		camera.orthographicSize = nextSize;
		return;
	}
	const Float32 nextDistance = std::clamp(distance * std::exp(-wheelDelta * 0.12f), MinimumDistance, MaximumDistance);
	const Float32 distanceRatio = nextDistance / distance;
	const Float32 normalizedX = mousePosition.x / viewportSize.x * 2.f - 1.f;
	const Float32 normalizedY = 1.f - mousePosition.y / viewportSize.y * 2.f;
	const Float32 halfHeight = distance * std::tan(camera.verticalFovRadians * 0.5f);
	const Vec3 pointerOffset = Add(Scale(right, normalizedX * halfHeight * viewportSize.x / viewportSize.y),
	                               Scale(screenUp, normalizedY * halfHeight));
	const Vec3 targetShift = Scale(pointerOffset, 1.f - distanceRatio);
	camera.target = Add(camera.target, targetShift);
	camera.position = Add(camera.target, Scale(offset, distanceRatio));
}

void FlythroughSceneView(Camera& camera, ViewportPoint mouseDelta, ViewportPoint movement, Float32 verticalMovement,
                         Float32 deltaTime, Float32 speed, bool fast)
{
	const Vec3 view = camera.target - camera.position;
	Float32 distance = Length(view);
	if (!std::isfinite(distance) || distance <= MinimumDistance || !std::isfinite(mouseDelta.x) ||
	    !std::isfinite(mouseDelta.y) || !std::isfinite(movement.x) || !std::isfinite(movement.y) ||
	    !std::isfinite(verticalMovement) || !std::isfinite(deltaTime) || deltaTime <= 0.f || !std::isfinite(speed) ||
	    speed <= 0.f)
	{
		return;
	}

	Vec3 forward = Scale(view, 1.f / distance);
	if (camera.orthographic)
	{
		if (!std::isfinite(camera.orthographicSize) || !std::isfinite(camera.verticalFovRadians) ||
		    camera.verticalFovRadians <= 0.f || camera.verticalFovRadians >= Pi)
		{
			return;
		}
		distance = std::max(camera.orthographicSize / std::tan(camera.verticalFovRadians * 0.5f), MinimumDistance);
		camera.position = Add(camera.target, Scale(forward, -distance));
		camera.orthographic = false;
	}
	Float32 yaw = std::atan2(forward.x, forward.z) - mouseDelta.x * OrbitRadiansPerPixel;
	Float32 pitch = std::clamp(std::asin(std::clamp(forward.y, -1.f, 1.f)) - mouseDelta.y * OrbitRadiansPerPixel,
	                           -Pi * 0.495f, Pi * 0.495f);
	const Float32 horizontal = std::cos(pitch);
	forward = {std::sin(yaw) * horizontal, std::sin(pitch), std::cos(yaw) * horizontal};
	const Vec3 rightVector = Cross(forward, camera.up);
	const Float32 rightLength = Length(rightVector);
	if (!std::isfinite(rightLength) || rightLength <= 1.0e-5f)
	{
		return;
	}
	const Vec3 right = Scale(rightVector, 1.f / rightLength);
	Vec3 direction = Add(Add(Scale(right, movement.x), Scale(forward, movement.y)), {0.f, verticalMovement, 0.f});
	const Float32 directionLength = Length(direction);
	if (directionLength > 1.f)
	{
		direction = Scale(direction, 1.f / directionLength);
	}
	const Float32 moveScale = speed * deltaTime * (fast ? 3.f : 1.f);
	camera.position = Add(camera.position, Scale(direction, moveScale));
	camera.target = Add(camera.position, Scale(forward, distance));
}

void MoveSceneView(Camera& camera, ViewportPoint movement, Float32 deltaTime, Float32 speed, bool fast,
                   bool panVerticalInOrthographic)
{
	const Vec3 view = camera.target - camera.position;
	const Float32 distance = Length(view);
	if (!std::isfinite(distance) || distance <= MinimumDistance || !std::isfinite(movement.x) ||
	    !std::isfinite(movement.y) || !std::isfinite(deltaTime) || deltaTime <= 0.f || !std::isfinite(speed) ||
	    speed <= 0.f)
	{
		return;
	}
	const Vec3 forward = Scale(view, 1.f / distance);
	const Vec3 rightVector = Cross(forward, camera.up);
	const Float32 rightLength = Length(rightVector);
	if (!std::isfinite(rightLength) || rightLength <= 1.0e-5f)
	{
		return;
	}
	const Vec3 right = Scale(rightVector, 1.f / rightLength);
	const Vec3 vertical = camera.orthographic && panVerticalInOrthographic ? Normalized(Cross(right, forward)) : forward;
	Vec3 direction = Add(Scale(right, movement.x), Scale(vertical, movement.y));
	const Float32 directionLength = Length(direction);
	if (directionLength > 1.f)
	{
		direction = Scale(direction, 1.f / directionLength);
	}
	const Float32 moveScale = speed * deltaTime * (fast ? 3.f : 1.f);
	const Vec3 shift = Scale(direction, moveScale);
	camera.position = Add(camera.position, shift);
	camera.target = Add(camera.target, shift);
}

void FocusSceneView(Camera& camera, Vec3 target, Float32 radius)
{
	if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) || !std::isfinite(radius) ||
	    !std::isfinite(camera.verticalFovRadians) || camera.verticalFovRadians <= 0.f ||
	    camera.verticalFovRadians >= Pi)
	{
		return;
	}
	const Vec3 currentOffset = camera.position - camera.target;
	Float32 distance = Length(currentOffset);
	const Vec3 direction = distance > 1.0e-5f ? Scale(currentOffset, 1.f / distance) : Vec3{0.f, 0.5f, 0.8660254f};
	if (camera.orthographic)
	{
		camera.orthographicSize = std::clamp(std::max(std::abs(radius), 0.1f) * 1.2f, MinimumDistance, MaximumDistance);
		distance = std::max(distance, 1.f);
		camera.target = target;
		camera.position = Add(target, Scale(direction, distance));
		return;
	}
	distance = std::max(std::abs(radius), 0.1f) / std::sin(camera.verticalFovRadians * 0.5f) * 1.2f;
	distance = std::clamp(distance, MinimumDistance, MaximumDistance);
	camera.target = target;
	camera.position = Add(target, Scale(direction, distance));
}
} // namespace Ursa::Editor
