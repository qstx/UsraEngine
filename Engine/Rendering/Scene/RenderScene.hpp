#pragma once

#include <Rendering/Scene/CameraData.hpp>
#include <Scene/Scene.hpp>

namespace Ursa::Rendering
{
struct RenderObject
{
	ResourceId objectId;
	ResourceId rendererId;
	UInt32 layer = 0;
	Mat4 world = Mat4::Identity();
	Bounds worldBounds;
	MeshPtr mesh;
	std::shared_ptr<const MeshSnapshot> meshSnapshot;
	std::shared_ptr<MaterialInstance> material;
	std::shared_ptr<const Shader::ShaderAsset> shader;
	UInt64 materialVersion = 0;
	MaterialParameters parameters;
	Vector<Mat4> jointPalette;
	UInt32 subMesh = 0;
};

class RenderScene final
{
public:
	Result<void> Capture(const Scene& scene, Float32 aspect);

	const Vector<RenderObject>& Objects() const
	{
		return objects;
	}

	const Vector<CameraData>& Cameras() const
	{
		return cameras;
	}

	const SceneLighting& Lighting() const
	{
		return lighting;
	}

	UInt64 Version() const
	{
		return version;
	}

	bool IsValid() const
	{
		return valid;
	}

private:
	Result<void> Collect(const Scene& scene, const GameObject::Ptr& object, Float32 aspect);

	Vector<RenderObject> objects;
	Vector<CameraData> cameras;
	SceneLighting lighting;
	UInt64 version = 0;
	bool valid = false;
	Size objectCount = 0;
	Vector<Mat4> scratchPalette;
	Vector<Mat4> scratchTransforms;
};
} // namespace Ursa::Rendering
