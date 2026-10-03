#include <math/Quat.hpp>
#include <cmath>

namespace ursa {
Quat Quat::fromAxisAngle(Vec3 axis, float radians) {
    const Vec3 unit = normalized(axis);
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return {unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half)};
}
} // namespace ursa
