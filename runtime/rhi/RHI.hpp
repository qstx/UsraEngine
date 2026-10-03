#pragma once
#include <core/Result.hpp>
#include <core/Containers.hpp>
#include <window/Window.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Ursa::Rhi
{
enum class Backend
{
	vulkan,
	openGl,
	d3D12
};
enum class Format
{
	rgbA8Unorm,
	rgbA8Srgb,
	bgrA8Unorm,
	r32Uint,
	d32Float
};
enum class ShaderFormat
{
	spirv,
	glsl,
	dxil
};
enum class ShaderStage
{
	vertex,
	fragment,
	compute
};
enum class ResourceState
{
	undefined,
	present,
	transferSource,
	transferDestination,
	shaderRead,
	shaderWrite,
	colorAttachment,
	depthAttachment,
	vertexBuffer,
	indexBuffer,
	uniformRead,
	storageReadWrite
};
enum class BufferUsage : uint32_t
{
	none = 0,
	transferSource = 1,
	transferDestination = 2,
	vertex = 4,
	index = 8,
	uniform = 16,
	storage = 32
};
enum class TextureUsage : uint32_t
{
	none = 0,
	transferSource = 1,
	transferDestination = 2,
	sampled = 4,
	storage = 8,
	colorAttachment = 16,
	depthAttachment = 32
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b)
{
	return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TextureUsage operator|(TextureUsage a, TextureUsage b)
{
	return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool Has(BufferUsage a, BufferUsage b)
{
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

inline bool Has(TextureUsage a, TextureUsage b)
{
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}
enum class Memory
{
	gpu,
	upload,
	readback
};
enum class BindingType
{
	uniformBuffer,
	storageBuffer,
	sampledTexture,
	storageTexture,
	sampler,
	combinedImageSampler
};
enum class VertexFormat
{
	float2,
	float3,
	float4
};
enum class Compare
{
	never,
	less,
	lessEqual,
	equal,
	greater,
	always
};

struct DeviceDesc
{
	Backend backend = Backend::vulkan;
	std::shared_ptr<Window> window;
	bool validation = false;
};

struct AdapterInfo
{
	String name;
	uint32_t vendorId = 0;
	uint32_t deviceId = 0;
	uint32_t apiVersion = 0;
};

struct Capabilities
{
	bool graphics = false;
	bool compute = false;
	bool textureSampling = false;
	bool storageImage = false;
	bool offscreenRendering = false;
	uint32_t maxTextureSize = 0;
	uint32_t maxPushConstants = 0;
};

struct BufferDesc
{
	size_t size = 0;
	BufferUsage usage = BufferUsage::none;
	Memory memory = Memory::gpu;
};

struct TextureDesc
{
	uint32_t width = 0, height = 0;
	Format format = Format::rgbA8Unorm;
	TextureUsage usage = TextureUsage::none;
};

struct SamplerDesc
{
	bool linear = true;
	bool repeat = false;
};

struct ShaderDesc
{
	ShaderStage stage = ShaderStage::vertex;
	ShaderFormat format = ShaderFormat::spirv;
	Vector<uint8_t> code;
	String entry = "main";
};

struct LayoutBinding
{
	uint32_t binding = 0;
	BindingType type = BindingType::uniformBuffer;
	uint32_t count = 1;
	uint32_t stages = 7;
};

struct VertexAttribute
{
	uint32_t location = 0, offset = 0;
	VertexFormat format = VertexFormat::float3;
};

struct SwapchainDesc
{
	bool vsync = true;
};

class Buffer
{
public:
	explicit Buffer(BufferDesc d) : desc(std::move(d))
	{
	}

	virtual ~Buffer() = default;

	virtual Result<void> Write(size_t offset, const void* data, size_t size) = 0;

	virtual Result<void> Read(size_t offset, void* data, size_t size) = 0;
	const BufferDesc desc;
};

class Texture
{
public:
	explicit Texture(TextureDesc d) : desc(std::move(d))
	{
	}

	virtual ~Texture() = default;
	const TextureDesc desc;
};

class Sampler
{
public:
	virtual ~Sampler() = default;
};

class Shader
{
public:
	virtual ~Shader() = default;
};

class BindGroupLayout
{
public:
	virtual ~BindGroupLayout() = default;
};

class BindGroup
{
public:
	virtual ~BindGroup() = default;
};

class Pipeline
{
public:
	virtual ~Pipeline() = default;
};

class Fence
{
public:
	virtual ~Fence() = default;

	virtual Result<void> Wait(uint64_t timeoutNanoseconds = UINT64_MAX) = 0;

	virtual bool Completed() const = 0;
};

using BufferPtr = std::shared_ptr<Buffer>;
using TexturePtr = std::shared_ptr<Texture>;
using SamplerPtr = std::shared_ptr<Sampler>;
using ShaderPtr = std::shared_ptr<Shader>;
using LayoutPtr = std::shared_ptr<BindGroupLayout>;
using BindGroupPtr = std::shared_ptr<BindGroup>;
using PipelinePtr = std::shared_ptr<Pipeline>;
using CommandListPtr = std::shared_ptr<class CommandList>;
using FencePtr = std::shared_ptr<Fence>;
using SwapchainPtr = std::shared_ptr<class Swapchain>;

struct BindingResource
{
	uint32_t binding = 0;
	BufferPtr buffer;
	TexturePtr texture;
	SamplerPtr sampler;
};

struct BindGroupDesc
{
	LayoutPtr layout;
	Vector<BindingResource> resources;
};

struct GraphicsPipelineDesc
{
	ShaderPtr vertex, fragment;
	LayoutPtr layout;
	Format colorFormat = Format::rgbA8Unorm;
	bool depthTest = false;
	Format depthFormat = Format::d32Float;
	Compare depthCompare = Compare::less;
	bool depthWrite = true;
	bool blend = false;
	bool cullBack = false;
	uint32_t vertexStride = 0;
	Vector<VertexAttribute> attributes;
};

struct ComputePipelineDesc
{
	ShaderPtr compute;
	LayoutPtr layout;
};

struct RenderTarget
{
	TexturePtr color;
	TexturePtr depth;
	bool clearColor = true;
	float colorValue[4] = {0.02f, 0.03f, 0.06f, 1.0f};
	bool clearDepth = true;
	float depthValue = 1.0f;
};

struct ScissorRect
{
	int32_t x = 0;
	int32_t y = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct AcquiredImage
{
	TexturePtr texture;
	ResourceState initialState = ResourceState::undefined;
};

class CommandList
{
public:
	virtual ~CommandList() = default;

	virtual Result<void> Begin() = 0;

	virtual Result<void> End() = 0;

	virtual Result<void> Barrier(const TexturePtr& texture, ResourceState before, ResourceState after) = 0;

	virtual Result<void> Barrier(const BufferPtr& buffer, ResourceState before, ResourceState after) = 0;

	virtual Result<void> CopyBuffer(const BufferPtr& source, const BufferPtr& destination, size_t size,
	                                size_t sourceOffset = 0, size_t destinationOffset = 0) = 0;

	virtual Result<void> CopyBufferToTexture(const BufferPtr& source, const TexturePtr& destination) = 0;

	virtual Result<void> CopyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination) = 0;

	virtual Result<void> BeginRendering(const RenderTarget& target) = 0;

	virtual Result<void> EndRendering() = 0;

	virtual Result<void> BindPipeline(const PipelinePtr& pipeline) = 0;

	virtual Result<void> BindGroup(const BindGroupPtr& group) = 0;

	virtual Result<void> BindVertexBuffer(const BufferPtr& buffer) = 0;

	virtual Result<void> BindIndexBuffer(const BufferPtr& buffer) = 0;

	virtual Result<void> SetScissor(const ScissorRect& rect) = 0;

	virtual Result<void> Draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;

	virtual Result<void> DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstInstance = 0,
	                                 uint32_t firstIndex = 0, int32_t vertexOffset = 0) = 0;

	virtual Result<void> Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) = 0;
};

class Swapchain
{
public:
	virtual ~Swapchain() = default;

	// A zero-sized window returns an empty texture. Poll or wait for window events, then retry.
	virtual Result<AcquiredImage> Acquire() = 0;

	virtual Result<void> Present() = 0;

	virtual Format Format() const = 0;
};

class Device
{
public:
	virtual ~Device() = default;

	virtual AdapterInfo AdapterInfo() const = 0;

	virtual Capabilities Capabilities() const = 0;

	virtual Result<BufferPtr> CreateBuffer(const BufferDesc&) = 0;

	virtual Result<TexturePtr> CreateTexture(const TextureDesc&) = 0;

	virtual Result<SamplerPtr> CreateSampler(const SamplerDesc&) = 0;

	virtual Result<ShaderPtr> CreateShader(const ShaderDesc&) = 0;

	virtual Result<LayoutPtr> CreateBindGroupLayout(const Vector<LayoutBinding>&) = 0;

	virtual Result<BindGroupPtr> CreateBindGroup(const BindGroupDesc&) = 0;

	virtual Result<PipelinePtr> CreateGraphicsPipeline(const GraphicsPipelineDesc&) = 0;

	virtual Result<PipelinePtr> CreateComputePipeline(const ComputePipelineDesc&) = 0;

	virtual Result<CommandListPtr> CreateCommandList() = 0;

	virtual Result<SwapchainPtr> CreateSwapchain(const SwapchainDesc&) = 0;

	virtual Result<FencePtr> Submit(const CommandListPtr&, const SwapchainPtr& = {}) = 0;

	virtual Result<void> WaitIdle() = 0;
};

using DevicePtr = std::shared_ptr<Device>;
Result<DevicePtr> CreateDevice(const DeviceDesc&);
} // namespace Ursa::Rhi
