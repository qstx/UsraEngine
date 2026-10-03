#pragma once
#include <core/Result.hpp>
#include <core/Containers.hpp>
#include <window/Window.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ursa::rhi {
enum class Backend { Vulkan, OpenGL, D3D12 };
enum class Format { RGBA8Unorm, RGBA8Srgb, BGRA8Unorm, R32Uint, D32Float };
enum class ShaderFormat { SPIRV, GLSL, DXIL };
enum class ShaderStage { Vertex, Fragment, Compute };
enum class ResourceState {
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
enum class BufferUsage : uint32_t {
    None = 0,
    TransferSource = 1,
    TransferDestination = 2,
    Vertex = 4,
    Index = 8,
    Uniform = 16,
    Storage = 32
};
enum class TextureUsage : uint32_t {
    None = 0,
    TransferSource = 1,
    TransferDestination = 2,
    Sampled = 4,
    Storage = 8,
    ColorAttachment = 16,
    DepthAttachment = 32
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool has(BufferUsage a, BufferUsage b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

inline bool has(TextureUsage a, TextureUsage b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}
enum class Memory { GPU, Upload, Readback };
enum class BindingType { UniformBuffer, StorageBuffer, SampledTexture, StorageTexture, Sampler, CombinedImageSampler };
enum class VertexFormat { Float2, Float3, Float4 };
enum class Compare { Never, Less, LessEqual, Equal, Greater, Always };

struct DeviceDesc {
    Backend backend = Backend::Vulkan;
    std::shared_ptr<Window> window;
    bool validation = false;
};

struct AdapterInfo {
    String name;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t apiVersion = 0;
};

struct Capabilities {
    bool graphics = false;
    bool compute = false;
    bool textureSampling = false;
    bool storageImage = false;
    bool offscreenRendering = false;
    uint32_t maxTextureSize = 0;
    uint32_t maxPushConstants = 0;
};

struct BufferDesc {
    size_t size = 0;
    BufferUsage usage = BufferUsage::None;
    Memory memory = Memory::GPU;
};

struct TextureDesc {
    uint32_t width = 0, height = 0;
    Format format = Format::RGBA8Unorm;
    TextureUsage usage = TextureUsage::None;
};

struct SamplerDesc {
    bool linear = true;
    bool repeat = false;
};

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    ShaderFormat format = ShaderFormat::SPIRV;
    Vector<uint8_t> code;
    String entry = "main";
};

struct LayoutBinding {
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    uint32_t count = 1;
    uint32_t stages = 7;
};

struct VertexAttribute {
    uint32_t location = 0, offset = 0;
    VertexFormat format = VertexFormat::Float3;
};

struct SwapchainDesc {
    bool vsync = true;
};

class Buffer {
public:
    explicit Buffer(BufferDesc d) : desc(std::move(d)) {
    }

    virtual ~Buffer() = default;

    virtual Result<void> write(size_t offset, const void *data, size_t size) = 0;

    virtual Result<void> read(size_t offset, void *data, size_t size) = 0;
    const BufferDesc desc;
};

class Texture {
public:
    explicit Texture(TextureDesc d) : desc(std::move(d)) {
    }

    virtual ~Texture() = default;
    const TextureDesc desc;
};

class Sampler {
public:
    virtual ~Sampler() = default;
};

class Shader {
public:
    virtual ~Shader() = default;
};

class BindGroupLayout {
public:
    virtual ~BindGroupLayout() = default;
};

class BindGroup {
public:
    virtual ~BindGroup() = default;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
};

class Fence {
public:
    virtual ~Fence() = default;

    virtual Result<void> wait(uint64_t timeoutNanoseconds = UINT64_MAX) = 0;

    virtual bool completed() const = 0;
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

struct BindingResource {
    uint32_t binding = 0;
    BufferPtr buffer;
    TexturePtr texture;
    SamplerPtr sampler;
};

struct BindGroupDesc {
    LayoutPtr layout;
    Vector<BindingResource> resources;
};

struct GraphicsPipelineDesc {
    ShaderPtr vertex, fragment;
    LayoutPtr layout;
    Format colorFormat = Format::RGBA8Unorm;
    bool depthTest = false;
    Format depthFormat = Format::D32Float;
    Compare depthCompare = Compare::Less;
    bool depthWrite = true;
    bool blend = false;
    bool cullBack = false;
    uint32_t vertexStride = 0;
    Vector<VertexAttribute> attributes;
};

struct ComputePipelineDesc {
    ShaderPtr compute;
    LayoutPtr layout;
};

struct RenderTarget {
    TexturePtr color;
    TexturePtr depth;
    bool clearColor = true;
    float colorValue[4] = {0.02f, 0.03f, 0.06f, 1.0f};
    bool clearDepth = true;
    float depthValue = 1.0f;
};

struct ScissorRect {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct AcquiredImage {
    TexturePtr texture;
    ResourceState initialState = ResourceState::Undefined;
};

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual Result<void> begin() = 0;

    virtual Result<void> end() = 0;

    virtual Result<void> barrier(const TexturePtr &texture, ResourceState before, ResourceState after) = 0;

    virtual Result<void> barrier(const BufferPtr &buffer, ResourceState before, ResourceState after) = 0;

    virtual Result<void> copyBuffer(const BufferPtr &source, const BufferPtr &destination, size_t size,
                                    size_t sourceOffset = 0, size_t destinationOffset = 0) = 0;

    virtual Result<void> copyBufferToTexture(const BufferPtr &source, const TexturePtr &destination) = 0;

    virtual Result<void> copyTextureToBuffer(const TexturePtr &source, const BufferPtr &destination) = 0;

    virtual Result<void> beginRendering(const RenderTarget &target) = 0;

    virtual Result<void> endRendering() = 0;

    virtual Result<void> bindPipeline(const PipelinePtr &pipeline) = 0;

    virtual Result<void> bindGroup(const BindGroupPtr &group) = 0;

    virtual Result<void> bindVertexBuffer(const BufferPtr &buffer) = 0;

    virtual Result<void> bindIndexBuffer(const BufferPtr &buffer) = 0;

    virtual Result<void> setScissor(const ScissorRect &rect) = 0;

    virtual Result<void> draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;

    virtual Result<void> drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstInstance = 0,
                                     uint32_t firstIndex = 0, int32_t vertexOffset = 0) = 0;

    virtual Result<void> dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) = 0;
};

class Swapchain {
public:
    virtual ~Swapchain() = default;

    // A zero-sized window returns an empty texture. Poll or wait for window events, then retry.
    virtual Result<AcquiredImage> acquire() = 0;

    virtual Result<void> present() = 0;

    virtual Format format() const = 0;
};

class Device {
public:
    virtual ~Device() = default;

    virtual AdapterInfo adapterInfo() const = 0;

    virtual Capabilities capabilities() const = 0;

    virtual Result<BufferPtr> createBuffer(const BufferDesc &) = 0;

    virtual Result<TexturePtr> createTexture(const TextureDesc &) = 0;

    virtual Result<SamplerPtr> createSampler(const SamplerDesc &) = 0;

    virtual Result<ShaderPtr> createShader(const ShaderDesc &) = 0;

    virtual Result<LayoutPtr> createBindGroupLayout(const Vector<LayoutBinding> &) = 0;

    virtual Result<BindGroupPtr> createBindGroup(const BindGroupDesc &) = 0;

    virtual Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc &) = 0;

    virtual Result<PipelinePtr> createComputePipeline(const ComputePipelineDesc &) = 0;

    virtual Result<CommandListPtr> createCommandList() = 0;

    virtual Result<SwapchainPtr> createSwapchain(const SwapchainDesc &) = 0;

    virtual Result<FencePtr> submit(const CommandListPtr &, const SwapchainPtr & = {}) = 0;

    virtual Result<void> waitIdle() = 0;
};

using DevicePtr = std::shared_ptr<Device>;
Result<DevicePtr> createDevice(const DeviceDesc &);
} // namespace ursa::rhi
