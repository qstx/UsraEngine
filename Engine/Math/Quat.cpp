#include <Core/Types.hpp>
#include <Math/Quat.hpp>
#include <cmath>

namespace Ursa
{
Quat Quat::FromAxisAngle(Vec3 axis, Ursa::Float32 radians)
{
	const Vec3 unit = Normalized(axis);
	const Ursa::Float32 half = radians * 0.5f;
	const Ursa::Float32 sine = std::sin(half);
	return {unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half)};
}
} // namespace Ursa
