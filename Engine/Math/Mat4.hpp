#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>

namespace Ursa
{
// Column-major matrices multiply column vectors. World coordinates are right-handed with +Y up.
struct Mat4
{
	Array<Ursa::Float32, 16> values{};

	Ursa::Float32& At(int row, int column);

	Ursa::Float32 At(int row, int column) const;

	static Mat4 Identity();

	static Mat4 Translation(Vec3 position);

	static Mat4 Scale(Vec3 size);

	static Mat4 Rotation(Quat rotation);

	static Mat4 Perspective(Ursa::Float32 verticalFovRadians, Ursa::Float32 aspect, Ursa::Float32 nearPlane,
	                        Ursa::Float32 farPlane);

	static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);

	Mat4 InverseAffine() const;
};

Mat4 operator*(const Mat4& a, const Mat4& b);
} // namespace Ursa
