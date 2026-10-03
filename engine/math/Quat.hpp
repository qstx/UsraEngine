#pragma once
#include <math/Vec3.hpp>

namespace ursa {
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static Quat fromAxisAngle(Vec3 axis, float radians);
};
} // namespace ursa
