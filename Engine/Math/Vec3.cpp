#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <cmath>
#include <stdexcept>

namespace Ursa
{
Vec3 operator-(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Ursa::Float32 Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 Normalized(Vec3 value)
{
	if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
	{
		throw std::invalid_argument("Cannot normalize a non-finite vector");
	}
	const double length = std::hypot(std::hypot(static_cast<double>(value.x), static_cast<double>(value.y)),
	                                 static_cast<double>(value.z));
	if (length <= 0.000001)
	{
		throw std::invalid_argument("Cannot normalize a zero-length vector");
	}
	return {static_cast<Ursa::Float32>(static_cast<double>(value.x) / length),
	        static_cast<Ursa::Float32>(static_cast<double>(value.y) / length),
	        static_cast<Ursa::Float32>(static_cast<double>(value.z) / length)};
}
} // namespace Ursa
