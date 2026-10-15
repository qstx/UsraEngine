#pragma once

#include <Mesh/Mesh.hpp>
#include <Material/Material.hpp>

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

} // namespace Ursa::Rendering
