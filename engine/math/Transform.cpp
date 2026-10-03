#include <math/Transform.hpp>

namespace ursa {
Mat4 Transform::matrix() const {
    return Mat4::translation(position) * Mat4::scale(size);
}
} // namespace ursa
