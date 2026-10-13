#pragma once
#include <Core/Containers.hpp>
#include <Material/Color.hpp>
#include <Material/Material.hpp>
#include <Math/Camera.hpp>
#include <Math/Transform.hpp>
#include <Mesh/Mesh.hpp>
#include <Rhi/RHI.hpp>
#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Scene/Lighting.hpp>
#include <memory>

namespace Ursa
{
struct BoxInstance
{
	Transform transform;
	Color color;
	std::shared_ptr<MaterialInstance> material;
};

struct ModelBoxInstance
{
	Mat4 model;
	Color color;
	std::shared_ptr<MaterialInstance> material;
	MeshPtr mesh;
	Vector<Mat4> jointPalette;
	std::shared_ptr<const MeshSnapshot> meshSnapshot;
	UInt32 subMesh = 0;
};

class Renderer3D final
{
public:
	static Result<std::unique_ptr<Renderer3D>> Create(Rhi::DevicePtr selectedDevice);

	~Renderer3D();

	Size MeshCacheSize() const;

	Renderer3D(const Renderer3D&) = delete;

	Renderer3D& operator=(const Renderer3D&) = delete;

	// Acquires, renders, submits, and presents one swapchain frame.
	Result<void> Render(const Rhi::SwapchainPtr& swapchain, const Camera& camera, const Vector<BoxInstance>& boxes);

	Result<void> RenderModels(const Rhi::SwapchainPtr& swapchain, const Mat4& viewProjection,
	                          const Vector<ModelBoxInstance>& boxes, const SceneLighting& lighting = {},
	                          Vec3 cameraPosition = {}, Rendering::FrameCapture* capture = nullptr);

	// Leaves an offscreen color target in TransferSource state for readback.
	Result<Rhi::FencePtr> RenderOffscreen(const Rhi::TexturePtr& color, const Camera& camera,
	                                      const Vector<BoxInstance>& boxes);

	Result<Rhi::FencePtr> RenderOffscreenModels(const Rhi::TexturePtr& color, const Mat4& viewProjection,
	                                            const Vector<ModelBoxInstance>& boxes,
	                                            const SceneLighting& lighting = {}, Vec3 cameraPosition = {});

	// Record a scene pass in an existing command list. The caller submits and waits for its fence
	// before recording another frame or releasing this renderer.
	Result<void> RecordOffscreenModels(const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color,
	                                   Rhi::ResourceState initialState, Rhi::ResourceState finalState,
	                                   const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes,
	                                   const SceneLighting& lighting = {}, Vec3 cameraPosition = {});

private:
	struct Impl;

	explicit Renderer3D(std::unique_ptr<Impl> implementation);

	std::unique_ptr<Impl> impl;
};
} // namespace Ursa
