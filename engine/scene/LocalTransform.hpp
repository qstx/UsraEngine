#pragma once
#include <core/Containers.hpp>
#include <math/Mat4.hpp>
#include <math/Quat.hpp>
#include <math/Vec3.hpp>

namespace Ursa
{
struct LocalTransform
{
	Vec3 position{};
	Quat rotation{};
	Vec3 scale{1.0f, 1.0f, 1.0f};

	Mat4 Matrix() const;
};
} // namespace Ursa
