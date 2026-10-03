#include <math/Camera.hpp>

namespace ursa {
Mat4 Camera::view() const {
    return Mat4::lookAt(position, target, up);
}

Mat4 Camera::projection(float aspect) const {
    return Mat4::perspective(verticalFovRadians, aspect, nearPlane, farPlane);
}

Mat4 Camera::viewProjection(float aspect) const {
    return projection(aspect) * view();
}
} // namespace ursa
