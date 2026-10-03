#include <scene/EditorGizmo.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <exception>

namespace Ursa::Editor
{
namespace
{
float Length3(float x, float y, float z)
{
	return std::sqrt(x * x + y * y + z * z);
}

Quat RotationFromMatrix(const Mat4& matrix)
{
	Quat result;
	const float trace = matrix.At(0, 0) + matrix.At(1, 1) + matrix.At(2, 2);
	if (trace > 0.f)
	{
		const float s = std::sqrt(trace + 1.f) * 2.f;
		result = {(matrix.At(2, 1) - matrix.At(1, 2)) / s, (matrix.At(0, 2) - matrix.At(2, 0)) / s,
		          (matrix.At(1, 0) - matrix.At(0, 1)) / s, s / 4.f};
	}
	else if (matrix.At(0, 0) > matrix.At(1, 1) && matrix.At(0, 0) > matrix.At(2, 2))
	{
		const float s = std::sqrt(1.f + matrix.At(0, 0) - matrix.At(1, 1) - matrix.At(2, 2)) * 2.f;
		result = {s / 4.f, (matrix.At(0, 1) + matrix.At(1, 0)) / s, (matrix.At(0, 2) + matrix.At(2, 0)) / s,
		          (matrix.At(2, 1) - matrix.At(1, 2)) / s};
	}
	else if (matrix.At(1, 1) > matrix.At(2, 2))
	{
		const float s = std::sqrt(1.f + matrix.At(1, 1) - matrix.At(0, 0) - matrix.At(2, 2)) * 2.f;
		result = {(matrix.At(0, 1) + matrix.At(1, 0)) / s, s / 4.f, (matrix.At(1, 2) + matrix.At(2, 1)) / s,
		          (matrix.At(0, 2) - matrix.At(2, 0)) / s};
	}
	else
	{
		const float s = std::sqrt(1.f + matrix.At(2, 2) - matrix.At(0, 0) - matrix.At(1, 1)) * 2.f;
		result = {(matrix.At(0, 2) + matrix.At(2, 0)) / s, (matrix.At(1, 2) + matrix.At(2, 1)) / s, s / 4.f,
		          (matrix.At(1, 0) - matrix.At(0, 1)) / s};
	}
	return result;
}

Result<LocalTransform> Decompose(const Mat4& matrix)
{
	for (float value : matrix.values)
	{
		if (!std::isfinite(value))
		{
			return Result<LocalTransform>::Failure("Gizmo matrix contains a non-finite value");
		}
	}
	if (std::abs(matrix.At(3, 0)) > 0.0001f || std::abs(matrix.At(3, 1)) > 0.0001f ||
	    std::abs(matrix.At(3, 2)) > 0.0001f || std::abs(matrix.At(3, 3) - 1.f) > 0.0001f)
	{
		return Result<LocalTransform>::Failure("Gizmo matrix is not affine");
	}
	LocalTransform local;
	local.position = {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
	const float scale[3] = {Length3(matrix.At(0, 0), matrix.At(1, 0), matrix.At(2, 0)),
	                        Length3(matrix.At(0, 1), matrix.At(1, 1), matrix.At(2, 1)),
	                        Length3(matrix.At(0, 2), matrix.At(1, 2), matrix.At(2, 2))};
	for (float value : scale)
	{
		if (!std::isfinite(value) || value <= 0.00001f)
		{
			return Result<LocalTransform>::Failure("Gizmo scale is singular or invalid");
		}
	}
	local.scale = {scale[0], scale[1], scale[2]};
	Mat4 rotation = Mat4::Identity();
	for (int column = 0; column < 3; ++column)
	{
		for (int row = 0; row < 3; ++row)
		{
			rotation.At(row, column) = matrix.At(row, column) / scale[column];
		}
	}
	for (int a = 0; a < 3; ++a)
	{
		for (int b = a + 1; b < 3; ++b)
		{
			float dot = 0.f;
			for (int row = 0; row < 3; ++row)
			{
				dot += rotation.At(row, a) * rotation.At(row, b);
			}
			if (std::abs(dot) > 0.001f)
			{
				return Result<LocalTransform>::Failure("Gizmo transform contains shear");
			}
		}
	}
	const float determinant =
	    rotation.At(0, 0) * (rotation.At(1, 1) * rotation.At(2, 2) - rotation.At(1, 2) * rotation.At(2, 1)) -
	    rotation.At(0, 1) * (rotation.At(1, 0) * rotation.At(2, 2) - rotation.At(1, 2) * rotation.At(2, 0)) +
	    rotation.At(0, 2) * (rotation.At(1, 0) * rotation.At(2, 1) - rotation.At(1, 1) * rotation.At(2, 0));
	if (determinant < 0.999f || determinant > 1.001f)
	{
		return Result<LocalTransform>::Failure("Gizmo transform contains a reflection or invalid rotation");
	}
	local.rotation = RotationFromMatrix(rotation);
	const Mat4 rebuilt = local.Matrix();
	for (size_t i = 0; i < matrix.values.size(); ++i)
	{
		if (std::abs(rebuilt.values[i] - matrix.values[i]) > 0.001f * std::max(1.f, std::abs(matrix.values[i])))
		{
			return Result<LocalTransform>::Failure("Gizmo transform cannot be represented as local TRS");
		}
	}
	return Result<LocalTransform>::Success(local);
}
} // namespace

Result<void> ApplyGizmoWorldTransform(Scene& scene, NodeId node, const Mat4& world)
{
	if (!scene.Contains(node))
	{
		return Result<void>::Failure("Cannot manipulate an invalid node");
	}
	try
	{
		auto parent = scene.ParentOf(node);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		Mat4 localMatrix = world;
		if (parent.value)
		{
			auto parentWorld = scene.WorldTransform(parent.value);
			if (!parentWorld)
			{
				return Result<void>::Failure(parentWorld.error);
			}
			localMatrix = parentWorld.value.InverseAffine() * world;
		}
		auto local = Decompose(localMatrix);
		if (!local)
		{
			return Result<void>::Failure(local.error);
		}
		return scene.SetLocalTransform(node, local.value);
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(String("Cannot apply gizmo transform: ") + error.what());
	}
}

Result<bool> DrawGizmo(Scene& scene, NodeId node, const Camera& camera, ViewportRect rect, GizmoOperation operation)
{
	if (!scene.Contains(node))
	{
		return Result<bool>::Failure("Cannot draw gizmo for an invalid node");
	}
	if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
	    rect.width <= 0.f || rect.height <= 0.f)
	{
		return Result<bool>::Failure("Gizmo viewport rectangle is invalid");
	}
	try
	{
		auto world = scene.WorldTransform(node);
		if (!world)
		{
			return Result<bool>::Failure(world.error);
		}
		const Mat4 view = camera.View();
		const Mat4 projection = camera.Projection(rect.width / rect.height);
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(rect.x, rect.y, rect.width, rect.height);
		ImGuizmo::OPERATION selected = ImGuizmo::TRANSLATE;
		if (operation == GizmoOperation::rotate)
		{
			selected = ImGuizmo::ROTATE;
		}
		else if (operation == GizmoOperation::scale)
		{
			selected = ImGuizmo::SCALE;
		}
		if (!ImGuizmo::Manipulate(view.values.data(), projection.values.data(), selected, ImGuizmo::WORLD,
		                          world.value.values.data()))
		{
			return Result<bool>::Success(false);
		}
		auto applied = ApplyGizmoWorldTransform(scene, node, world.value);
		if (!applied)
		{
			return Result<bool>::Failure(applied.error);
		}
		return Result<bool>::Success(true);
	}
	catch (const std::exception& error)
	{
		return Result<bool>::Failure(String("Cannot draw gizmo: ") + error.what());
	}
}
} // namespace Ursa::Editor
