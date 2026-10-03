#pragma once
#include <Math/Vec3.hpp>

namespace Ursa
{
struct Quat
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;

	static Quat FromAxisAngle(Vec3 axis, float radians);
};
} // namespace Ursa
