#include <Math/Transform.hpp>

namespace Ursa
{
Mat4 Transform::Matrix() const
{
	return Mat4::Translation(position) * Mat4::Scale(size);
}
} // namespace Ursa
