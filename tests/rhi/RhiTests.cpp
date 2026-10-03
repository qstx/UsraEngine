#include <core/Containers.hpp>
#include <rhi/RHI.hpp>
#include <core/StringInterop.hpp>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa::rhi;

template <class T> T take(ursa::Result<T> r) {
    if (!r)
        throw std::runtime_error(ursa::toStdString(r.error));
    return std::move(r.value);
}

void done(ursa::Result<void> r) {
    if (!r)
        throw std::runtime_error(ursa::toStdString(r.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

ursa::Vector<uint8_t> loadShader(const char *name) {
    std::ifstream file(std::string(URSA_SHADER_DIR) + "/" + name + ".spv", std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error(std::string("Cannot open shader ") + name);
    ursa::Vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
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
    ursa::Array<uint32_t, 64> values{};
    done(readback->read(0, values.data(), size));
    for (uint32_t i = 0; i < values.size(); ++i)
        expect(values[i] == i * 3 + 7, "Compute output mismatch");
}

void testTextureAndOffscreen(const DevicePtr &device, bool srgb) {
    constexpr uint32_t width = 4, height = 4;
    ursa::Array<uint8_t, 16> pixels{};
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = srgb ? 128 : 240;
        pixels[i + 1] = srgb ? 128 : 20;
        pixels[i + 2] = srgb ? 128 : 40;
        pixels[i + 3] = 255;
    }
    auto upload = take(device->createBuffer({pixels.size(), BufferUsage::TransferSource, Memory::Upload}));
    done(upload->write(0, pixels.data(), pixels.size()));
    auto texture = take(device->createTexture({2, 2, srgb ? Format::RGBA8Srgb : Format::RGBA8Unorm,
                                               TextureUsage::Sampled | TextureUsage::TransferDestination}));
    auto color = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    auto depth = take(device->createTexture({width, height, Format::D32Float, TextureUsage::DepthAttachment}));
    auto readback =
        take(device->createBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    auto sampler = take(device->createSampler({false, false}));
    constexpr ursa::Array<float, 16> vertices{
        {-1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f}};
    constexpr ursa::Array<uint32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
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
    ursa::Array<uint8_t, width * height * 4> result{};
    done(readback->read(0, result.data(), result.size()));
    for (size_t i = 0; i < result.size(); i += 4) {
        expect(result[i] == (srgb ? 55 : 240) && result[i + 1] == (srgb ? 55 : 20) &&
                   result[i + 2] == (srgb ? 55 : 40) && result[i + 3] == 255,
               "Texture sampling or offscreen readback mismatch");
    }
}

void testFirstInstance(const DevicePtr &device) {
    constexpr uint32_t width = 64, height = 32;
    auto color = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    auto readback =
        take(device->createBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    constexpr ursa::Array<uint32_t, 3> indices{0, 1, 2};
    auto indexBuffer = take(device->createBuffer({sizeof(indices), BufferUsage::Index, Memory::Upload}));
    done(indexBuffer->write(0, indices.data(), sizeof(indices)));
    auto vertex =
        take(device->createShader({ShaderStage::Vertex, ShaderFormat::SPIRV, loadShader("test_instance.vert")}));
    auto fragment =
        take(device->createShader({ShaderStage::Fragment, ShaderFormat::SPIRV, loadShader("test_instance.frag")}));
    GraphicsPipelineDesc desc;
    desc.vertex = vertex;
    desc.fragment = fragment;
    auto pipeline = take(device->createGraphicsPipeline(desc));
    auto command = take(device->createCommandList());
    done(command->begin());
    done(command->barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
    RenderTarget target;
    target.color = color;
    done(command->beginRendering(target));
    done(command->bindPipeline(pipeline));
    done(command->bindIndexBuffer(indexBuffer));
    done(command->drawIndexed(3, 1));
    done(command->drawIndexed(3, 1, 1));
    done(command->endRendering());
    done(command->barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
    done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(command->copyTextureToBuffer(color, readback));
    done(command->end());
    done(take(device->submit(command))->wait());
    ursa::Array<uint8_t, width * height * 4> pixels{};
    done(readback->read(0, pixels.data(), pixels.size()));
    const size_t left = (height / 2 * width + width / 4) * 4;
    const size_t right = (height / 2 * width + width * 3 / 4) * 4;
    expect(pixels[left] > 240 && pixels[left + 2] < 10, "First indexed instance is missing");
    expect(pixels[right + 2] > 240 && pixels[right] < 10, "firstInstance did not select second instance");
}

void testScissorAndIndexedOffsets(const DevicePtr &device) {
    constexpr uint32_t width = 8, height = 8;
    auto color = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    auto readback = take(device->createBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    constexpr ursa::Array<float, 12> vertices{
        -1.f, -1.f, 0.f, -1.f, -0.5f, 1.f,
         0.f, -1.f, 1.f, -1.f,  0.5f, 1.f
    };
    constexpr ursa::Array<uint32_t, 4> indices{0, 0, 1, 2};
    auto vertexBuffer = take(device->createBuffer({sizeof(vertices), BufferUsage::Vertex, Memory::Upload}));
    auto indexBuffer = take(device->createBuffer({sizeof(indices), BufferUsage::Index, Memory::Upload}));
    done(vertexBuffer->write(0, vertices.data(), sizeof(vertices)));
    done(indexBuffer->write(0, indices.data(), sizeof(indices)));
    auto vertex = take(device->createShader({ShaderStage::Vertex, ShaderFormat::SPIRV,
                                             loadShader("test_offset.vert")}));
    auto fragment = take(device->createShader({ShaderStage::Fragment, ShaderFormat::SPIRV,
                                               loadShader("test_offset.frag")}));
    GraphicsPipelineDesc desc;
    desc.vertex = vertex;
    desc.fragment = fragment;
    desc.vertexStride = sizeof(float) * 2;
    desc.attributes = {{0, 0, VertexFormat::Float2}};
    auto pipeline = take(device->createGraphicsPipeline(desc));
    auto command = take(device->createCommandList());
    done(command->begin());
    done(command->barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
    RenderTarget target;
    target.color = color;
    done(command->beginRendering(target));
    done(command->bindPipeline(pipeline));
    done(command->bindVertexBuffer(vertexBuffer));
    done(command->bindIndexBuffer(indexBuffer));
    expect(!command->setScissor({0, 0, width + 1, height}), "Out-of-bounds scissor must fail");
    expect(!command->drawIndexed(3, 1, 0, 2, 3), "Out-of-bounds firstIndex must fail");
    done(command->setScissor({4, 0, 4, height}));
    done(command->drawIndexed(3, 1, 0, 1, 3));
    done(command->endRendering());
    done(command->barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
    done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(command->copyTextureToBuffer(color, readback));
    done(command->end());
    done(take(device->submit(command))->wait());

    ursa::Array<uint8_t, width * height * 4> pixels{};
    done(readback->read(0, pixels.data(), pixels.size()));
    const size_t left = (height / 2 * width + 1) * 4;
    const size_t right = (height / 2 * width + 6) * 4;
    expect(pixels[left + 1] < 100, "Scissor failed to clip the left side");
    expect(pixels[right + 1] > 240 && pixels[right] < 10, "Indexed offsets failed to draw in the scissor");
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
    ursa::Array<uint8_t, width * height * 4> pixels{};
    done(readback->read(0, pixels.data(), pixels.size()));
    for (size_t i = 0; i < pixels.size(); i += 4)
        expect(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255,
               "Storage image output mismatch");
}

void testErrors(const DevicePtr &device) {
    expect(!device->createBuffer({0, BufferUsage::Storage, Memory::GPU}), "Zero-sized buffer should fail");
    const auto unavailable = createDevice({Backend::D3D12, {}, false});
    expect(!unavailable && !unavailable.error.empty(), "Unavailable backend lacked an initialization diagnostic");
    const auto badTexture = device->createTexture({0, 1, Format::RGBA8Unorm, TextureUsage::Sampled});
    expect(!badTexture && badTexture.error.find("dimensions") != ursa::String::npos,
           "Invalid texture dimensions lacked a diagnostic");
    const auto badSrgb = device->createTexture({1, 1, Format::RGBA8Srgb, TextureUsage::Storage});
    expect(!badSrgb && badSrgb.error.find("sRGB") != ursa::String::npos,
           "Invalid sRGB storage texture lacked a diagnostic");
    const auto badPipeline = device->createGraphicsPipeline({});
    expect(!badPipeline && badPipeline.error.find("shader") != ursa::String::npos,
           "Incomplete graphics pipeline lacked a shader diagnostic");
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
        testTextureAndOffscreen(device, false);
        testTextureAndOffscreen(device, true);
        testFirstInstance(device);
        testScissorAndIndexedOffsets(device);
        testStorageImage(device);
        done(device->waitIdle());
        std::cout << "RHI GPU tests passed on " << ursa::toStdString(device->adapterInfo().name) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RHI GPU test failed: " << e.what() << '\n';
        return 1;
    }
}
