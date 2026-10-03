#pragma once
#include <Core/Containers.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Scene/Component.hpp>
#include <Scene/Lighting.hpp>

namespace Ursa
{
class CameraComponent final : public Component
{
public:
	float verticalFovRadians = 0.78539816f;
	float nearPlane = 0.1f;
	float farPlane = 100.0f;
};

class BoxRenderComponent final : public Component
{
public:
	explicit BoxRenderComponent(Color selectedColor = {}) : color(selectedColor)
	{
	}

	Color color;
	std::shared_ptr<MaterialInstance> material;
};

struct SceneRenderData
{
	Mat4 viewProjection;
	Vec3 cameraPosition{};
	SceneLighting lighting;
	Vector<ModelBoxInstance> boxes;
};
} // namespace Ursa
