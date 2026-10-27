#pragma once

#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa::Editor
{
// Editor rotations use the Z-X-Y Euler convention used by the Transform Inspector.
inline Vec3 UnityEulerAngles(Quat rotation)
{
	const Float32 lengthSquared =
	    rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
	if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
	{
		return {};
	}
	const Float32 inverseLength = 1.f / std::sqrt(lengthSquared);
	rotation.x *= inverseLength;
	rotation.y *= inverseLength;
	rotation.z *= inverseLength;
	rotation.w *= inverseLength;
	const Float32 r13 = 2.f * (rotation.x * rotation.z + rotation.w * rotation.y);
	const Float32 r23 = 2.f * (rotation.y * rotation.z - rotation.w * rotation.x);
	const Float32 r21 = 2.f * (rotation.x * rotation.y + rotation.w * rotation.z);
	const Float32 r22 = 1.f - 2.f * (rotation.x * rotation.x + rotation.z * rotation.z);
	const Float32 r31 = 2.f * (rotation.x * rotation.z - rotation.w * rotation.y);
	const Float32 r11 = 1.f - 2.f * (rotation.y * rotation.y + rotation.z * rotation.z);
	const Float32 x = std::asin(std::clamp(-r23, -1.f, 1.f));
	const Float32 cosineX = std::cos(x);
	const Float32 y = std::abs(cosineX) > 1.0e-5f
	                      ? std::atan2(r13, 1.f - 2.f * (rotation.x * rotation.x + rotation.y * rotation.y))
	                      : std::atan2(-r31, r11);
	const Float32 z = std::abs(cosineX) > 1.0e-5f ? std::atan2(r21, r22) : 0.f;
	const auto positiveDegrees = [](Float32 radians)
	{
		const Float32 degrees = std::fmod(radians * 57.295779513082320876f, 360.f);
		return degrees < 0.f ? degrees + 360.f : degrees;
	};
	return {positiveDegrees(x), positiveDegrees(y), positiveDegrees(z)};
}

inline Quat UnityEulerQuaternion(Vec3 degrees)
{
	constexpr Float32 degreesToRadians = 0.01745329251994329577f;
	const Float32 halfX = degrees.x * degreesToRadians * 0.5f;
	const Float32 halfY = degrees.y * degreesToRadians * 0.5f;
	const Float32 halfZ = degrees.z * degreesToRadians * 0.5f;
	const Float32 sx = std::sin(halfX);
	const Float32 cx = std::cos(halfX);
	const Float32 sy = std::sin(halfY);
	const Float32 cy = std::cos(halfY);
	const Float32 sz = std::sin(halfZ);
	const Float32 cz = std::cos(halfZ);
	return {cy * sx * cz + sy * cx * sz, sy * cx * cz - cy * sx * sz, cy * cx * sz - sy * sx * cz,
	        cy * cx * cz + sy * sx * sz};
}
} // namespace Ursa::Editor
