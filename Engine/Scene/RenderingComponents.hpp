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
	String SerializationType() const override
	{
		return "ursa.Camera";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Ursa::Float32 verticalFovRadians = 0.78539816f;
	Ursa::Float32 nearPlane = 0.1f;
	Ursa::Float32 farPlane = 100.0f;
};

class BoxRenderComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "ursa.BoxRender";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

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
