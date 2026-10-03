#pragma once
#include <math/Mat4.hpp>
#include <math/Vec3.hpp>

namespace ursa {
struct Camera {
    Vec3 position{0.0f, 12.0f, 18.0f};
    Vec3 target{};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float verticalFovRadians = 0.78539816f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    Mat4 view() const;

    Mat4 projection(float aspect) const;

    Mat4 viewProjection(float aspect) const;
};
} // namespace ursa
