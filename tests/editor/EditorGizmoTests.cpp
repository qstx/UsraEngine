#include <scene/EditorGizmo.hpp>
#include <core/StringInterop.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa;

template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
    return std::move(value.value);
}

void done(Result<void> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
}

void expect(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

bool near(const Mat4 &a, const Mat4 &b) {
    for (size_t index = 0; index < a.values.size(); ++index)
        if (std::abs(a.values[index] - b.values[index]) > 0.001f)
            return false;
    return true;
}
} // namespace

int main() {
    try {
        Scene scene;
        const auto parent = take(scene.createNode());
        const auto child = take(scene.createNode(parent));
        LocalTransform parentLocal;
        parentLocal.position = {3, 1, -2};
        parentLocal.rotation = Quat::fromAxisAngle({0, 0, 1}, 0.5f);
        parentLocal.scale = {2, 1, 3};
        done(scene.setLocalTransform(parent, parentLocal));
        LocalTransform desired;
        desired.position = {1, 2, 3};
        desired.rotation = Quat::fromAxisAngle({0, 0, 1}, 0.35f);
        desired.scale = {1.5f, 0.8f, 1.2f};
        const Mat4 world = take(scene.worldTransform(parent)) * desired.matrix();
        done(editor::applyGizmoWorldTransform(scene, child, world));
        expect(near(take(scene.worldTransform(child)), world), "Nested gizmo world transform did not round-trip");
        expect(near(take(scene.localTransform(child)).matrix(), desired.matrix()),
               "Gizmo did not convert the parent transform to local TRS");

        const Mat4 previous = take(scene.worldTransform(child));
        Mat4 shear = desired.matrix();
        shear.at(0, 1) += 0.3f;
        expect(!editor::applyGizmoWorldTransform(scene, child, take(scene.worldTransform(parent)) * shear),
               "Sheared gizmo transform should fail");
        expect(near(take(scene.worldTransform(child)), previous), "Rejected shear changed the node");
        Mat4 singular = world;
        for (int row = 0; row < 3; ++row)
            singular.at(row, 0) = 0;
        expect(!editor::applyGizmoWorldTransform(scene, child, singular), "Singular gizmo transform should fail");
        Mat4 nonfinite = world;
        nonfinite.at(0, 3) = NAN;
        expect(!editor::applyGizmoWorldTransform(scene, child, nonfinite), "Non-finite gizmo transform should fail");
        expect(near(take(scene.worldTransform(child)), previous), "Invalid gizmo transform changed the node");
        std::cout << "Editor gizmo transform test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
