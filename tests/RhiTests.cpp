#include <Ursa/RHI.hpp>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace ursa::rhi;

template <class T> T take(ursa::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error);
    return std::move(r.value);
}

void done(ursa::Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error);
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<uint8_t> loadShader(const char *name) {
    std::ifstream file(std::string(URSA_SHADER_DIR) + "/" + name + ".spv", std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error(std::string("Cannot open shader ") + name);
    std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error(std::string("Cannot read shader ") + name);
    return bytes;
}

void testCompute(const DevicePtr &device) {
    constexpr size_t size = 64 * sizeof(uint32_t);
    auto output = take(device->createBuffer({size, BufferUsage::Storage | BufferUsage::TransferSource, Memory::GPU}));
    auto readback = take(device->createBuffer({size, BufferUsage::TransferDestination, Memory::Readback}));
    auto layout = take(device->createBindGroupLayout({{0, BindingType::StorageBuffer, 1, 4}}));
    auto group = take(device->createBindGroup({layout, {{0, output, {}, {}}}}));
    auto shader = take(device->createShader({ShaderStage::Compute, ShaderFormat::SPIRV, loadShader("test.comp")}));
    auto pipeline = take(device->createComputePipeline({shader, layout}));
    auto cmd = take(device->createCommandList());
    done(cmd->begin());
    done(cmd->barrier(output, ResourceState::Undefined, ResourceState::StorageReadWrite));
    done(cmd->bindPipeline(pipeline));
    done(cmd->bindGroup(group));
    done(cmd->dispatch(1));
    done(cmd->barrier(output, ResourceState::StorageReadWrite, ResourceState::TransferSource));
    done(cmd->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(cmd->copyBuffer(output, readback, size));
    done(cmd->end());
    auto fence = take(device->submit(cmd));
    done(fence->wait());
    std::array<uint32_t, 64> values{};
    done(readback->read(0, values.data(), size));
    for (uint32_t i = 0; i < values.size(); ++i)
        expect(values[i] == i * 3 + 7, "Compute output mismatch");
}

void testTextureAndOffscreen(const DevicePtr &device) {
    constexpr uint32_t width = 4, height = 4;
    std::array<uint8_t, 16> pixels{};
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 240;
        pixels[i + 1] = 20;
        pixels[i + 2] = 40;
        pixels[i + 3] = 255;
    }
    auto upload = take(device->createBuffer({pixels.size(), BufferUsage::TransferSource, Memory::Upload}));
    done(upload->write(0, pixels.data(), pixels.size()));
    auto texture = take(
        device->createTexture({2, 2, Format::RGBA8Unorm, TextureUsage::Sampled | TextureUsage::TransferDestination}));
    auto color = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    auto depth = take(device->createTexture({width, height, Format::D32Float, TextureUsage::DepthAttachment}));
    auto readback =
        take(device->createBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    auto sampler = take(device->createSampler({false, false}));
    constexpr std::array<float, 16> vertices{
        {-1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f}};
    constexpr std::array<uint32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
    auto vertexBuffer = take(device->createBuffer({sizeof(vertices), BufferUsage::Vertex, Memory::Upload}));
    auto indexBuffer = take(device->createBuffer({sizeof(indices), BufferUsage::Index, Memory::Upload}));
    done(vertexBuffer->write(0, vertices.data(), sizeof(vertices)));
    done(indexBuffer->write(0, indices.data(), sizeof(indices)));
    auto layout = take(device->createBindGroupLayout({{0, BindingType::CombinedImageSampler, 1, 2}}));
    auto group = take(device->createBindGroup({layout, {{0, {}, texture, sampler}}}));
    auto vertex =
        take(device->createShader({ShaderStage::Vertex, ShaderFormat::SPIRV, loadShader("test_vertex.vert")}));
    auto fragment = take(device->createShader({ShaderStage::Fragment, ShaderFormat::SPIRV, loadShader("test.frag")}));
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.vertex = vertex;
    pipelineDesc.fragment = fragment;
    pipelineDesc.layout = layout;
    pipelineDesc.depthTest = true;
    pipelineDesc.vertexStride = sizeof(float) * 4;
    pipelineDesc.attributes = {{0, 0, VertexFormat::Float2}, {1, sizeof(float) * 2, VertexFormat::Float2}};
    auto pipeline = take(device->createGraphicsPipeline(pipelineDesc));
    auto cmd = take(device->createCommandList());
    done(cmd->begin());
    done(cmd->barrier(texture, ResourceState::Undefined, ResourceState::TransferDestination));
    done(cmd->copyBufferToTexture(upload, texture));
    done(cmd->barrier(texture, ResourceState::TransferDestination, ResourceState::ShaderRead));
    done(cmd->barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
    done(cmd->barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment));
    RenderTarget target;
    target.color = color;
    target.depth = depth;
    done(cmd->beginRendering(target));
    done(cmd->bindPipeline(pipeline));
    done(cmd->bindGroup(group));
    done(cmd->bindVertexBuffer(vertexBuffer));
    done(cmd->bindIndexBuffer(indexBuffer));
    done(cmd->drawIndexed(6));
    done(cmd->endRendering());
    done(cmd->barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
    done(cmd->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(cmd->copyTextureToBuffer(color, readback));
    done(cmd->end());
    auto fence = take(device->submit(cmd));
    done(fence->wait());
    std::array<uint8_t, width * height * 4> result{};
    done(readback->read(0, result.data(), result.size()));
    for (size_t i = 0; i < result.size(); i += 4) {
        expect(result[i] == 240 && result[i + 1] == 20 && result[i + 2] == 40 && result[i + 3] == 255,
               "Texture sampling or offscreen readback mismatch");
    }
}

void testStorageImage(const DevicePtr &device) {
    constexpr uint32_t width = 4, height = 4;
    auto texture = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::Storage | TextureUsage::TransferSource}));
    auto readback =
        take(device->createBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    auto layout = take(device->createBindGroupLayout({{0, BindingType::StorageTexture, 1, 4}}));
    auto group = take(device->createBindGroup({layout, {{0, {}, texture, {}}}}));
    auto shader =
        take(device->createShader({ShaderStage::Compute, ShaderFormat::SPIRV, loadShader("test_image.comp")}));
    auto pipeline = take(device->createComputePipeline({shader, layout}));
    auto cmd = take(device->createCommandList());
    done(cmd->begin());
    done(cmd->barrier(texture, ResourceState::Undefined, ResourceState::ShaderWrite));
    done(cmd->bindPipeline(pipeline));
    done(cmd->bindGroup(group));
    done(cmd->dispatch(1));
    done(cmd->barrier(texture, ResourceState::ShaderWrite, ResourceState::TransferSource));
    done(cmd->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(cmd->copyTextureToBuffer(texture, readback));
    done(cmd->end());
    auto fence = take(device->submit(cmd));
    done(fence->wait());
    std::array<uint8_t, width * height * 4> pixels{};
    done(readback->read(0, pixels.data(), pixels.size()));
    for (size_t i = 0; i < pixels.size(); i += 4)
        expect(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255,
               "Storage image output mismatch");
}

void testErrors(const DevicePtr &device) {
    expect(!device->createBuffer({0, BufferUsage::Storage, Memory::GPU}), "Zero-sized buffer should fail");
    expect(!createDevice({Backend::D3D12, {}, false}), "Unavailable backend should fail");
    auto upload = take(device->createBuffer({4, BufferUsage::TransferSource, Memory::Upload}));
    uint32_t value = 0;
    expect(!upload->read(0, &value, sizeof(value)), "Reading upload-only buffer should fail");
}
} // namespace

int main() {
    try {
        DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(createDevice(desc));
        const auto caps = device->capabilities();
        expect(caps.graphics && caps.compute && caps.textureSampling && caps.storageImage && caps.offscreenRendering,
               "Required RHI capabilities unavailable");
        testErrors(device);
        testCompute(device);
        testTextureAndOffscreen(device);
        testStorageImage(device);
        done(device->waitIdle());
        std::cout << "RHI GPU tests passed on " << device->adapterInfo().name << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RHI GPU test failed: " << e.what() << '\n';
        return 1;
    }
}
