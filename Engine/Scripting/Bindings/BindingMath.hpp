#pragma once

#include <Material/Color.hpp>
#include <Math/Mat4.hpp>

namespace Ursa::Scripting::Bindings
{
template <class W> Vec3 ReadVector3(const W& value)
{
	return {value.F_X, value.F_Y, value.F_Z};
}

template <class W> W WriteVector3(Vec3 value)
{
	W result{};
	result.F_X = value.x;
	result.F_Y = value.y;
	result.F_Z = value.z;
	return result;
}

template <class W> Quat ReadQuaternion(const W& value)
{
	return {value.F_X, value.F_Y, value.F_Z, value.F_W};
}

template <class W> W WriteQuaternion(Quat value)
{
	W result{};
	result.F_X = value.x;
	result.F_Y = value.y;
	result.F_Z = value.z;
	result.F_W = value.w;
	return result;
}

template <class W> Color ReadColor(const W& value)
{
	return {value.F_R, value.F_G, value.F_B, value.F_A};
}

template <class W> W WriteColor(Color value)
{
	W result{};
	result.F_R = value.red;
	result.F_G = value.green;
	result.F_B = value.blue;
	result.F_A = value.alpha;
	return result;
}

template <class W> Mat4 ReadMatrix(const W& value)
{
	Mat4 result;
	result.values = {value.F_M00, value.F_M10, value.F_M20, value.F_M30, value.F_M01, value.F_M11,
	                 value.F_M21, value.F_M31, value.F_M02, value.F_M12, value.F_M22, value.F_M32,
	                 value.F_M03, value.F_M13, value.F_M23, value.F_M33};
	return result;
}

template <class W> W WriteMatrix(const Mat4& value)
{
	W result{};
	result.F_M00 = value.At(0, 0);
	result.F_M10 = value.At(1, 0);
	result.F_M20 = value.At(2, 0);
	result.F_M30 = value.At(3, 0);
	result.F_M01 = value.At(0, 1);
	result.F_M11 = value.At(1, 1);
	result.F_M21 = value.At(2, 1);
	result.F_M31 = value.At(3, 1);
	result.F_M02 = value.At(0, 2);
	result.F_M12 = value.At(1, 2);
	result.F_M22 = value.At(2, 2);
	result.F_M32 = value.At(3, 2);
	result.F_M03 = value.At(0, 3);
	result.F_M13 = value.At(1, 3);
	result.F_M23 = value.At(2, 3);
	result.F_M33 = value.At(3, 3);
	return result;
}
} // namespace Ursa::Scripting::Bindings
