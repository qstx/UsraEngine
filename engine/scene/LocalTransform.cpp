#include <scene/LocalTransform.hpp>

namespace ursa {
Mat4 LocalTransform::matrix() const {
    return Mat4::translation(position) * Mat4::rotation(rotation) * Mat4::scale(scale);
}
} // namespace ursa
