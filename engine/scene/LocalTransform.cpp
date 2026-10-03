#include <scene/LocalTransform.hpp>

namespace Ursa
{
Mat4 LocalTransform::Matrix() const
{
	return Mat4::Translation(position) * Mat4::Rotation(rotation) * Mat4::Scale(scale);
}
} // namespace Ursa
