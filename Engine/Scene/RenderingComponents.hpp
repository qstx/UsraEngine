#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Scene/Component.hpp>
#include <Scene/Lighting.hpp>
#include <Renderer3D/ModelMesh.hpp>

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

class MeshRendererComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "ursa.MeshRenderer";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Result<void> SetMesh(ResourceId modelGuid, Ursa::UInt64 localFileId, ModelMeshPtr value)
	{
		if (!modelGuid || localFileId == 0 || !value || value->modelGuid != modelGuid ||
		    value->localFileId != localFileId)
		{
			return Result<void>::Failure("MeshRenderer needs a valid model mesh sub-asset reference");
		}
		meshGuid = modelGuid;
		meshLocalFileId = localFileId;
		mesh = std::move(value);
		return {};
	}

	ResourceId MeshGuid() const
	{
		return meshGuid;
	}

	Ursa::UInt64 MeshLocalFileId() const
	{
		return meshLocalFileId;
	}

	ModelMeshPtr Mesh() const
	{
		return mesh;
	}

private:
	ResourceId meshGuid;
	Ursa::UInt64 meshLocalFileId = 0;
	ModelMeshPtr mesh;
};

class SkinnedMeshRendererComponent final : public Component
{
public:
	String SerializationType() const override
	{
		return "ursa.SkinnedMeshRenderer";
	}

	Result<void> Serialize(ArchiveWriter& writer) const override;

	Result<void> Deserialize(const ArchiveReader& reader, SerializationContext& context) override;

	Result<void> SetMesh(ResourceId modelGuid, Ursa::UInt64 localFileId, ModelMeshPtr value)
	{
		if (!modelGuid || localFileId == 0 || !value || value->modelGuid != modelGuid ||
		    value->localFileId != localFileId || !value->skeleton || value->skinInfluences.empty())
		{
			return Result<void>::Failure("SkinnedMeshRenderer needs a skinned model mesh sub-asset");
		}
		meshGuid = modelGuid;
		meshLocalFileId = localFileId;
		mesh = std::move(value);
		return {};
	}

	ResourceId MeshGuid() const
	{
		return meshGuid;
	}

	Ursa::UInt64 MeshLocalFileId() const
	{
		return meshLocalFileId;
	}

	ModelMeshPtr Mesh() const
	{
		return mesh;
	}

private:
	ResourceId meshGuid;
	Ursa::UInt64 meshLocalFileId = 0;
	ModelMeshPtr mesh;
};

struct SceneRenderData
{
	Mat4 viewProjection;
	Vec3 cameraPosition{};
	SceneLighting lighting;
	Vector<ModelBoxInstance> boxes;
};
} // namespace Ursa
