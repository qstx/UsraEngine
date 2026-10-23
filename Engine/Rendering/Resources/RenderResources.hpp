#pragma once

#include <Rhi/RHI.hpp>

namespace Ursa::Rendering
{
struct RenderTextureDescription
{
	UInt32 width = 0;
	UInt32 height = 0;
	UInt32 format = 0;
	UInt32 depthBits = 0;
	bool randomWrite = false;
	UInt32 samples = 1;

	bool operator==(const RenderTextureDescription& other) const;
	UInt64 ByteSize() const;
};

struct RenderTextureResource
{
	RenderTextureResource();
	RenderTextureResource(const RenderTextureResource&) = delete;
	RenderTextureResource& operator=(const RenderTextureResource&) = delete;

	UInt64 identity = 0;
	UInt64 generation = 1;
	RenderTextureDescription description;
	Rhi::TexturePtr color;
	Rhi::TexturePtr depth;
	Rhi::ResourceState colorState = Rhi::ResourceState::Undefined;
	Rhi::ResourceState depthState = Rhi::ResourceState::Undefined;
	std::shared_ptr<RenderTextureResource> depthBacking;

	Rhi::ResourceState& DepthState()
	{
		return depthBacking ? depthBacking->DepthState() : depthState;
	}
};

struct GraphicsBufferResource
{
	Rhi::BufferPtr buffer;
	Rhi::ResourceState state = Rhi::ResourceState::Undefined;
};

struct RenderTargetReference
{
	std::shared_ptr<RenderTextureResource> texture;
	UInt32 id = 0;
};

struct RenderGlobalParameter
{
	String name;
	UInt32 kind = 1;
	Vector<UInt8> bytes;
	RenderTargetReference texture;
	std::shared_ptr<GraphicsBufferResource> buffer;
};

Result<std::shared_ptr<RenderTextureResource>> CreateRenderTexture(Rhi::Device& device,
                                                                   const RenderTextureDescription& description);
Result<std::shared_ptr<GraphicsBufferResource>> CreateGraphicsBuffer(Rhi::Device& device, UInt32 target, UInt32 count,
                                                                     UInt32 stride);
Result<Vector<UInt8>> ReadRenderTexture(Rhi::Device& device, RenderTextureResource& resource);
Result<Vector<UInt8>> ReadGraphicsBuffer(Rhi::Device& device, GraphicsBufferResource& resource, Size offset, Size size);
} // namespace Ursa::Rendering
