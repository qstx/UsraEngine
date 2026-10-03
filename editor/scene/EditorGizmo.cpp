#include <scene/EditorGizmo.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <exception>

namespace ursa::editor {
namespace {
float length3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

Quat rotationFromMatrix(const Mat4 &matrix) {
    Quat result;
    const float trace = matrix.at(0, 0) + matrix.at(1, 1) + matrix.at(2, 2);
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        result = {(matrix.at(2, 1) - matrix.at(1, 2)) / s,
                  (matrix.at(0, 2) - matrix.at(2, 0)) / s,
                  (matrix.at(1, 0) - matrix.at(0, 1)) / s, s / 4.f};
    } else if (matrix.at(0, 0) > matrix.at(1, 1) && matrix.at(0, 0) > matrix.at(2, 2)) {
        const float s = std::sqrt(1.f + matrix.at(0, 0) - matrix.at(1, 1) - matrix.at(2, 2)) * 2.f;
        result = {s / 4.f, (matrix.at(0, 1) + matrix.at(1, 0)) / s,
                  (matrix.at(0, 2) + matrix.at(2, 0)) / s,
                  (matrix.at(2, 1) - matrix.at(1, 2)) / s};
    } else if (matrix.at(1, 1) > matrix.at(2, 2)) {
        const float s = std::sqrt(1.f + matrix.at(1, 1) - matrix.at(0, 0) - matrix.at(2, 2)) * 2.f;
        result = {(matrix.at(0, 1) + matrix.at(1, 0)) / s, s / 4.f,
                  (matrix.at(1, 2) + matrix.at(2, 1)) / s,
                  (matrix.at(0, 2) - matrix.at(2, 0)) / s};
    } else {
        const float s = std::sqrt(1.f + matrix.at(2, 2) - matrix.at(0, 0) - matrix.at(1, 1)) * 2.f;
        result = {(matrix.at(0, 2) + matrix.at(2, 0)) / s,
                  (matrix.at(1, 2) + matrix.at(2, 1)) / s, s / 4.f,
                  (matrix.at(1, 0) - matrix.at(0, 1)) / s};
    }
    return result;
}

Result<LocalTransform> decompose(const Mat4 &matrix) {
    for (float value : matrix.values)
        if (!std::isfinite(value))
            return Result<LocalTransform>::failure("Gizmo matrix contains a non-finite value");
    if (std::abs(matrix.at(3, 0)) > 0.0001f || std::abs(matrix.at(3, 1)) > 0.0001f ||
        std::abs(matrix.at(3, 2)) > 0.0001f || std::abs(matrix.at(3, 3) - 1.f) > 0.0001f)
        return Result<LocalTransform>::failure("Gizmo matrix is not affine");
    LocalTransform local;
    local.position = {matrix.at(0, 3), matrix.at(1, 3), matrix.at(2, 3)};
    const float scale[3] = {
        length3(matrix.at(0, 0), matrix.at(1, 0), matrix.at(2, 0)),
        length3(matrix.at(0, 1), matrix.at(1, 1), matrix.at(2, 1)),
        length3(matrix.at(0, 2), matrix.at(1, 2), matrix.at(2, 2))};
    for (float value : scale)
        if (!std::isfinite(value) || value <= 0.00001f)
            return Result<LocalTransform>::failure("Gizmo scale is singular or invalid");
    local.scale = {scale[0], scale[1], scale[2]};
    Mat4 rotation = Mat4::identity();
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            rotation.at(row, column) = matrix.at(row, column) / scale[column];
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b) {
            float dot = 0.f;
            for (int row = 0; row < 3; ++row)
                dot += rotation.at(row, a) * rotation.at(row, b);
            if (std::abs(dot) > 0.001f)
                return Result<LocalTransform>::failure("Gizmo transform contains shear");
        }
    const float determinant = rotation.at(0, 0) * (rotation.at(1, 1) * rotation.at(2, 2) -
                                                    rotation.at(1, 2) * rotation.at(2, 1)) -
                              rotation.at(0, 1) * (rotation.at(1, 0) * rotation.at(2, 2) -
                                                    rotation.at(1, 2) * rotation.at(2, 0)) +
                              rotation.at(0, 2) * (rotation.at(1, 0) * rotation.at(2, 1) -
                                                    rotation.at(1, 1) * rotation.at(2, 0));
    if (determinant < 0.999f || determinant > 1.001f)
        return Result<LocalTransform>::failure("Gizmo transform contains a reflection or invalid rotation");
    local.rotation = rotationFromMatrix(rotation);
    const Mat4 rebuilt = local.matrix();
    for (size_t i = 0; i < matrix.values.size(); ++i)
        if (std::abs(rebuilt.values[i] - matrix.values[i]) >
            0.001f * std::max(1.f, std::abs(matrix.values[i])))
            return Result<LocalTransform>::failure("Gizmo transform cannot be represented as local TRS");
    return Result<LocalTransform>::success(local);
}
} // namespace

Result<void> applyGizmoWorldTransform(Scene &scene, NodeId node, const Mat4 &world) {
    if (!scene.contains(node))
        return Result<void>::failure("Cannot manipulate an invalid node");
    try {
        auto parent = scene.parentOf(node);
        if (!parent)
            return Result<void>::failure(parent.error);
        Mat4 localMatrix = world;
        if (parent.value) {
            auto parentWorld = scene.worldTransform(parent.value);
            if (!parentWorld)
                return Result<void>::failure(parentWorld.error);
            localMatrix = parentWorld.value.inverseAffine() * world;
        }
        auto local = decompose(localMatrix);
        if (!local)
            return Result<void>::failure(local.error);
        return scene.setLocalTransform(node, local.value);
    } catch (const std::exception &error) {
        return Result<void>::failure(String("Cannot apply gizmo transform: ") + error.what());
    }
}

Result<bool> drawGizmo(Scene &scene, NodeId node, const Camera &camera, ViewportRect rect,
                       GizmoOperation operation) {
    if (!scene.contains(node))
        return Result<bool>::failure("Cannot draw gizmo for an invalid node");
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
        !std::isfinite(rect.height) || rect.width <= 0.f || rect.height <= 0.f)
        return Result<bool>::failure("Gizmo viewport rectangle is invalid");
    try {
        auto world = scene.worldTransform(node);
        if (!world)
            return Result<bool>::failure(world.error);
        const Mat4 view = camera.view();
        const Mat4 projection = camera.projection(rect.width / rect.height);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(rect.x, rect.y, rect.width, rect.height);
        ImGuizmo::OPERATION selected = ImGuizmo::TRANSLATE;
        if (operation == GizmoOperation::Rotate)
            selected = ImGuizmo::ROTATE;
        else if (operation == GizmoOperation::Scale)
            selected = ImGuizmo::SCALE;
        if (!ImGuizmo::Manipulate(view.values.data(), projection.values.data(), selected, ImGuizmo::WORLD,
                                  world.value.values.data()))
            return Result<bool>::success(false);
        auto applied = applyGizmoWorldTransform(scene, node, world.value);
        if (!applied)
            return Result<bool>::failure(applied.error);
        return Result<bool>::success(true);
    } catch (const std::exception &error) {
        return Result<bool>::failure(String("Cannot draw gizmo: ") + error.what());
    }
}
} // namespace ursa::editor
