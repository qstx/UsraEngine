#include <math/Camera.hpp>
#include <math/Quat.hpp>
#include <math/Transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expectNear(float actual, float expected) {
    if (std::abs(actual - expected) > 0.0001f)
        throw std::runtime_error("Matrix value differs from expected result");
}

void testTransform() {
    ursa::Transform transform;
    transform.position = {2.0f, 3.0f, 4.0f};
    transform.size = {2.0f, 4.0f, 6.0f};
    const auto matrix = transform.matrix();
    expectNear(matrix.at(0, 0), 2.0f);
    expectNear(matrix.at(1, 1), 4.0f);
    expectNear(matrix.at(2, 2), 6.0f);
    expectNear(matrix.at(0, 3), 2.0f);
    expectNear(matrix.at(1, 3), 3.0f);
    expectNear(matrix.at(2, 3), 4.0f);
}

void testCamera() {
    ursa::Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.target = {};
    const auto view = camera.view();
    expectNear(view.at(2, 3), -5.0f);
    const auto square = camera.projection(1.0f);
    const auto wide = camera.projection(2.0f);
    expectNear(wide.at(0, 0), square.at(0, 0) * 0.5f);
    expectNear(wide.at(1, 1), square.at(1, 1));
    const auto viewProjection = camera.viewProjection(1.0f);
    expectNear(viewProjection.at(3, 3), 5.0f);
}

void testHierarchyMatrix() {
    const auto parent = ursa::Mat4::translation({2.0f, 3.0f, 4.0f}) *
                        ursa::Mat4::rotation(ursa::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.57079633f));
    const auto child = ursa::Mat4::translation({1.0f, 0.0f, 0.0f}) * ursa::Mat4::scale({2.0f, 3.0f, 4.0f});
    const auto world = parent * child;
    expectNear(world.at(0, 3), 2.0f);
    expectNear(world.at(1, 3), 3.0f);
    expectNear(world.at(2, 3), 3.0f);
    const auto identity = world.inverseAffine() * world;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            expectNear(identity.at(row, column), row == column ? 1.0f : 0.0f);
}
} // namespace

int main() {
    try {
        testTransform();
        testCamera();
        testHierarchyMatrix();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
