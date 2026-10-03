#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>
#include <Core/StringInterop.hpp>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa::Rhi;

template <class T> T Take(Ursa::Result<T> r)
{
	if (!r)
	{
		throw std::runtime_error(Ursa::ToStdString(r.error));
	}
	return std::move(r.value);
}

void done(Ursa::Result<void> r)
{
	if (!r)
	{
		throw std::runtime_error(Ursa::ToStdString(r.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

Ursa::Vector<uint8_t> LoadShader(const char* name)
{
	std::ifstream file(std::string(URSA_SHADER_DIR) + "/" + name + ".spv", std::ios::binary | std::ios::ate);
	if (!file)
	{
		throw std::runtime_error(std::string("Cannot open shader ") + name);
	}
	Ursa::Vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!file)
	{
		throw std::runtime_error(std::string("Cannot read shader ") + name);
	}
	return bytes;
}

void TestCompute(const DevicePtr& device)
{
	constexpr size_t size = 64 * sizeof(uint32_t);
	auto output = Take(device->CreateBuffer({size, BufferUsage::storage | BufferUsage::transferSource, Memory::gpu}));
	auto readback = Take(device->CreateBuffer({size, BufferUsage::transferDestination, Memory::readback}));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::storageBuffer, 1, 4}}));
	auto group = Take(device->CreateBindGroup({layout, {{0, output, {}, {}}}}));
	auto shader = Take(device->CreateShader({ShaderStage::compute, ShaderFormat::spirv, LoadShader("Test.comp")}));
	auto pipeline = Take(device->CreateComputePipeline({shader, layout}));
	auto cmd = Take(device->CreateCommandList());
	done(cmd->Begin());
	done(cmd->Barrier(output, ResourceState::undefined, ResourceState::storageReadWrite));
	done(cmd->BindPipeline(pipeline));
	done(cmd->BindGroup(group));
	done(cmd->Dispatch(1));
	done(cmd->Barrier(output, ResourceState::storageReadWrite, ResourceState::transferSource));
	done(cmd->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
	done(cmd->CopyBuffer(output, readback, size));
	done(cmd->End());
	auto fence = Take(device->Submit(cmd));
	done(fence->Wait());
	Ursa::Array<uint32_t, 64> values{};
	done(readback->Read(0, values.data(), size));
	for (uint32_t i = 0; i < values.size(); ++i)
	{
		Expect(values[i] == i * 3 + 7, "Compute output mismatch");
	}
}

void TestTextureAndOffscreen(const DevicePtr& device, bool srgb)
{
	constexpr uint32_t width = 4, height = 4;
	Ursa::Array<uint8_t, 16> pixels{};
	for (size_t i = 0; i < pixels.size(); i += 4)
	{
		pixels[i] = srgb ? 128 : 240;
		pixels[i + 1] = srgb ? 128 : 20;
		pixels[i + 2] = srgb ? 128 : 40;
		pixels[i + 3] = 255;
	}
	auto upload = Take(device->CreateBuffer({pixels.size(), BufferUsage::transferSource, Memory::upload}));
	done(upload->Write(0, pixels.data(), pixels.size()));
	auto texture = Take(device->CreateTexture({2, 2, srgb ? Format::rgbA8Srgb : Format::rgbA8Unorm,
	                                           TextureUsage::sampled | TextureUsage::transferDestination}));
	auto color = Take(device->CreateTexture(
	    {width, height, Format::rgbA8Unorm, TextureUsage::colorAttachment | TextureUsage::transferSource}));
	auto depth = Take(device->CreateTexture({width, height, Format::d32Float, TextureUsage::depthAttachment}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::transferDestination, Memory::readback}));
	auto sampler = Take(device->CreateSampler({false, false}));
	constexpr Ursa::Array<float, 16> vertices{
	    {-1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f}};
	constexpr Ursa::Array<uint32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
	auto vertexBuffer = Take(device->CreateBuffer({sizeof(vertices), BufferUsage::vertex, Memory::upload}));
	auto indexBuffer = Take(device->CreateBuffer({sizeof(indices), BufferUsage::index, Memory::upload}));
	done(vertexBuffer->Write(0, vertices.data(), sizeof(vertices)));
	done(indexBuffer->Write(0, indices.data(), sizeof(indices)));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::combinedImageSampler, 1, 2}}));
	auto group = Take(device->CreateBindGroup({layout, {{0, {}, texture, sampler}}}));
	auto vertex = Take(device->CreateShader({ShaderStage::vertex, ShaderFormat::spirv, LoadShader("TestVertex.vert")}));
	auto fragment = Take(device->CreateShader({ShaderStage::fragment, ShaderFormat::spirv, LoadShader("Test.frag")}));
	GraphicsPipelineDesc pipelineDesc;
	pipelineDesc.vertex = vertex;
	pipelineDesc.fragment = fragment;
	pipelineDesc.layout = layout;
	pipelineDesc.depthTest = true;
	pipelineDesc.vertexStride = sizeof(float) * 4;
	pipelineDesc.attributes = {{0, 0, VertexFormat::float2}, {1, sizeof(float) * 2, VertexFormat::float2}};
	auto pipeline = Take(device->CreateGraphicsPipeline(pipelineDesc));
	auto cmd = Take(device->CreateCommandList());
	done(cmd->Begin());
	done(cmd->Barrier(texture, ResourceState::undefined, ResourceState::transferDestination));
	done(cmd->CopyBufferToTexture(upload, texture));
	done(cmd->Barrier(texture, ResourceState::transferDestination, ResourceState::shaderRead));
	done(cmd->Barrier(color, ResourceState::undefined, ResourceState::colorAttachment));
	done(cmd->Barrier(depth, ResourceState::undefined, ResourceState::depthAttachment));
	RenderTarget target;
	target.color = color;
	target.depth = depth;
	done(cmd->BeginRendering(target));
	done(cmd->BindPipeline(pipeline));
	done(cmd->BindGroup(group));
	done(cmd->BindVertexBuffer(vertexBuffer));
	done(cmd->BindIndexBuffer(indexBuffer));
	done(cmd->DrawIndexed(6));
	done(cmd->EndRendering());
	done(cmd->Barrier(color, ResourceState::colorAttachment, ResourceState::transferSource));
	done(cmd->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
	done(cmd->CopyTextureToBuffer(color, readback));
	done(cmd->End());
	auto fence = Take(device->Submit(cmd));
	done(fence->Wait());
	Ursa::Array<uint8_t, width * height * 4> result{};
	done(readback->Read(0, result.data(), result.size()));
	for (size_t i = 0; i < result.size(); i += 4)
	{
		Expect(result[i] == (srgb ? 55 : 240) && result[i + 1] == (srgb ? 55 : 20) &&
		           result[i + 2] == (srgb ? 55 : 40) && result[i + 3] == 255,
		       "Texture sampling or offscreen readback mismatch");
	}
}

void TestFirstInstance(const DevicePtr& device)
{
	constexpr uint32_t width = 64, height = 32;
	auto color = Take(device->CreateTexture(
	    {width, height, Format::rgbA8Unorm, TextureUsage::colorAttachment | TextureUsage::transferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::transferDestination, Memory::readback}));
	constexpr Ursa::Array<uint32_t, 3> indices{0, 1, 2};
	auto indexBuffer = Take(device->CreateBuffer({sizeof(indices), BufferUsage::index, Memory::upload}));
	done(indexBuffer->Write(0, indices.data(), sizeof(indices)));
	auto vertex =
	    Take(device->CreateShader({ShaderStage::vertex, ShaderFormat::spirv, LoadShader("TestInstance.vert")}));
	auto fragment =
	    Take(device->CreateShader({ShaderStage::fragment, ShaderFormat::spirv, LoadShader("TestInstance.frag")}));
	GraphicsPipelineDesc desc;
	desc.vertex = vertex;
	desc.fragment = fragment;
	auto pipeline = Take(device->CreateGraphicsPipeline(desc));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::undefined, ResourceState::colorAttachment));
	RenderTarget target;
	target.color = color;
	done(command->BeginRendering(target));
	done(command->BindPipeline(pipeline));
	done(command->BindIndexBuffer(indexBuffer));
	done(command->DrawIndexed(3, 1));
	done(command->DrawIndexed(3, 1, 1));
	done(command->EndRendering());
	done(command->Barrier(color, ResourceState::colorAttachment, ResourceState::transferSource));
	done(command->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Array<uint8_t, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	const size_t left = (height / 2 * width + width / 4) * 4;
	const size_t right = (height / 2 * width + width * 3 / 4) * 4;
	Expect(pixels[left] > 240 && pixels[left + 2] < 10, "First indexed instance is missing");
	Expect(pixels[right + 2] > 240 && pixels[right] < 10, "firstInstance did not select second instance");
}

void TestScissorAndIndexedOffsets(const DevicePtr& device)
{
	constexpr uint32_t width = 8, height = 8;
	auto color = Take(device->CreateTexture(
	    {width, height, Format::rgbA8Unorm, TextureUsage::colorAttachment | TextureUsage::transferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::transferDestination, Memory::readback}));
	constexpr Ursa::Array<float, 12> vertices{-1.f, -1.f, 0.f, -1.f, -0.5f, 1.f, 0.f, -1.f, 1.f, -1.f, 0.5f, 1.f};
	constexpr Ursa::Array<uint32_t, 4> indices{0, 0, 1, 2};
	auto vertexBuffer = Take(device->CreateBuffer({sizeof(vertices), BufferUsage::vertex, Memory::upload}));
	auto indexBuffer = Take(device->CreateBuffer({sizeof(indices), BufferUsage::index, Memory::upload}));
	done(vertexBuffer->Write(0, vertices.data(), sizeof(vertices)));
	done(indexBuffer->Write(0, indices.data(), sizeof(indices)));
	auto vertex = Take(device->CreateShader({ShaderStage::vertex, ShaderFormat::spirv, LoadShader("TestOffset.vert")}));
	auto fragment =
	    Take(device->CreateShader({ShaderStage::fragment, ShaderFormat::spirv, LoadShader("TestOffset.frag")}));
	GraphicsPipelineDesc desc;
	desc.vertex = vertex;
	desc.fragment = fragment;
	desc.vertexStride = sizeof(float) * 2;
	desc.attributes = {{0, 0, VertexFormat::float2}};
	auto pipeline = Take(device->CreateGraphicsPipeline(desc));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::undefined, ResourceState::colorAttachment));
	RenderTarget target;
	target.color = color;
	done(command->BeginRendering(target));
	done(command->BindPipeline(pipeline));
	done(command->BindVertexBuffer(vertexBuffer));
	done(command->BindIndexBuffer(indexBuffer));
	Expect(!command->SetScissor({0, 0, width + 1, height}), "Out-of-bounds scissor must fail");
	Expect(!command->DrawIndexed(3, 1, 0, 2, 3), "Out-of-bounds firstIndex must fail");
	done(command->SetScissor({4, 0, 4, height}));
	done(command->DrawIndexed(3, 1, 0, 1, 3));
	done(command->EndRendering());
	done(command->Barrier(color, ResourceState::colorAttachment, ResourceState::transferSource));
	done(command->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());

	Ursa::Array<uint8_t, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	const size_t left = (height / 2 * width + 1) * 4;
	const size_t right = (height / 2 * width + 6) * 4;
	Expect(pixels[left + 1] < 100, "Scissor failed to clip the left side");
	Expect(pixels[right + 1] > 240 && pixels[right] < 10, "Indexed offsets failed to draw in the scissor");
}

void TestStorageImage(const DevicePtr& device)
{
	constexpr uint32_t width = 4, height = 4;
	auto texture = Take(device->CreateTexture(
	    {width, height, Format::rgbA8Unorm, TextureUsage::storage | TextureUsage::transferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::transferDestination, Memory::readback}));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::storageTexture, 1, 4}}));
	auto group = Take(device->CreateBindGroup({layout, {{0, {}, texture, {}}}}));
	auto shader = Take(device->CreateShader({ShaderStage::compute, ShaderFormat::spirv, LoadShader("TestImage.comp")}));
	auto pipeline = Take(device->CreateComputePipeline({shader, layout}));
	auto cmd = Take(device->CreateCommandList());
	done(cmd->Begin());
	done(cmd->Barrier(texture, ResourceState::undefined, ResourceState::shaderWrite));
	done(cmd->BindPipeline(pipeline));
	done(cmd->BindGroup(group));
	done(cmd->Dispatch(1));
	done(cmd->Barrier(texture, ResourceState::shaderWrite, ResourceState::transferSource));
	done(cmd->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
	done(cmd->CopyTextureToBuffer(texture, readback));
	done(cmd->End());
	auto fence = Take(device->Submit(cmd));
	done(fence->Wait());
	Ursa::Array<uint8_t, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	for (size_t i = 0; i < pixels.size(); i += 4)
	{
		Expect(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255,
		       "Storage image output mismatch");
	}
}

void TestErrors(const DevicePtr& device)
{
	Expect(!device->CreateBuffer({0, BufferUsage::storage, Memory::gpu}), "Zero-sized buffer should fail");
	const auto unavailable = CreateDevice({Backend::d3D12, {}, false});
	Expect(!unavailable && !unavailable.error.empty(), "Unavailable backend lacked an initialization diagnostic");
	const auto badTexture = device->CreateTexture({0, 1, Format::rgbA8Unorm, TextureUsage::sampled});
	Expect(!badTexture && badTexture.error.find("dimensions") != Ursa::String::npos,
	       "Invalid texture dimensions lacked a diagnostic");
	const auto badSrgb = device->CreateTexture({1, 1, Format::rgbA8Srgb, TextureUsage::storage});
	Expect(!badSrgb && badSrgb.error.find("sRGB") != Ursa::String::npos,
	       "Invalid sRGB storage texture lacked a diagnostic");
	const auto badPipeline = device->CreateGraphicsPipeline({});
	Expect(!badPipeline && badPipeline.error.find("shader") != Ursa::String::npos,
	       "Incomplete graphics pipeline lacked a shader diagnostic");
	auto upload = Take(device->CreateBuffer({4, BufferUsage::transferSource, Memory::upload}));
	uint32_t value = 0;
	Expect(!upload->Read(0, &value, sizeof(value)), "Reading upload-only buffer should fail");
}
} // namespace

int main()
{
	try
	{
		DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(CreateDevice(desc));
		const auto caps = device->Capabilities();
		Expect(caps.graphics && caps.compute && caps.textureSampling && caps.storageImage && caps.offscreenRendering,
		       "Required RHI capabilities unavailable");
		TestErrors(device);
		TestCompute(device);
		TestTextureAndOffscreen(device, false);
		TestTextureAndOffscreen(device, true);
		TestFirstInstance(device);
		TestScissorAndIndexedOffsets(device);
		TestStorageImage(device);
		done(device->WaitIdle());
		std::cout << "RHI GPU tests passed on " << Ursa::ToStdString(device->AdapterInfo().name) << '\n';
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "RHI GPU test failed: " << e.what() << '\n';
		return 1;
	}
}
