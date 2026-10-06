#pragma once
#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>

namespace Ursa
{
struct Camera
{
	Vec3 position{0.0f, 12.0f, 18.0f};
	Vec3 target{};
	Vec3 up{0.0f, 1.0f, 0.0f};
	Ursa::Float32 verticalFovRadians = 0.78539816f;
	Ursa::Float32 nearPlane = 0.1f;
	Ursa::Float32 farPlane = 100.0f;

	Mat4 View() const;

	Mat4 Projection(Ursa::Float32 aspect) const;

	Mat4 ViewProjection(Ursa::Float32 aspect) const;
};
} // namespace Ursa
