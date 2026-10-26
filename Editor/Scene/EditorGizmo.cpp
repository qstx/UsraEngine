#include <Core/Types.hpp>
#include <Scene/EditorGizmo.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>

namespace Ursa::Editor
{
namespace
{
Vec3 Add(Vec3 left, Vec3 right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Scale(Vec3 value, Ursa::Float32 scalar)
{
	return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Ursa::Float32 Length3(Ursa::Float32 x, Ursa::Float32 y, Ursa::Float32 z)
{
	return std::sqrt(x * x + y * y + z * z);
}

Quat RotationFromMatrix(const Mat4& matrix)
{
	Quat result;
	const Ursa::Float32 trace = matrix.At(0, 0) + matrix.At(1, 1) + matrix.At(2, 2);
	if (trace > 0.f)
	{
		const Ursa::Float32 s = std::sqrt(trace + 1.f) * 2.f;
		result = {(matrix.At(2, 1) - matrix.At(1, 2)) / s, (matrix.At(0, 2) - matrix.At(2, 0)) / s,
		          (matrix.At(1, 0) - matrix.At(0, 1)) / s, s / 4.f};
	}
	else if (matrix.At(0, 0) > matrix.At(1, 1) && matrix.At(0, 0) > matrix.At(2, 2))
	{
		const Ursa::Float32 s = std::sqrt(1.f + matrix.At(0, 0) - matrix.At(1, 1) - matrix.At(2, 2)) * 2.f;
		result = {s / 4.f, (matrix.At(0, 1) + matrix.At(1, 0)) / s, (matrix.At(0, 2) + matrix.At(2, 0)) / s,
		          (matrix.At(2, 1) - matrix.At(1, 2)) / s};
	}
	else if (matrix.At(1, 1) > matrix.At(2, 2))
	{
		const Ursa::Float32 s = std::sqrt(1.f + matrix.At(1, 1) - matrix.At(0, 0) - matrix.At(2, 2)) * 2.f;
		result = {(matrix.At(0, 1) + matrix.At(1, 0)) / s, s / 4.f, (matrix.At(1, 2) + matrix.At(2, 1)) / s,
		          (matrix.At(0, 2) - matrix.At(2, 0)) / s};
	}
	else
	{
		const Ursa::Float32 s = std::sqrt(1.f + matrix.At(2, 2) - matrix.At(0, 0) - matrix.At(1, 1)) * 2.f;
		result = {(matrix.At(0, 2) + matrix.At(2, 0)) / s, (matrix.At(1, 2) + matrix.At(2, 1)) / s, s / 4.f,
		          (matrix.At(1, 0) - matrix.At(0, 1)) / s};
	}
	return result;
}

Result<LocalTransform> Decompose(const Mat4& matrix)
{
	for (Ursa::Float32 value : matrix.values)
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
	const Ursa::Float32 scale[3] = {Length3(matrix.At(0, 0), matrix.At(1, 0), matrix.At(2, 0)),
	                                Length3(matrix.At(0, 1), matrix.At(1, 1), matrix.At(2, 1)),
	                                Length3(matrix.At(0, 2), matrix.At(1, 2), matrix.At(2, 2))};
	for (Ursa::Float32 value : scale)
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
			Ursa::Float32 dot = 0.f;
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
	const Ursa::Float32 determinant =
	    rotation.At(0, 0) * (rotation.At(1, 1) * rotation.At(2, 2) - rotation.At(1, 2) * rotation.At(2, 1)) -
	    rotation.At(0, 1) * (rotation.At(1, 0) * rotation.At(2, 2) - rotation.At(1, 2) * rotation.At(2, 0)) +
	    rotation.At(0, 2) * (rotation.At(1, 0) * rotation.At(2, 1) - rotation.At(1, 1) * rotation.At(2, 0));
	if (determinant < 0.999f || determinant > 1.001f)
	{
		return Result<LocalTransform>::Failure("Gizmo transform contains a reflection or invalid rotation");
	}
	local.rotation = RotationFromMatrix(rotation);
	const Mat4 rebuilt = local.Matrix();
	for (Ursa::Size i = 0; i < matrix.values.size(); ++i)
	{
		if (std::abs(rebuilt.values[i] - matrix.values[i]) > 0.001f * std::max(1.f, std::abs(matrix.values[i])))
		{
			return Result<LocalTransform>::Failure("Gizmo transform cannot be represented as local TRS");
		}
	}
	return Result<LocalTransform>::Success(local);
}
} // namespace

Result<void> ApplyGizmoWorldTransform(Scene& scene, GameObject::Ptr node, const Mat4& world)
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

Result<bool> DrawGizmo(Scene& scene, const Vector<GameObject::Ptr>& nodes, GameObject::Ptr primary,
                       const Camera& camera, ViewportRect rect, GizmoOperation operation, GizmoPivotMode pivotMode,
                       GizmoSpace space, const GizmoSnapSettings& snapSettings, GizmoInteractionReport* interaction)
{
	if (nodes.empty() || !primary || !scene.Contains(primary))
	{
		return Result<bool>::Failure("Cannot draw gizmo without a valid selection");
	}
	if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
	    rect.width <= 0.f || rect.height <= 0.f)
	{
		return Result<bool>::Failure("Gizmo viewport rectangle is invalid");
	}
	try
	{
		struct SelectedTransform
		{
			GameObject::Ptr node;
			Mat4 world;
			LocalTransform local;
			Mat4 desiredWorld;
			Size depth = 0;
		};

		Vector<SelectedTransform> selectedTransforms;
		selectedTransforms.reserve(nodes.size());
		Mat4 primaryWorld;
		Vec3 fallbackCenter{};
		for (const auto& node : nodes)
		{
			if (!node || !scene.Contains(node))
			{
				continue;
			}
			auto world = scene.WorldTransform(node);
			auto local = scene.GetLocalTransform(node);
			if (!world || !local)
			{
				return Result<bool>::Failure(!world ? world.error : local.error);
			}
			SelectedTransform item;
			item.node = node;
			item.world = world.value;
			item.local = local.value;
			item.desiredWorld = world.value;
			auto parent = scene.ParentOf(node);
			if (!parent)
			{
				return Result<bool>::Failure(parent.error);
			}
			for (auto ancestor = parent.value; ancestor;)
			{
				++item.depth;
				auto next = scene.ParentOf(ancestor);
				if (!next)
				{
					return Result<bool>::Failure(next.error);
				}
				ancestor = next.value;
			}
			fallbackCenter = Add(fallbackCenter, {world.value.At(0, 3), world.value.At(1, 3), world.value.At(2, 3)});
			if (node == primary)
			{
				primaryWorld = world.value;
			}
			selectedTransforms.push_back(std::move(item));
		}
		if (selectedTransforms.empty())
		{
			return Result<bool>::Failure("The selected objects are no longer available in the scene");
		}
		if (std::none_of(selectedTransforms.begin(), selectedTransforms.end(),
		                 [&](const SelectedTransform& item) { return item.node == primary; }))
		{
			return Result<bool>::Failure("The active object is not part of the gizmo selection");
		}
		fallbackCenter = Scale(fallbackCenter, 1.f / static_cast<Float32>(selectedTransforms.size()));

		Mat4 manipulatedWorld = primaryWorld;
		if (pivotMode == GizmoPivotMode::Center)
		{
			auto renderData = scene.CollectPreviewRenderData(camera, rect.width / rect.height);
			if (renderData)
			{
				Bounds bounds;
				for (const auto& item : selectedTransforms)
				{
					const auto id = scene.PersistentId(item.node);
					if (!id)
					{
						continue;
					}
					for (const auto& object : renderData.value.objects)
					{
						if (object.objectId != id.value || object.worldBounds.empty)
						{
							continue;
						}
						if (bounds.empty)
						{
							bounds = object.worldBounds;
						}
						else
						{
							bounds.minimum = {std::min(bounds.minimum.x, object.worldBounds.minimum.x),
							                  std::min(bounds.minimum.y, object.worldBounds.minimum.y),
							                  std::min(bounds.minimum.z, object.worldBounds.minimum.z)};
							bounds.maximum = {std::max(bounds.maximum.x, object.worldBounds.maximum.x),
							                  std::max(bounds.maximum.y, object.worldBounds.maximum.y),
							                  std::max(bounds.maximum.z, object.worldBounds.maximum.z)};
						}
					}
				}
				if (bounds.empty)
				{
					manipulatedWorld.At(0, 3) = fallbackCenter.x;
					manipulatedWorld.At(1, 3) = fallbackCenter.y;
					manipulatedWorld.At(2, 3) = fallbackCenter.z;
				}
				else
				{
					manipulatedWorld.At(0, 3) = (bounds.minimum.x + bounds.maximum.x) * 0.5f;
					manipulatedWorld.At(1, 3) = (bounds.minimum.y + bounds.maximum.y) * 0.5f;
					manipulatedWorld.At(2, 3) = (bounds.minimum.z + bounds.maximum.z) * 0.5f;
				}
			}
		}
		const Mat4 pivotWorld = manipulatedWorld;
		const Mat4 view = camera.View();
		const Mat4 projection = camera.Projection(rect.width / rect.height);
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(rect.x, rect.y, rect.width, rect.height);
		ImGuizmo::OPERATION selected = ImGuizmo::TRANSLATE;
		if (operation == GizmoOperation::Rotate)
		{
			selected = ImGuizmo::ROTATE;
		}
		else if (operation == GizmoOperation::Scale)
		{
			selected = ImGuizmo::SCALE;
		}
		const ImGuizmo::MODE mode = space == GizmoSpace::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
		std::array<Float32, 3> snapValues{};
		const Float32* snap = nullptr;
		if (snapSettings.enabled || ImGui::GetIO().KeyCtrl)
		{
			if (operation == GizmoOperation::Translate)
			{
				snapValues = {snapSettings.moveX, snapSettings.moveY, snapSettings.moveZ};
			}
			else
			{
				const Float32 increment =
				    operation == GizmoOperation::Rotate ? snapSettings.rotation : snapSettings.scale;
				snapValues = {increment, increment, increment};
			}
			snap = snapValues.data();
		}
		const bool matrixChanged = ImGuizmo::Manipulate(view.values.data(), projection.values.data(), selected, mode,
		                                                manipulatedWorld.values.data(), nullptr, snap);
		if (interaction)
		{
			interaction->isOver = ImGuizmo::IsOver();
			interaction->isUsing = ImGuizmo::IsUsing();
			interaction->matrixChanged = matrixChanged;
		}
		if (!matrixChanged)
		{
			return Result<bool>::Success(false);
		}
		const Mat4 delta = manipulatedWorld * pivotWorld.InverseAffine();
		for (auto& item : selectedTransforms)
		{
			item.desiredWorld = delta * item.world;
		}
		std::sort(selectedTransforms.begin(), selectedTransforms.end(),
		          [](const SelectedTransform& left, const SelectedTransform& right)
		          { return left.depth < right.depth; });
		Size appliedCount = 0;
		for (; appliedCount < selectedTransforms.size(); ++appliedCount)
		{
			auto applied = ApplyGizmoWorldTransform(scene, selectedTransforms[appliedCount].node,
			                                        selectedTransforms[appliedCount].desiredWorld);
			if (!applied)
			{
				while (appliedCount > 0)
				{
					--appliedCount;
					scene.SetLocalTransform(selectedTransforms[appliedCount].node,
					                        selectedTransforms[appliedCount].local);
				}
				return Result<bool>::Failure(applied.error);
			}
		}
		return Result<bool>::Success(true);
	}
	catch (const std::exception& error)
	{
		return Result<bool>::Failure(String("Cannot draw gizmo: ") + error.what());
	}
}
} // namespace Ursa::Editor
