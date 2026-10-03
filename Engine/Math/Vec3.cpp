#include <Math/Vec3.hpp>
#include <cmath>
#include <stdexcept>

namespace Ursa
{
Vec3 operator-(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 Normalized(Vec3 value)
{
	const float length = std::sqrt(Dot(value, value));
	if (length <= 0.000001f)
	{
		throw std::invalid_argument("Cannot normalize a zero-length vector");
	}
	return {value.x / length, value.y / length, value.z / length};
}
} // namespace Ursa
