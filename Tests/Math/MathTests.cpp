#include <Math/Camera.hpp>
#include <Math/Quat.hpp>
#include <Math/Transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void ExpectNear(float actual, float expected)
{
	if (std::abs(actual - expected) > 0.0001f)
	{
		throw std::runtime_error("Matrix value differs from expected result");
	}
}

void TestTransform()
{
	Ursa::Transform transform;
	transform.position = {2.0f, 3.0f, 4.0f};
	transform.size = {2.0f, 4.0f, 6.0f};
	const auto matrix = transform.Matrix();
	ExpectNear(matrix.At(0, 0), 2.0f);
	ExpectNear(matrix.At(1, 1), 4.0f);
	ExpectNear(matrix.At(2, 2), 6.0f);
	ExpectNear(matrix.At(0, 3), 2.0f);
	ExpectNear(matrix.At(1, 3), 3.0f);
	ExpectNear(matrix.At(2, 3), 4.0f);
}

void TestCamera()
{
	Ursa::Camera camera;
	camera.position = {0.0f, 0.0f, 5.0f};
	camera.target = {};
	const auto view = camera.View();
	ExpectNear(view.At(2, 3), -5.0f);
	const auto square = camera.Projection(1.0f);
	const auto wide = camera.Projection(2.0f);
	ExpectNear(wide.At(0, 0), square.At(0, 0) * 0.5f);
	ExpectNear(wide.At(1, 1), square.At(1, 1));
	const auto viewProjection = camera.ViewProjection(1.0f);
	ExpectNear(viewProjection.At(3, 3), 5.0f);
}

void TestHierarchyMatrix()
{
	const auto parent = Ursa::Mat4::Translation({2.0f, 3.0f, 4.0f}) *
	                    Ursa::Mat4::Rotation(Ursa::Quat::FromAxisAngle({0.0f, 1.0f, 0.0f}, 1.57079633f));
	const auto child = Ursa::Mat4::Translation({1.0f, 0.0f, 0.0f}) * Ursa::Mat4::Scale({2.0f, 3.0f, 4.0f});
	const auto world = parent * child;
	ExpectNear(world.At(0, 3), 2.0f);
	ExpectNear(world.At(1, 3), 3.0f);
	ExpectNear(world.At(2, 3), 3.0f);
	const auto identity = world.InverseAffine() * world;
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			ExpectNear(identity.At(row, column), row == column ? 1.0f : 0.0f);
		}
	}
}
} // namespace

int main()
{
	try
	{
		TestTransform();
		TestCamera();
		TestHierarchyMatrix();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
