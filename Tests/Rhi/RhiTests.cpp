#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>
#include <Core/StringInterop.hpp>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
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

Ursa::Vector<Ursa::UInt8> LoadShader(const char* name)
{
	std::ifstream file(std::string(URSA_SHADER_DIR) + "/" + name + ".spv", std::ios::binary | std::ios::ate);
	if (!file)
	{
		throw std::runtime_error(std::string("Cannot open shader ") + name);
	}
	Ursa::Vector<Ursa::UInt8> bytes(static_cast<Ursa::Size>(file.tellg()));
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
	constexpr Ursa::Size size = 64 * sizeof(Ursa::UInt32);
	auto output = Take(device->CreateBuffer({size, BufferUsage::Storage | BufferUsage::TransferSource, Memory::Gpu}));
	auto readback = Take(device->CreateBuffer({size, BufferUsage::TransferDestination, Memory::Readback}));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::StorageBuffer, 1, 4}}));
	auto group = Take(device->CreateBindGroup({layout, {{0, output, {}, {}}}}));
	auto shader = Take(device->CreateShader({ShaderStage::Compute, ShaderFormat::Spirv, LoadShader("Test.comp")}));
	auto pipeline = Take(device->CreateComputePipeline({shader, layout}));
	auto cmd = Take(device->CreateCommandList());
	done(cmd->Begin());
	done(cmd->Barrier(output, ResourceState::Undefined, ResourceState::StorageReadWrite));
	done(cmd->BindPipeline(pipeline));
	done(cmd->BindGroup(group));
	done(cmd->Dispatch(1));
	done(cmd->Barrier(output, ResourceState::StorageReadWrite, ResourceState::TransferSource));
	done(cmd->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(cmd->CopyBuffer(output, readback, size));
	done(cmd->End());
	auto fence = Take(device->Submit(cmd));
	done(fence->Wait());
	Ursa::Array<Ursa::UInt32, 64> values{};
	done(readback->Read(0, values.data(), size));
	for (Ursa::UInt32 i = 0; i < values.size(); ++i)
	{
		Expect(values[i] == i * 3 + 7, "Compute output mismatch");
	}
}

void TestTextureAndOffscreen(const DevicePtr& device, bool srgb)
{
	constexpr Ursa::UInt32 width = 4, height = 4;
	Ursa::Array<Ursa::UInt8, 16> pixels{};
	for (Ursa::Size i = 0; i < pixels.size(); i += 4)
	{
		pixels[i] = srgb ? 128 : 240;
		pixels[i + 1] = srgb ? 128 : 20;
		pixels[i + 2] = srgb ? 128 : 40;
		pixels[i + 3] = 255;
	}
	auto upload = Take(device->CreateBuffer({pixels.size(), BufferUsage::TransferSource, Memory::Upload}));
	done(upload->Write(0, pixels.data(), pixels.size()));
	auto texture = Take(device->CreateTexture({2, 2, srgb ? Format::RgbA8Srgb : Format::RgbA8Unorm,
	                                           TextureUsage::Sampled | TextureUsage::TransferDestination}));
	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto depth = Take(device->CreateTexture({width, height, Format::D32Float, TextureUsage::DepthAttachment}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	auto sampler = Take(device->CreateSampler({false, false}));
	constexpr Ursa::Array<Ursa::Float32, 16> vertices{
	    {-1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f}};
	constexpr Ursa::Array<Ursa::UInt32, 6> indices{{0, 1, 2, 0, 2, 3}};
	auto vertexBuffer = Take(device->CreateBuffer({sizeof(vertices), BufferUsage::Vertex, Memory::Upload}));
	auto indexBuffer = Take(device->CreateBuffer({sizeof(indices), BufferUsage::Index, Memory::Upload}));
	done(vertexBuffer->Write(0, vertices.data(), sizeof(vertices)));
	done(indexBuffer->Write(0, indices.data(), sizeof(indices)));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::CombinedImageSampler, 1, 2}}));
	auto group = Take(device->CreateBindGroup({layout, {{0, {}, texture, sampler}}}));
	auto vertex = Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("TestVertex.vert")}));
	auto fragment = Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("Test.frag")}));
	GraphicsPipelineDesc pipelineDesc;
	pipelineDesc.vertex = vertex;
	pipelineDesc.fragment = fragment;
	pipelineDesc.layout = layout;
	pipelineDesc.depthTest = true;
	pipelineDesc.vertexStride = sizeof(Ursa::Float32) * 4;
	pipelineDesc.attributes = {{0, 0, VertexFormat::Float2}, {1, sizeof(Ursa::Float32) * 2, VertexFormat::Float2}};
	auto pipeline = Take(device->CreateGraphicsPipeline(pipelineDesc));
	auto cmd = Take(device->CreateCommandList());
	done(cmd->Begin());
	done(cmd->Barrier(texture, ResourceState::Undefined, ResourceState::TransferDestination));
	done(cmd->CopyBufferToTexture(upload, texture));
	done(cmd->Barrier(texture, ResourceState::TransferDestination, ResourceState::ShaderRead));
	done(cmd->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(cmd->Barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment));
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
	done(cmd->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(cmd->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(cmd->CopyTextureToBuffer(color, readback));
	done(cmd->End());
	auto fence = Take(device->Submit(cmd));
	done(fence->Wait());
	Ursa::Array<Ursa::UInt8, width * height * 4> result{};
	done(readback->Read(0, result.data(), result.size()));
	for (Ursa::Size i = 0; i < result.size(); i += 4)
	{
		Expect(result[i] == (srgb ? 55 : 240) && result[i + 1] == (srgb ? 55 : 20) &&
		           result[i + 2] == (srgb ? 55 : 40) && result[i + 3] == 255,
		       "Texture sampling or offscreen readback mismatch");
	}
}

void TestFirstInstance(const DevicePtr& device)
{
	constexpr Ursa::UInt32 width = 64, height = 32;
	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	constexpr Ursa::Array<Ursa::UInt32, 3> indices{0, 1, 2};
	auto indexBuffer = Take(device->CreateBuffer({sizeof(indices), BufferUsage::Index, Memory::Upload}));
	done(indexBuffer->Write(0, indices.data(), sizeof(indices)));
	auto vertex =
	    Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("TestInstance.vert")}));
	auto fragment =
	    Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("TestInstance.frag")}));
	GraphicsPipelineDesc desc;
	desc.vertex = vertex;
	desc.fragment = fragment;
	auto pipeline = Take(device->CreateGraphicsPipeline(desc));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	RenderTarget target;
	target.color = color;
	done(command->BeginRendering(target));
	done(command->BindPipeline(pipeline));
	done(command->BindIndexBuffer(indexBuffer));
	done(command->DrawIndexed(3, 1));
	done(command->DrawIndexed(3, 1, 1));
	done(command->EndRendering());
	done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Array<Ursa::UInt8, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	const Ursa::Size left = (height / 2 * width + width / 4) * 4;
	const Ursa::Size right = (height / 2 * width + width * 3 / 4) * 4;
	Expect(pixels[left] > 240 && pixels[left + 2] < 10, "First indexed instance is missing");
	Expect(pixels[right + 2] > 240 && pixels[right] < 10, "firstInstance did not select second instance");
}

void TestScissorAndIndexedOffsets(const DevicePtr& device)
{
	constexpr Ursa::UInt32 width = 8, height = 8;
	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	constexpr Ursa::Array<Ursa::Float32, 12> vertices{-1.f, -1.f, 0.f, -1.f, -0.5f, 1.f,
	                                                  0.f,  -1.f, 1.f, -1.f, 0.5f,  1.f};
	constexpr Ursa::Array<Ursa::UInt32, 4> indices{0, 0, 1, 2};
	auto vertexBuffer = Take(device->CreateBuffer({sizeof(vertices), BufferUsage::Vertex, Memory::Upload}));
	auto indexBuffer = Take(device->CreateBuffer({sizeof(indices), BufferUsage::Index, Memory::Upload}));
	done(vertexBuffer->Write(0, vertices.data(), sizeof(vertices)));
	done(indexBuffer->Write(0, indices.data(), sizeof(indices)));
	auto vertex = Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("TestOffset.vert")}));
	auto fragment =
	    Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("TestOffset.frag")}));
	GraphicsPipelineDesc desc;
	desc.vertex = vertex;
	desc.fragment = fragment;
	desc.vertexStride = sizeof(Ursa::Float32) * 2;
	desc.attributes = {{0, 0, VertexFormat::Float2}};
	auto pipeline = Take(device->CreateGraphicsPipeline(desc));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
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
	done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());

	Ursa::Array<Ursa::UInt8, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	const Ursa::Size left = (height / 2 * width + 1) * 4;
	const Ursa::Size right = (height / 2 * width + 6) * 4;
	Expect(pixels[left + 1] < 100, "Scissor failed to clip the left side");
	Expect(pixels[right + 1] > 240 && pixels[right] < 10, "Indexed offsets failed to draw in the scissor");
}

void TestStorageImage(const DevicePtr& device)
{
	constexpr Ursa::UInt32 width = 4, height = 4;
	auto texture = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::Storage | TextureUsage::TransferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::StorageTexture, 1, 4}}));
	auto group = Take(device->CreateBindGroup({layout, {{0, {}, texture, {}}}}));
	auto shader = Take(device->CreateShader({ShaderStage::Compute, ShaderFormat::Spirv, LoadShader("TestImage.comp")}));
	auto pipeline = Take(device->CreateComputePipeline({shader, layout}));
	auto cmd = Take(device->CreateCommandList());
	done(cmd->Begin());
	done(cmd->Barrier(texture, ResourceState::Undefined, ResourceState::ShaderWrite));
	done(cmd->BindPipeline(pipeline));
	done(cmd->BindGroup(group));
	done(cmd->Dispatch(1));
	done(cmd->Barrier(texture, ResourceState::ShaderWrite, ResourceState::TransferSource));
	done(cmd->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(cmd->CopyTextureToBuffer(texture, readback));
	done(cmd->End());
	auto fence = Take(device->Submit(cmd));
	done(fence->Wait());
	Ursa::Array<Ursa::UInt8, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	for (Ursa::Size i = 0; i < pixels.size(); i += 4)
	{
		Expect(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255,
		       "Storage image output mismatch");
	}
}

void TestErrors(const DevicePtr& device)
{
	Expect(!device->CreateBuffer({0, BufferUsage::Storage, Memory::Gpu}), "Zero-sized buffer should fail");
	const auto unavailable = CreateDevice({Backend::D3D12, {}, false});
	Expect(!unavailable && !unavailable.error.empty(), "Unavailable backend lacked an initialization diagnostic");
	const auto badTexture = device->CreateTexture({0, 1, Format::RgbA8Unorm, TextureUsage::Sampled});
	Expect(!badTexture && badTexture.error.find("dimensions") != Ursa::String::npos,
	       "Invalid texture dimensions lacked a diagnostic");
	const auto badSrgb = device->CreateTexture({1, 1, Format::RgbA8Srgb, TextureUsage::Storage});
	Expect(!badSrgb && badSrgb.error.find("sRGB") != Ursa::String::npos,
	       "Invalid sRGB storage texture lacked a diagnostic");
	const auto badPipeline = device->CreateGraphicsPipeline({});
	Expect(!badPipeline && badPipeline.error.find("shader") != Ursa::String::npos,
	       "Incomplete graphics pipeline lacked a shader diagnostic");
	auto upload = Take(device->CreateBuffer({4, BufferUsage::TransferSource, Memory::Upload}));
	Ursa::UInt32 value = 0;
	Expect(!upload->Read(0, &value, sizeof(value)), "Reading upload-only buffer should fail");
}

void TestShaderLabPipelineStates(const DevicePtr& device)
{
	auto vertex = Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("State.vert")}));
	auto fragment = Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("State.frag")}));
	GraphicsPipelineDesc desc;
	desc.vertex = vertex;
	desc.fragment = fragment;
	desc.depthTest = true;
	desc.depthWrite = false;
	desc.depthCompare = Compare::GreaterEqual;
	desc.depthFormat = Format::D32FloatS8Uint;
	desc.cullMode = CullMode::Front;
	desc.blendState.enabled = true;
	desc.blendState.sourceColor = BlendFactor::SourceAlpha;
	desc.blendState.destinationColor = BlendFactor::OneMinusSourceAlpha;
	desc.blendState.colorOperation = BlendOperation::ReverseSubtract;
	desc.blendState.sourceAlpha = BlendFactor::One;
	desc.blendState.destinationAlpha = BlendFactor::Zero;
	desc.blendState.alphaOperation = BlendOperation::Maximum;
	desc.stencil.enabled = true;
	desc.stencil.reference = 3;
	desc.stencil.readMask = 0x7f;
	desc.stencil.writeMask = 0x3f;
	desc.stencil.front.compare = Compare::LessEqual;
	desc.stencil.front.failOperation = StencilOperation::Keep;
	desc.stencil.front.passOperation = StencilOperation::Replace;
	desc.stencil.front.depthFailOperation = StencilOperation::IncrementClamp;
	desc.stencil.back.compare = Compare::NotEqual;
	desc.stencil.back.failOperation = StencilOperation::Zero;
	desc.stencil.back.passOperation = StencilOperation::Invert;
	desc.stencil.back.depthFailOperation = StencilOperation::DecrementWrap;
	desc.colorWriteMask = ColorWriteMask::Red | ColorWriteMask::Alpha;
	desc.depthBiasConstant = 1;
	desc.depthBiasSlope = 1.0f;
	Take(device->CreateGraphicsPipeline(desc));

	GraphicsPipelineDesc noStencilFormat = desc;
	noStencilFormat.depthFormat = Format::D32Float;
	auto rejectedStencil = device->CreateGraphicsPipeline(noStencilFormat);
	Expect(!rejectedStencil && rejectedStencil.error.find("Stencil") != Ursa::String::npos,
	       "Stencil state without a stencil-capable format lacked a diagnostic");

	GraphicsPipelineDesc invalidMask = desc;
	invalidMask.colorWriteMask = static_cast<ColorWriteMask>(0x10);
	auto rejectedMask = device->CreateGraphicsPipeline(invalidMask);
	Expect(!rejectedMask && rejectedMask.error.find("Color write mask") != Ursa::String::npos,
	       "Invalid color write mask lacked a diagnostic");

	GraphicsPipelineDesc invalidBlend = desc;
	invalidBlend.colorFormat = Format::R32Uint;
	auto rejectedBlend = device->CreateGraphicsPipeline(invalidBlend);
	Expect(!rejectedBlend && rejectedBlend.error.find("integer") != Ursa::String::npos,
	       "Integer target blending lacked a diagnostic");

	GraphicsPipelineDesc invalidDepthBias = desc;
	invalidDepthBias.depthBiasSlope = std::numeric_limits<Ursa::Float32>::quiet_NaN();
	auto rejectedDepthBias = device->CreateGraphicsPipeline(invalidDepthBias);
	Expect(!rejectedDepthBias && rejectedDepthBias.error.find("Depth bias") != Ursa::String::npos,
	       "Non-finite depth bias lacked a diagnostic");

	auto color = Take(device->CreateTexture(
	    {4, 4, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto depthStencil = Take(device->CreateTexture({4, 4, Format::D32FloatS8Uint, TextureUsage::DepthAttachment}));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(command->Barrier(depthStencil, ResourceState::Undefined, ResourceState::DepthAttachment));
	RenderTarget target;
	target.color = color;
	target.depth = depthStencil;
	target.clearStencil = true;
	target.stencilValue = 3;
	done(command->BeginRendering(target));
	done(command->EndRendering());
	done(command->End());
	done(Take(device->Submit(command))->Wait());
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
		TestShaderLabPipelineStates(device);
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
