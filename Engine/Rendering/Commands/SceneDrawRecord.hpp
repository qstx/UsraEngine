#pragma once

#include <Rendering/Scene/RenderScene.hpp>
#include <Rendering/Resources/RenderResources.hpp>
#include <Rendering/Resources/RenderFrameContext.hpp>
#include <Rendering/Shader/ShaderPassSelection.hpp>
#include <functional>
#include <optional>

namespace Ursa::Rendering
{
struct SceneDrawRecord
{
	RenderObject object;
	ResourceId materialIdentity;
	UInt64 materialVersion = 0;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
	bool selectedPass = false;
	String activePipeline = "UrsaForward";
	std::optional<ResolvedGraphicsPass> resolvedPass;
};

struct SceneDrawEncoding
{
	Rhi::RenderTarget target;
	RenderFrameQueue* frames = nullptr;
	const Map<String, RenderGlobalParameter>* globals = nullptr;
	std::function<Result<Rhi::TexturePtr>(const MaterialTexturePtr&)> uploadTexture;
	std::function<Result<void>(const std::shared_ptr<RenderTextureResource>&, Rhi::ResourceState)> textureBarrier;
	std::function<Result<void>(const std::shared_ptr<GraphicsBufferResource>&, Rhi::ResourceState)> bufferBarrier;
	std::optional<Rhi::ViewportRect> viewport;
	std::optional<Rhi::ScissorRect> scissor;
};
} // namespace Ursa::Rendering
