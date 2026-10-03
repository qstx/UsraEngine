#pragma once
#include <core/Containers.hpp>
#include <math/Quat.hpp>
#include <math/Vec3.hpp>
#include <array>

namespace ursa {
// Column-major matrices multiply column vectors. World coordinates are right-handed with +Y up.
struct Mat4 {
    Array<float, 16> values{};

    float &at(int row, int column);

    float at(int row, int column) const;

    static Mat4 identity();

    static Mat4 translation(Vec3 position);

    static Mat4 scale(Vec3 size);

    static Mat4 rotation(Quat rotation);

    static Mat4 perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);

    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);

    Mat4 inverseAffine() const;
};

Mat4 operator*(const Mat4 &a, const Mat4 &b);
} // namespace ursa
