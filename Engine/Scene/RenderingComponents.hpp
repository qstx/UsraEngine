#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Scene/Component.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Scene/SkinnedMeshRendererComponent.hpp>
#include <Scene/RendererComponent.hpp>
#include <Scene/MeshFilterComponent.hpp>

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

struct SceneRenderData
{
	Mat4 viewProjection;
	Vec3 cameraPosition{};
	SceneLighting lighting;
	Vector<ModelBoxInstance> boxes;
};
} // namespace Ursa
