#pragma once

namespace Ursa
{
struct Vec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

Vec3 operator-(Vec3 a, Vec3 b);

float Dot(Vec3 a, Vec3 b);

Vec3 Cross(Vec3 a, Vec3 b);

Vec3 Normalized(Vec3 value);
} // namespace Ursa
