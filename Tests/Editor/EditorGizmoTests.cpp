#include <Scene/EditorGizmo.hpp>
#include <Core/StringInterop.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

bool near(const Mat4& a, const Mat4& b)
{
	for (size_t index = 0; index < a.values.size(); ++index)
	{
		if (std::abs(a.values[index] - b.values[index]) > 0.001f)
		{
			return false;
		}
	}
	return true;
}
} // namespace

int main()
{
	try
	{
		Scene scene;
		const auto parent = Take(scene.CreateNode());
		const auto child = Take(scene.CreateNode(parent));
		LocalTransform parentLocal;
		parentLocal.position = {3, 1, -2};
		parentLocal.rotation = Quat::FromAxisAngle({0, 0, 1}, 0.5f);
		parentLocal.scale = {2, 1, 3};
		done(scene.SetLocalTransform(parent, parentLocal));
		LocalTransform desired;
		desired.position = {1, 2, 3};
		desired.rotation = Quat::FromAxisAngle({0, 0, 1}, 0.35f);
		desired.scale = {1.5f, 0.8f, 1.2f};
		const Mat4 world = Take(scene.WorldTransform(parent)) * desired.Matrix();
		done(Editor::ApplyGizmoWorldTransform(scene, child, world));
		Expect(near(Take(scene.WorldTransform(child)), world), "Nested gizmo world transform did not round-trip");
		Expect(near(Take(scene.GetLocalTransform(child)).Matrix(), desired.Matrix()),
		       "Gizmo did not convert the parent transform to local TRS");

		const Mat4 previous = Take(scene.WorldTransform(child));
		Mat4 shear = desired.Matrix();
		shear.At(0, 1) += 0.3f;
		Expect(!Editor::ApplyGizmoWorldTransform(scene, child, Take(scene.WorldTransform(parent)) * shear),
		       "Sheared gizmo transform should fail");
		Expect(near(Take(scene.WorldTransform(child)), previous), "Rejected shear changed the node");
		Mat4 singular = world;
		for (int row = 0; row < 3; ++row)
		{
			singular.At(row, 0) = 0;
		}
		Expect(!Editor::ApplyGizmoWorldTransform(scene, child, singular), "Singular gizmo transform should fail");
		Mat4 nonfinite = world;
		nonfinite.At(0, 3) = NAN;
		Expect(!Editor::ApplyGizmoWorldTransform(scene, child, nonfinite), "Non-finite gizmo transform should fail");
		Expect(near(Take(scene.WorldTransform(child)), previous), "Invalid gizmo transform changed the node");
		std::cout << "Editor gizmo transform test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
