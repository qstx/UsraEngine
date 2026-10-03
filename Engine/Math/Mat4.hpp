#pragma once
#include <Core/Containers.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <array>

namespace Ursa
{
// Column-major matrices multiply column vectors. World coordinates are right-handed with +Y up.
struct Mat4
{
	Array<float, 16> values{};

	float& At(int row, int column);

	float At(int row, int column) const;

	static Mat4 Identity();

	static Mat4 Translation(Vec3 position);

	static Mat4 Scale(Vec3 size);

	static Mat4 Rotation(Quat rotation);

	static Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);

	static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);

	Mat4 InverseAffine() const;
};

Mat4 operator*(const Mat4& a, const Mat4& b);
} // namespace Ursa
