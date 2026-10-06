#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Scene/Component.hpp>
#include <Scene/Lighting.hpp>

namespace Ursa
{
class CameraComponent final : public Component
{
public:
	Ursa::Float32 verticalFovRadians = 0.78539816f;
	Ursa::Float32 nearPlane = 0.1f;
	Ursa::Float32 farPlane = 100.0f;
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
