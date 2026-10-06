#include <Core/Types.hpp>
#include <Math/Camera.hpp>

namespace Ursa
{
Mat4 Camera::View() const
{
	return Mat4::LookAt(position, target, up);
}

Mat4 Camera::Projection(Ursa::Float32 aspect) const
{
	return Mat4::Perspective(verticalFovRadians, aspect, nearPlane, farPlane);
}

Mat4 Camera::ViewProjection(Ursa::Float32 aspect) const
{
	return Projection(aspect) * View();
}
} // namespace Ursa
