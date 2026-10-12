#pragma once

#include <Math/Vec3.hpp>

namespace Ursa
{
struct Bounds
{
	Vec3 minimum{};
	Vec3 maximum{};
	bool empty = true;
};
} // namespace Ursa
