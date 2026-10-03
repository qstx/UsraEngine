#pragma once
#include <math/Mat4.hpp>
#include <math/Vec3.hpp>

namespace Ursa
{
struct Transform
{
	Vec3 position{};
	Vec3 size{1.0f, 1.0f, 1.0f};

	Mat4 Matrix() const;
};
} // namespace Ursa
