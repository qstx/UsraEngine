#pragma once

#include <Core/Types.hpp>

namespace Ursa
{
struct Vec3
{
	Ursa::Float32 x = 0.0f;
	Ursa::Float32 y = 0.0f;
	Ursa::Float32 z = 0.0f;
};

Vec3 operator-(Vec3 a, Vec3 b);

Ursa::Float32 Dot(Vec3 a, Vec3 b);

Vec3 Cross(Vec3 a, Vec3 b);

Vec3 Normalized(Vec3 value);
} // namespace Ursa
