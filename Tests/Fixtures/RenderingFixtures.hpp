#pragma once

#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Math/Camera.hpp>
#include <Math/Transform.hpp>

namespace Ursa::Test
{
struct CubeDraw
{
	Transform transform;
	Color color;
	std::shared_ptr<MaterialInstance> material;
};

struct MeshDraw
{
	Mat4 model;
	Color color;
	std::shared_ptr<MaterialInstance> material;
	MeshPtr mesh;
	Vector<Mat4> jointPalette;
	std::shared_ptr<const MeshSnapshot> meshSnapshot;
	UInt32 subMesh = 0;
};

inline Vector<MeshDraw> MeshDraws(const Vector<Rendering::RenderObject>& objects)
{
	Vector<MeshDraw> draws;
	for (const auto& object : objects)
	{
		draws.push_back({object.world, object.parameters.baseColor, object.material, object.mesh, object.jointPalette,
		                 object.meshSnapshot, object.subMesh});
	}
	return draws;
}

// Test scene construction only. All rendering runs through the SRP command executor.
class SceneRenderer final
{
public:
	static Result<std::unique_ptr<SceneRenderer>> Create(Rhi::DevicePtr device);
	Result<Rhi::FencePtr> DrawOffscreen(const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                                    const Mat4& viewProjection, const Vector<Rendering::RenderObject>& draws,
	                                    const SceneLighting& lighting, Vec3 cameraPosition,
	                                    Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead);
	Result<Rhi::FencePtr> DrawOffscreen(const Rhi::TexturePtr& color, const Mat4& viewProjection,
	                                    const Vector<MeshDraw>& draws, const SceneLighting& lighting = {},
	                                    Vec3 cameraPosition = {},
	                                    Rhi::ResourceState finalState = Rhi::ResourceState::TransferSource);
	Result<Rhi::FencePtr> DrawOffscreen(const Rhi::TexturePtr& color, const Mat4& viewProjection,
	                                    const Vector<Rendering::RenderObject>& draws,
	                                    const SceneLighting& lighting = {}, Vec3 cameraPosition = {},
	                                    Rhi::ResourceState finalState = Rhi::ResourceState::TransferSource);
	Result<Rhi::FencePtr> DrawCubes(const Rhi::TexturePtr& color, const Camera& camera, const Vector<CubeDraw>& cubes);
	Result<void> Render(const Rhi::SwapchainPtr& window, const Camera& camera, const Vector<CubeDraw>& cubes);
	Size MeshCacheSize() const;

private:
	Result<void> Record(const Mat4& viewProjection, const Vector<MeshDraw>& draws, const SceneLighting& lighting,
	                    Vec3 cameraPosition);
	Rhi::DevicePtr device;
	std::unique_ptr<Rendering::RenderCommandExecutor> executor;
	std::shared_ptr<Rendering::RenderTextureResource> output;
};
} // namespace Ursa::Test
