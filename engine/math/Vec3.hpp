#pragma once

namespace ursa {
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator-(Vec3 a, Vec3 b);

float dot(Vec3 a, Vec3 b);

Vec3 cross(Vec3 a, Vec3 b);

Vec3 normalized(Vec3 value);
} // namespace ursa
