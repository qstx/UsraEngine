#pragma once

#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Rendering/Scene/RendererList.hpp>

namespace Ursa::Rendering
{
class FrameCapture;

class NativeForwardPipeline final
{
public:
	static Result<std::unique_ptr<NativeForwardPipeline>> Create(Rhi::DevicePtr device);
	Result<void> Render(Scene& scene, const Rhi::SwapchainPtr& window, FrameCapture* capture = nullptr);
	Result<void> RenderOffscreen(const Scene& scene, const std::shared_ptr<RenderTextureResource>& output,
	                             Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead,
	                             std::optional<CameraType> filter = {}, std::optional<CameraData> cameraOverride = {});

private:
	Result<void> RenderTarget(const Scene& scene, const std::shared_ptr<RenderTextureResource>& output,
	                          const Rhi::SwapchainPtr& window, Rhi::ResourceState finalState, FrameCapture* capture,
	                          std::optional<CameraType> filter, std::optional<CameraData> cameraOverride);
	std::unique_ptr<RenderCommandExecutor> executor;
	RenderScene snapshot;
	RenderSceneContext context;
	Vector<RenderCommand> commands;
	UInt64 frameId = 0;
};
} // namespace Ursa::Rendering
