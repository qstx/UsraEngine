#include <Core/Types.hpp>
#include <Math/Mat4.hpp>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace Ursa
{
Ursa::Float32& Mat4::At(int row, int column)
{
	return values[static_cast<Ursa::Size>(column * 4 + row)];
}

Ursa::Float32 Mat4::At(int row, int column) const
{
	return values[static_cast<Ursa::Size>(column * 4 + row)];
}

Mat4 Mat4::Identity()
{
	Mat4 result;
	for (int i = 0; i < 4; ++i)
	{
		result.At(i, i) = 1.0f;
	}
	return result;
}

Mat4 Mat4::Translation(Vec3 position)
{
	Mat4 result = Identity();
	result.At(0, 3) = position.x;
	result.At(1, 3) = position.y;
	result.At(2, 3) = position.z;
	return result;
}

Mat4 Mat4::Scale(Vec3 size)
{
	Mat4 result = Identity();
	result.At(0, 0) = size.x;
	result.At(1, 1) = size.y;
	result.At(2, 2) = size.z;
	return result;
}

Mat4 Mat4::Rotation(Quat rotation)
{
	const Ursa::Float32 length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z +
	                                       rotation.w * rotation.w);
	if (length <= 0.000001f)
	{
		throw std::invalid_argument("Cannot create a matrix from a zero-length quaternion");
	}
	const Ursa::Float32 x = rotation.x / length;
	const Ursa::Float32 y = rotation.y / length;
	const Ursa::Float32 z = rotation.z / length;
	const Ursa::Float32 w = rotation.w / length;
	Mat4 result = Identity();
	result.At(0, 0) = 1.0f - 2.0f * (y * y + z * z);
	result.At(0, 1) = 2.0f * (x * y - z * w);
	result.At(0, 2) = 2.0f * (x * z + y * w);
	result.At(1, 0) = 2.0f * (x * y + z * w);
	result.At(1, 1) = 1.0f - 2.0f * (x * x + z * z);
	result.At(1, 2) = 2.0f * (y * z - x * w);
	result.At(2, 0) = 2.0f * (x * z - y * w);
	result.At(2, 1) = 2.0f * (y * z + x * w);
	result.At(2, 2) = 1.0f - 2.0f * (x * x + y * y);
	return result;
}

Mat4 Mat4::Perspective(Ursa::Float32 verticalFovRadians, Ursa::Float32 aspect, Ursa::Float32 nearPlane,
                       Ursa::Float32 farPlane)
{
	if (verticalFovRadians <= 0.0f || verticalFovRadians >= 3.14159265f || aspect <= 0.0f || nearPlane <= 0.0f ||
	    farPlane <= nearPlane)
	{
		throw std::invalid_argument("Invalid perspective camera parameters");
	}
	const Ursa::Float32 focal = 1.0f / std::tan(verticalFovRadians * 0.5f);
	Mat4 result;
	result.At(0, 0) = focal / aspect;
	result.At(1, 1) = focal;
	result.At(2, 2) = farPlane / (nearPlane - farPlane);
	result.At(2, 3) = farPlane * nearPlane / (nearPlane - farPlane);
	result.At(3, 2) = -1.0f;
	return result;
}

Mat4 Mat4::LookAt(Vec3 eye, Vec3 target, Vec3 up)
{
	const Vec3 forward = Normalized(target - eye);
	const Vec3 right = Normalized(Cross(forward, up));
	const Vec3 cameraUp = Cross(right, forward);
	Mat4 result = Identity();
	result.At(0, 0) = right.x;
	result.At(0, 1) = right.y;
	result.At(0, 2) = right.z;
	result.At(0, 3) = -Dot(right, eye);
	result.At(1, 0) = cameraUp.x;
	result.At(1, 1) = cameraUp.y;
	result.At(1, 2) = cameraUp.z;
	result.At(1, 3) = -Dot(cameraUp, eye);
	result.At(2, 0) = -forward.x;
	result.At(2, 1) = -forward.y;
	result.At(2, 2) = -forward.z;
	result.At(2, 3) = Dot(forward, eye);
	return result;
}

Mat4 Mat4::InverseAffine() const
{
	if (std::abs(At(3, 0)) > 0.000001f || std::abs(At(3, 1)) > 0.000001f || std::abs(At(3, 2)) > 0.000001f ||
	    std::abs(At(3, 3) - 1.0f) > 0.000001f)
	{
		throw std::invalid_argument("Matrix is not affine");
	}

	const Ursa::Float32 a = At(0, 0), b = At(0, 1), c = At(0, 2);
	const Ursa::Float32 d = At(1, 0), e = At(1, 1), f = At(1, 2);
	const Ursa::Float32 g = At(2, 0), h = At(2, 1), i = At(2, 2);
	const Ursa::Float32 determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (std::abs(determinant) <= 0.000001f)
	{
		throw std::invalid_argument("Affine matrix is not invertible");
	}

	const Ursa::Float32 inverseDeterminant = 1.0f / determinant;
	Mat4 result = Identity();
	result.At(0, 0) = (e * i - f * h) * inverseDeterminant;
	result.At(0, 1) = (c * h - b * i) * inverseDeterminant;
	result.At(0, 2) = (b * f - c * e) * inverseDeterminant;
	result.At(1, 0) = (f * g - d * i) * inverseDeterminant;
	result.At(1, 1) = (a * i - c * g) * inverseDeterminant;
	result.At(1, 2) = (c * d - a * f) * inverseDeterminant;
	result.At(2, 0) = (d * h - e * g) * inverseDeterminant;
	result.At(2, 1) = (b * g - a * h) * inverseDeterminant;
	result.At(2, 2) = (a * e - b * d) * inverseDeterminant;
	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 3; ++column)
		{
			result.At(row, 3) -= result.At(row, column) * At(column, 3);
		}
	}
	return result;
}

Mat4 operator*(const Mat4& a, const Mat4& b)
{
	Mat4 result;
	for (int column = 0; column < 4; ++column)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int k = 0; k < 4; ++k)
			{
				result.At(row, column) += a.At(row, k) * b.At(k, column);
			}
		}
	}
	return result;
}
} // namespace Ursa
