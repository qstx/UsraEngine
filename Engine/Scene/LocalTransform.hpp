#pragma once
#include <Core/Containers.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>

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
