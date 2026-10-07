#pragma once
#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <Core/Containers.hpp>
#include <Window/Window.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Ursa::Rhi
{
enum class Backend
{
	Vulkan,
	OpenGl,
	D3D12
};
enum class Format
{
	RgbA8Unorm,
	RgbA8Srgb,
	BgrA8Unorm,
	R32Uint,
	D32Float,
	D32FloatS8Uint
};
enum class ShaderFormat
{
	Spirv,
	Glsl,
	Dxil
};
enum class ShaderStage
{
	Vertex,
	Fragment,
	Compute
};
enum class ResourceState
{
	Undefined,
	Present,
	TransferSource,
	TransferDestination,
	ShaderRead,
	ShaderWrite,
	ColorAttachment,
	DepthAttachment,
	VertexBuffer,
	IndexBuffer,
	UniformRead,
	StorageReadWrite
};
enum class BufferUsage : Ursa::UInt32
{
	None = 0,
	TransferSource = 1,
	TransferDestination = 2,
	Vertex = 4,
	Index = 8,
	Uniform = 16,
	Storage = 32
};
enum class TextureUsage : Ursa::UInt32
{
	None = 0,
	TransferSource = 1,
	TransferDestination = 2,
	Sampled = 4,
	Storage = 8,
	ColorAttachment = 16,
	DepthAttachment = 32
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b)
{
	return static_cast<BufferUsage>(static_cast<Ursa::UInt32>(a) | static_cast<Ursa::UInt32>(b));
}

inline TextureUsage operator|(TextureUsage a, TextureUsage b)
{
	return static_cast<TextureUsage>(static_cast<Ursa::UInt32>(a) | static_cast<Ursa::UInt32>(b));
}

inline bool Has(BufferUsage a, BufferUsage b)
{
	return (static_cast<Ursa::UInt32>(a) & static_cast<Ursa::UInt32>(b)) != 0;
}

inline bool Has(TextureUsage a, TextureUsage b)
{
	return (static_cast<Ursa::UInt32>(a) & static_cast<Ursa::UInt32>(b)) != 0;
}
enum class Memory
{
	Gpu,
	Upload,
	Readback
};
enum class BindingType
{
	UniformBuffer,
	StorageBuffer,
	SampledTexture,
	StorageTexture,
	Sampler,
	CombinedImageSampler
};
enum class VertexFormat
{
	Float2,
	Float3,
	Float4,
	UInt4
};
enum class Compare
{
	Never,
	Less,
	LessEqual,
	Equal,
	NotEqual,
	Greater,
	GreaterEqual,
	Always
};

enum class CullMode
{
	None,
	Front,
	Back
};

enum class BlendFactor
{
	Zero,
	One,
	SourceColor,
	OneMinusSourceColor,
	DestinationColor,
	OneMinusDestinationColor,
	SourceAlpha,
	OneMinusSourceAlpha,
	DestinationAlpha,
	OneMinusDestinationAlpha
};

enum class BlendOperation
{
	Add,
	Subtract,
	ReverseSubtract,
	Minimum,
	Maximum
};

enum class StencilOperation
{
	Keep,
	Zero,
	Replace,
	IncrementClamp,
	DecrementClamp,
	Invert,
	IncrementWrap,
	DecrementWrap
};

enum class ColorWriteMask : Ursa::UInt32
{
	None = 0,
	Red = 1,
	Green = 2,
	Blue = 4,
	Alpha = 8,
	All = 15
};

inline ColorWriteMask operator|(ColorWriteMask left, ColorWriteMask right)
{
	return static_cast<ColorWriteMask>(static_cast<Ursa::UInt32>(left) | static_cast<Ursa::UInt32>(right));
}

inline bool Has(ColorWriteMask value, ColorWriteMask channel)
{
	return (static_cast<Ursa::UInt32>(value) & static_cast<Ursa::UInt32>(channel)) != 0;
}

struct BlendState
{
	bool enabled = false;
	BlendFactor sourceColor = BlendFactor::SourceAlpha;
	BlendFactor destinationColor = BlendFactor::OneMinusSourceAlpha;
	BlendOperation colorOperation = BlendOperation::Add;
	BlendFactor sourceAlpha = BlendFactor::One;
	BlendFactor destinationAlpha = BlendFactor::Zero;
	BlendOperation alphaOperation = BlendOperation::Add;
};

struct StencilFaceState
{
	Compare compare = Compare::Always;
	StencilOperation failOperation = StencilOperation::Keep;
	StencilOperation passOperation = StencilOperation::Keep;
	StencilOperation depthFailOperation = StencilOperation::Keep;
};

struct StencilState
{
	bool enabled = false;
	Ursa::UInt32 reference = 0;
	Ursa::UInt32 readMask = 0xff;
	Ursa::UInt32 writeMask = 0xff;
	StencilFaceState front;
	StencilFaceState back;
};

struct DeviceDesc
{
	Backend backend = Backend::Vulkan;
	std::shared_ptr<Window> window;
	bool validation = false;
};

struct AdapterInfo
{
	String name;
	Ursa::UInt32 vendorId = 0;
	Ursa::UInt32 deviceId = 0;
	Ursa::UInt32 apiVersion = 0;
};

struct Capabilities
{
	bool graphics = false;
	bool compute = false;
	bool textureSampling = false;
	bool storageImage = false;
	bool offscreenRendering = false;
	Ursa::UInt32 maxTextureSize = 0;
	Ursa::UInt32 maxPushConstants = 0;
	Ursa::UInt32 maxStorageBufferRange = 0;
};

struct BufferDesc
{
	Ursa::Size size = 0;
	BufferUsage usage = BufferUsage::None;
	Memory memory = Memory::Gpu;
};

struct TextureDesc
{
	Ursa::UInt32 width = 0, height = 0;
	Format format = Format::RgbA8Unorm;
	TextureUsage usage = TextureUsage::None;
	Ursa::UInt32 mipLevels = 1;
};

struct SamplerDesc
{
	bool linear = true;
	bool repeat = false;
	Ursa::Float32 maxLod = 16.0f;
};

struct ShaderDesc
{
	ShaderStage stage = ShaderStage::Vertex;
	ShaderFormat format = ShaderFormat::Spirv;
	Vector<Ursa::UInt8> code;
	String entry = "main";
};

struct LayoutBinding
{
	Ursa::UInt32 binding = 0;
	BindingType type = BindingType::UniformBuffer;
	Ursa::UInt32 count = 1;
	Ursa::UInt32 stages = 7;
};

struct VertexAttribute
{
	Ursa::UInt32 location = 0, offset = 0;
	VertexFormat format = VertexFormat::Float3;
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

	virtual Result<void> Write(Ursa::Size offset, const void* data, Ursa::Size size) = 0;

	virtual Result<void> Read(Ursa::Size offset, void* data, Ursa::Size size) = 0;
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

	virtual Result<void> Wait(Ursa::UInt64 timeoutNanoseconds = UINT64_MAX) = 0;

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
	Ursa::UInt32 binding = 0;
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
	Format colorFormat = Format::RgbA8Unorm;
	bool depthTest = false;
	Format depthFormat = Format::D32Float;
	Compare depthCompare = Compare::Less;
	bool depthWrite = true;
	bool blend = false;
	bool cullBack = false;
	CullMode cullMode = CullMode::None;
	BlendState blendState;
	StencilState stencil;
	ColorWriteMask colorWriteMask = ColorWriteMask::All;
	Ursa::Int32 depthBiasConstant = 0;
	Ursa::Float32 depthBiasSlope = 0.0f;
	Ursa::UInt32 vertexStride = 0;
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
	Ursa::Float32 colorValue[4] = {0.02f, 0.03f, 0.06f, 1.0f};
	bool clearDepth = true;
	Ursa::Float32 depthValue = 1.0f;
	bool clearStencil = true;
	Ursa::UInt32 stencilValue = 0;
};

struct ScissorRect
{
	Ursa::Int32 x = 0;
	Ursa::Int32 y = 0;
	Ursa::UInt32 width = 0;
	Ursa::UInt32 height = 0;
};

struct AcquiredImage
{
	TexturePtr texture;
	ResourceState initialState = ResourceState::Undefined;
};

class CommandList
{
public:
	virtual ~CommandList() = default;

	virtual Result<void> Begin() = 0;

	virtual Result<void> End() = 0;

	virtual Result<void> Barrier(const TexturePtr& texture, ResourceState before, ResourceState after) = 0;

	virtual Result<void> Barrier(const BufferPtr& buffer, ResourceState before, ResourceState after) = 0;

	virtual Result<void> CopyBuffer(const BufferPtr& source, const BufferPtr& destination, Ursa::Size size,
	                                Ursa::Size sourceOffset = 0, Ursa::Size destinationOffset = 0) = 0;

	virtual Result<void> CopyBufferToTexture(const BufferPtr& source, const TexturePtr& destination,
	                                         Ursa::UInt32 mipLevel = 0, Ursa::Size sourceOffset = 0) = 0;

	virtual Result<void> CopyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination,
	                                         Ursa::UInt32 mipLevel = 0) = 0;

	virtual Result<void> BeginRendering(const RenderTarget& target) = 0;

	virtual Result<void> EndRendering() = 0;

	virtual Result<void> BindPipeline(const PipelinePtr& pipeline) = 0;

	virtual Result<void> BindGroup(const BindGroupPtr& group) = 0;

	virtual Result<void> BindVertexBuffer(const BufferPtr& buffer) = 0;

	virtual Result<void> BindIndexBuffer(const BufferPtr& buffer) = 0;

	virtual Result<void> SetScissor(const ScissorRect& rect) = 0;

	virtual Result<void> Draw(Ursa::UInt32 vertexCount, Ursa::UInt32 instanceCount = 1) = 0;

	virtual Result<void> DrawIndexed(Ursa::UInt32 indexCount, Ursa::UInt32 instanceCount = 1,
	                                 Ursa::UInt32 firstInstance = 0, Ursa::UInt32 firstIndex = 0,
	                                 Ursa::Int32 vertexOffset = 0) = 0;

	virtual Result<void> Dispatch(Ursa::UInt32 x, Ursa::UInt32 y = 1, Ursa::UInt32 z = 1) = 0;
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
