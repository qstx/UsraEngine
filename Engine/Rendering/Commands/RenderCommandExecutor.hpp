#pragma once

#include <Rendering/Commands/RenderCommandProtocol.hpp>
#include <Rendering/Resources/RenderResources.hpp>
#include <Rendering/Resources/RenderFrameContext.hpp>
#include <Rendering/Commands/SceneDrawRecord.hpp>
#include <Material/Material.hpp>
#include <functional>

namespace Ursa::Rendering
{
struct RenderCommand
{
	RenderCommandOpcode opcode = RenderCommandOpcode::SetRenderTarget;
	RenderTargetReference source;
	RenderTargetReference target;
	Vector<RenderTargetReference> colors;
	RenderTargetReference depthTarget;
	bool hasDepthTarget = false;
	RenderTextureDescription description;
	std::shared_ptr<GraphicsBufferResource> sourceBuffer;
	std::shared_ptr<GraphicsBufferResource> targetBuffer;
	std::shared_ptr<MaterialInstance> material;
	std::shared_ptr<const Shader::ShaderAsset> shader;
	UInt32 kernel = 0;
	Array<UInt32, 3> groups{1, 1, 1};
	Vector<SceneDrawRecord> draws;
	CameraData camera;
	SceneLighting lighting;
	String activePipeline = "UrsaForward";
	RenderGlobalParameter global;
	Color color;
	UInt32 clearFlags = 0;
	Float32 clearDepth = 1;
	UInt32 clearStencil = 0;
	Rhi::ViewportRect viewport;
	Rhi::ScissorRect scissor;
	UInt64 byteSize = 0;
	UInt64 sourceOffset = 0;
	UInt64 targetOffset = 0;
	Int32 pass = -1;
};

struct RenderCommandStatistics
{
	UInt64 logicalSubmissions = 0;
	UInt64 physicalSubmissions = 0;
	UInt64 temporaryTargetsCreated = 0;
	UInt64 temporaryTargetsReused = 0;
	UInt64 temporaryBytes = 0;
	UInt64 pipelinesCreated = 0;
	Size meshCacheEntries = 0;
};

// Copies accepted operations and resources; logical Submit does not wait or present.
class RenderCommandExecutor final
{
public:
	static Result<std::unique_ptr<RenderCommandExecutor>> Create(Rhi::DevicePtr device);
	~RenderCommandExecutor();

	Result<void> Begin(std::shared_ptr<RenderTextureResource> cameraTarget = {},
	                   Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead,
	                   const Rhi::SwapchainPtr& swapchain = {});
	Result<void> ResetCamera();
	Result<void> Execute(const Vector<RenderCommand>& commands);
	Result<void> Submit();
	Result<Rhi::FencePtr> End();
	// Discards the unsubmitted segment; already submitted segments still complete safely.
	Result<Rhi::FencePtr> Abort();
	Result<void> Drain();
	bool IsActive() const;
	std::shared_ptr<RenderTextureResource> CameraTarget() const;
	Result<std::shared_ptr<const Shader::ShaderAsset>> ResolveBuiltinShader(ResourceId id);
	void SetFrameCapture(FrameCapture* capture);
	const RenderCommandStatistics& Statistics() const;

private:
	struct Impl;
	explicit RenderCommandExecutor(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl;
};
} // namespace Ursa::Rendering
