#pragma once
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

namespace Ursa
{
struct Quat
{
	Ursa::Float32 x = 0.0f;
	Ursa::Float32 y = 0.0f;
	Ursa::Float32 z = 0.0f;
	Ursa::Float32 w = 1.0f;

	static Quat FromAxisAngle(Vec3 axis, Ursa::Float32 radians);
};
} // namespace Ursa
