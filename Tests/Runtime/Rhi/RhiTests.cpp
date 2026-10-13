#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Rhi/RHI.hpp>
#include <Core/StringInterop.hpp>
#include <Window/Window.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

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

void TestTextureAndOffscreen(const DevicePtr& device, bool srgb, bool partialViewport = false)
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
	if (partialViewport)
	{
		std::fill(target.colorValue, target.colorValue + 3, 0.0f);
	}
	done(cmd->BeginRendering(target));
	if (partialViewport)
	{
		Expect(!cmd->SetViewport({0, 0, -1, 1}), "Negative viewport extent accepted");
		Expect(!cmd->SetViewport({0, 0, 5, 4}), "Out-of-bounds viewport accepted");
		done(cmd->SetViewport({2, 1, 2, 2}));
	}
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
		const Ursa::Size x = (i / 4) % width, y = (i / 4) / width;
		if (partialViewport && (x < 2 || y < 1 || y >= 3))
		{
			Expect(result[i] == 0 && result[i + 1] == 0 && result[i + 2] == 0 && result[i + 3] == 255,
			       "Binding a pipeline overwrote the explicit viewport");
			continue;
		}
		Expect(result[i] == (srgb ? 55 : 240) && result[i + 1] == (srgb ? 55 : 20) &&
		           result[i + 2] == (srgb ? 55 : 40) && result[i + 3] == 255,
		       "Texture sampling or offscreen readback mismatch");
	}

	auto feedbackTexture = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::Sampled}));
	auto feedbackDepth = Take(device->CreateTexture({width, height, Format::D32Float, TextureUsage::DepthAttachment}));
	auto feedbackGroup = Take(device->CreateBindGroup({layout, {{0, {}, feedbackTexture, sampler}}}));
	auto feedbackCommand = Take(device->CreateCommandList());
	done(feedbackCommand->Begin());
	done(feedbackCommand->Barrier(feedbackTexture, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(feedbackCommand->Barrier(feedbackDepth, ResourceState::Undefined, ResourceState::DepthAttachment));
	RenderTarget feedbackTarget;
	feedbackTarget.color = feedbackTexture;
	feedbackTarget.depth = feedbackDepth;
	done(feedbackCommand->BeginRendering(feedbackTarget));
	done(feedbackCommand->BindPipeline(pipeline));
	Expect(!feedbackCommand->BindGroup(feedbackGroup), "Render target feedback binding was accepted");
	done(feedbackCommand->EndRendering());
	done(feedbackCommand->End());
	done(Take(device->Submit(feedbackCommand))->Wait());
}

void TestTextureMipUploads(const DevicePtr& device)
{
	constexpr Ursa::UInt32 textureWidth = 2;
	constexpr Ursa::UInt32 textureHeight = 2;
	constexpr Ursa::Size baseLevelBytes = textureWidth * textureHeight * 4;
	constexpr Ursa::Array<Ursa::UInt8, baseLevelBytes + 4> pixels{10, 20,  30,  255, 40,  50,  60,  255, 70,  80,
	                                                              90, 255, 100, 110, 120, 255, 130, 140, 150, 255};
	auto upload = Take(device->CreateBuffer({pixels.size(), BufferUsage::TransferSource, Memory::Upload}));
	done(upload->Write(0, pixels.data(), pixels.size()));
	auto texture = Take(device->CreateTexture(
	    {textureWidth, textureHeight, Format::RgbA8Unorm,
	     TextureUsage::Sampled | TextureUsage::TransferSource | TextureUsage::TransferDestination, 2}));
	auto baseReadback =
	    Take(device->CreateBuffer({baseLevelBytes, BufferUsage::TransferDestination, Memory::Readback}));
	auto mipReadback = Take(device->CreateBuffer({4, BufferUsage::TransferDestination, Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(texture, {0, 1}, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->Barrier(texture, {1, 1}, ResourceState::Undefined, ResourceState::TransferDestination));
	Expect(!command->Barrier(texture, {1, 1}, ResourceState::Undefined, ResourceState::ShaderRead),
	       "Texture subresource state tracking accepted a mismatched before-state");
	done(command->CopyBufferToTexture(upload, texture, 0, 0));
	done(command->CopyBufferToTexture(upload, texture, 1, baseLevelBytes));
	done(command->Barrier(texture, {0, 1}, ResourceState::TransferDestination, ResourceState::TransferSource));
	done(command->Barrier(texture, {1, 1}, ResourceState::TransferDestination, ResourceState::TransferSource));
	done(command->Barrier(baseReadback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->Barrier(mipReadback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->CopyTextureToBuffer(texture, baseReadback, 0));
	done(command->CopyTextureToBuffer(texture, mipReadback, 1));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Array<Ursa::UInt8, baseLevelBytes> baseResult{};
	Ursa::Array<Ursa::UInt8, 4> mipResult{};
	done(baseReadback->Read(0, baseResult.data(), baseResult.size()));
	done(mipReadback->Read(0, mipResult.data(), mipResult.size()));
	Expect(std::equal(baseResult.begin(), baseResult.end(), pixels.begin()) &&
	           std::equal(mipResult.begin(), mipResult.end(), pixels.begin() + baseLevelBytes),
	       "Texture mip upload or readback returned incorrect level data");
}

void TestHdrAndDepth(const DevicePtr& device)
{
	const auto hdrCaps = Take(device->QueryFormat(Format::RgbA16Float));
	const auto depthCaps = Take(device->QueryFormat(Format::D32Float));
	Expect(hdrCaps.colorAttachment && hdrCaps.sampled && hdrCaps.transferSource && depthCaps.depthAttachment &&
	           depthCaps.sampled && depthCaps.transferSource,
	       "Required HDR/depth format capabilities unavailable");
	Expect(!device->QueryFormat(static_cast<Format>(999)), "Unknown format query was accepted");
	Expect(!device->CreateTexture({2, 2, Format::RgbA8Unorm, TextureUsage::DepthAttachment}),
	       "Color format with depth usage was accepted");
	auto hdr = Take(
	    device->CreateTexture({2, 2, Format::RgbA16Float,
	                           TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSource}));
	auto depth = Take(
	    device->CreateTexture({2, 2, Format::D32Float,
	                           TextureUsage::DepthAttachment | TextureUsage::Sampled | TextureUsage::TransferSource}));
	auto color = Take(device->CreateTexture(
	    {2, 2, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto hdrReadback = Take(device->CreateBuffer({32, BufferUsage::TransferDestination, Memory::Readback}));
	auto depthReadback = Take(device->CreateBuffer({16, BufferUsage::TransferDestination, Memory::Readback}));
	auto colorReadback = Take(device->CreateBuffer({16, BufferUsage::TransferDestination, Memory::Readback}));
	auto layout = Take(device->CreateBindGroupLayout({{0, BindingType::CombinedImageSampler, 1, 2}}));
	auto sampler = Take(device->CreateSampler({false, false}));
	auto group = Take(device->CreateBindGroup({layout, {{0, {}, depth, sampler}}}));
	GraphicsPipelineDesc pipelineDesc;
	pipelineDesc.vertex =
	    Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("DepthSample.vert")}));
	pipelineDesc.fragment =
	    Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("DepthSample.frag")}));
	pipelineDesc.layout = layout;
	auto pipeline = Take(device->CreateGraphicsPipeline(pipelineDesc));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(hdr, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(command->Barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment));
	RenderTarget target;
	target.color = hdr;
	target.depth = depth;
	target.colorValue[0] = 2;
	target.colorValue[1] = 0.5f;
	target.colorValue[2] = 0.25f;
	target.depthValue = 0.25f;
	done(command->BeginRendering(target));
	done(command->EndRendering());
	done(command->Barrier(depth, ResourceState::DepthAttachment, ResourceState::ShaderRead));
	done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	RenderTarget sampled;
	sampled.color = color;
	done(command->BeginRendering(sampled));
	done(command->BindPipeline(pipeline));
	done(command->BindGroup(group));
	done(command->Draw(3));
	done(command->EndRendering());
	done(command->Barrier(hdr, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(depth, ResourceState::ShaderRead, ResourceState::TransferSource));
	done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	for (const auto& buffer : {hdrReadback, depthReadback, colorReadback})
	{
		done(command->Barrier(buffer, ResourceState::Undefined, ResourceState::TransferDestination));
	}
	done(command->CopyTextureToBuffer(hdr, hdrReadback));
	done(command->CopyTextureToBuffer(depth, depthReadback));
	done(command->CopyTextureToBuffer(color, colorReadback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Array<Ursa::UInt16, 16> hdrValues{};
	Ursa::Array<Ursa::Float32, 4> depthValues{};
	Ursa::Array<Ursa::UInt8, 16> pixels{};
	done(hdrReadback->Read(0, hdrValues.data(), sizeof(hdrValues)));
	done(depthReadback->Read(0, depthValues.data(), sizeof(depthValues)));
	done(colorReadback->Read(0, pixels.data(), sizeof(pixels)));
	for (Ursa::Size pixel = 0; pixel < 4; ++pixel)
	{
		Expect(hdrValues[pixel * 4] == 0x4000 && hdrValues[pixel * 4 + 1] == 0x3800 &&
		           hdrValues[pixel * 4 + 2] == 0x3400 && hdrValues[pixel * 4 + 3] == 0x3c00,
		       "HDR clear/readback truncated floating point color");
		Expect(depthValues[pixel] == 0.25f && pixels[pixel * 4] == 64 && pixels[pixel * 4 + 1] == 64 &&
		           pixels[pixel * 4 + 2] == 64,
		       "Depth sampling or readback mismatch");
	}
}

void TestMultipleAttachments(const DevicePtr& device)
{
	Expect(device->Capabilities().maxColorAttachments >= 2, "Multiple render target capability unavailable");
	auto first = Take(device->CreateTexture(
	    {2, 2, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto second = Take(device->CreateTexture(
	    {2, 2, Format::RgbA16Float, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto depth = Take(
	    device->CreateTexture({2, 2, Format::D32Float, TextureUsage::DepthAttachment | TextureUsage::TransferSource}));
	auto firstReadback = Take(device->CreateBuffer({16, BufferUsage::TransferDestination, Memory::Readback}));
	auto secondReadback = Take(device->CreateBuffer({32, BufferUsage::TransferDestination, Memory::Readback}));
	auto depthReadback = Take(device->CreateBuffer({16, BufferUsage::TransferDestination, Memory::Readback}));
	GraphicsPipelineDesc desc;
	desc.vertex = Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("Fullscreen.vert")}));
	desc.fragment =
	    Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("MultipleTargets.frag")}));
	desc.colorFormats = {Format::RgbA8Unorm, Format::RgbA16Float};
	auto pipeline = Take(device->CreateGraphicsPipeline(desc));
	GraphicsPipelineDesc depthDesc;
	depthDesc.vertex = desc.vertex;
	depthDesc.depthOnly = true;
	depthDesc.depthTest = true;
	auto depthPipeline = Take(device->CreateGraphicsPipeline(depthDesc));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(first, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(command->Barrier(second, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(command->Barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment));
	RenderTarget target;
	target.colors = {{first}, {second}};
	auto invalid = target;
	invalid.colors[1].texture = first;
	Expect(!command->BeginRendering(invalid), "Duplicate attachments accepted");
	invalid = target;
	invalid.color = first;
	Expect(!command->BeginRendering(invalid), "Conflicting legacy/explicit attachments accepted");
	invalid = target;
	invalid.colors[0].load = static_cast<AttachmentLoadOp>(99);
	Expect(!command->BeginRendering(invalid), "Unknown load operation accepted");
	invalid = target;
	invalid.colors[0].store = static_cast<AttachmentStoreOp>(99);
	Expect(!command->BeginRendering(invalid), "Unknown store operation accepted");
	done(command->BeginRendering(target));
	Expect(!command->BindPipeline(depthPipeline), "Depth-only pipeline accepted color attachments");
	done(command->BindPipeline(pipeline));
	done(command->Draw(3));
	done(command->EndRendering());
	for (auto& color : target.colors)
	{
		color.load = AttachmentLoadOp::Load;
	}
	done(command->BeginRendering(target));
	done(command->EndRendering());
	target.colors[0].load = AttachmentLoadOp::Clear;
	target.colors[0].clearValue = {0, 0, 1, 1};
	done(command->BeginRendering(target));
	done(command->EndRendering());
	RenderTarget depthTarget;
	depthTarget.depthAttachment.texture = depth;
	depthTarget.depthAttachment.clearDepth = 0.75f;
	done(command->BeginRendering(depthTarget));
	done(command->BindPipeline(depthPipeline));
	done(command->Draw(3));
	done(command->EndRendering());
	auto discarded = Take(device->CreateTexture({2, 2, Format::RgbA8Unorm, TextureUsage::ColorAttachment}));
	done(command->Barrier(discarded, ResourceState::Undefined, ResourceState::ColorAttachment));
	RenderTarget discardTarget;
	discardTarget.colors = {{discarded, AttachmentLoadOp::DontCare, AttachmentStoreOp::DontCare}};
	done(command->BeginRendering(discardTarget));
	done(command->EndRendering());
	done(command->Barrier(first, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(second, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(depth, ResourceState::DepthAttachment, ResourceState::TransferSource));
	for (const auto& buffer : {firstReadback, secondReadback, depthReadback})
	{
		done(command->Barrier(buffer, ResourceState::Undefined, ResourceState::TransferDestination));
	}
	done(command->CopyTextureToBuffer(first, firstReadback));
	done(command->CopyTextureToBuffer(second, secondReadback));
	done(command->CopyTextureToBuffer(depth, depthReadback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Array<Ursa::UInt8, 16> firstPixels{};
	Ursa::Array<Ursa::UInt16, 16> secondPixels{};
	Ursa::Array<Ursa::Float32, 4> depthPixels{};
	done(firstReadback->Read(0, firstPixels.data(), sizeof(firstPixels)));
	done(secondReadback->Read(0, secondPixels.data(), sizeof(secondPixels)));
	done(depthReadback->Read(0, depthPixels.data(), sizeof(depthPixels)));
	for (Ursa::Size pixel = 0; pixel < 4; ++pixel)
	{
		Expect(firstPixels[pixel * 4] == 0 && firstPixels[pixel * 4 + 2] == 255 && secondPixels[pixel * 4] == 0 &&
		           secondPixels[pixel * 4 + 1] == 0x4000 && secondPixels[pixel * 4 + 3] == 0x3c00 &&
		           depthPixels[pixel] == 0.5f,
		       "MRT draw, attachment Load/Store, or depth-only rendering mismatch");
	}
}

void TestFramebufferCache(const DevicePtr& device)
{
	const auto createTarget = [&](Ursa::UInt32 size, Format format)
	{
		return Take(
		    device->CreateTexture({size, size, format, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	};
	const auto render = [&](const TexturePtr& target, ResourceState before)
	{
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		if (before != ResourceState::ColorAttachment)
		{
			done(command->Barrier(target, before, ResourceState::ColorAttachment));
		}
		RenderTarget desc;
		desc.color = target;
		done(command->BeginRendering(desc));
		done(command->EndRendering());
		done(command->End());
		done(Take(device->Submit(command))->Wait());
	};

	auto original = createTarget(8, Format::RgbA8Unorm);
	render(original, ResourceState::Undefined);
	// The second command list must find the existing framebuffer and attachment backing.
	render(original, ResourceState::ColorAttachment);
	auto staleTransition = Take(device->CreateCommandList());
	done(staleTransition->Begin());
	done(staleTransition->Barrier(original, ResourceState::Undefined, ResourceState::TransferSource));
	done(staleTransition->End());
	auto rejectedSubmission = device->Submit(staleTransition);
	Expect(!rejectedSubmission && rejectedSubmission.error.find("queue resource state") != Ursa::String::npos,
	       "Submission accepted a texture transition with a stale queue state");
	done(staleTransition->Reset());
	done(staleTransition->Begin());
	done(staleTransition->Barrier(original, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(staleTransition->End());
	done(Take(device->Submit(staleTransition))->Wait());
	auto resized = createTarget(16, Format::RgbA8Unorm);
	render(resized, ResourceState::Undefined);
	auto reformatted = createTarget(16, Format::RgbA16Float);
	render(reformatted, ResourceState::Undefined);
	original.reset();
	resized.reset();
	reformatted.reset();
	done(device->WaitIdle());
}

void TestBufferRangesAndGroups(const DevicePtr& device)
{
	const auto capabilities = device->Capabilities();
	const auto alignment = capabilities.minUniformBufferOffsetAlignment;
	Expect(alignment > 0 && alignment <= 65536 && capabilities.maxBindGroups >= 2,
	       "Required uniform alignment or descriptor group capability unavailable");
	const Ursa::Size offset = Ursa::Size(alignment);
	auto frameBuffer = Take(device->CreateBuffer({16, BufferUsage::Uniform, Memory::Upload}));
	auto objectBuffer = Take(device->CreateBuffer({offset + 16, BufferUsage::Uniform, Memory::Upload}));
	const Ursa::Array<float, 4> white{1, 1, 1, 1}, red{1, 0, 0, 1}, green{0, 1, 0, 1};
	done(frameBuffer->Write(0, white.data(), sizeof(white)));
	done(objectBuffer->Write(0, red.data(), sizeof(red)));
	done(objectBuffer->Write(offset, green.data(), sizeof(green)));
	auto frameLayout = Take(device->CreateBindGroupLayout({{0, BindingType::UniformBuffer, 1, 2}}));
	auto objectLayout = Take(device->CreateBindGroupLayout({{0, BindingType::DynamicUniformBuffer, 1, 2}}));
	BindingResource frameResource;
	frameResource.buffer = frameBuffer;
	frameResource.size = 16;
	BindingResource objectResource;
	objectResource.buffer = objectBuffer;
	objectResource.size = 16;
	auto frameGroup = Take(device->CreateBindGroup({frameLayout, {frameResource}}));
	auto objectGroup = Take(device->CreateBindGroup({objectLayout, {objectResource}}));
	auto invalidResource = objectResource;
	invalidResource.size = objectBuffer->desc.size + 1;
	Expect(!device->CreateBindGroup({objectLayout, {invalidResource}}), "Descriptor range exceeded its buffer");
	if (alignment > 1)
	{
		invalidResource = objectResource;
		invalidResource.offset = 1;
		Expect(!device->CreateBindGroup({objectLayout, {invalidResource}}), "Unaligned descriptor offset accepted");
	}
	GraphicsPipelineDesc desc;
	desc.vertex = Take(device->CreateShader({ShaderStage::Vertex, ShaderFormat::Spirv, LoadShader("Fullscreen.vert")}));
	desc.fragment =
	    Take(device->CreateShader({ShaderStage::Fragment, ShaderFormat::Spirv, LoadShader("Parameters.frag")}));
	desc.layouts = {frameLayout, objectLayout};
	auto pipeline = Take(device->CreateGraphicsPipeline(desc));
	auto incompatible = desc;
	incompatible.layout = frameLayout;
	Expect(!device->CreateGraphicsPipeline(incompatible), "Conflicting single/multiple layout description accepted");
	auto color = Take(device->CreateTexture(
	    {4, 2, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto readback = Take(device->CreateBuffer({32, BufferUsage::TransferDestination, Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	auto transient = Take(command->CreateTransientBindGroup({objectLayout, {objectResource}}));
	done(command->Barrier(frameBuffer, ResourceState::Undefined, ResourceState::UniformRead));
	done(command->Barrier(objectBuffer, ResourceState::Undefined, ResourceState::UniformRead));
	done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	RenderTarget target;
	target.color = color;
	done(command->BeginRendering(target));
	done(command->BindPipeline(pipeline));
	Expect(!command->BindGroup(2, frameGroup), "Invalid descriptor group index accepted");
	auto foreignCommand = Take(device->CreateCommandList());
	done(foreignCommand->Begin());
	done(foreignCommand->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	done(foreignCommand->BeginRendering(target));
	done(foreignCommand->BindPipeline(pipeline));
	Expect(!foreignCommand->BindGroup(1, transient, {0}), "Transient descriptors escaped their owning command frame");
	done(foreignCommand->EndRendering());
	done(foreignCommand->Reset());
	Expect(!command->BindGroup(1, frameGroup), "Incompatible descriptor layout accepted");
	Expect(!command->BindGroup(1, objectGroup), "Missing dynamic offset accepted");
	if (alignment > 1)
	{
		Expect(!command->BindGroup(1, objectGroup, {1}), "Unaligned dynamic offset accepted");
	}
	Expect(!command->BindGroup(1, objectGroup, {Ursa::UInt32(offset * 2)}), "Out-of-range dynamic offset accepted");
	done(command->BindGroup(0, frameGroup));
	done(command->SetViewport({0, 0, 2, 2}));
	done(command->BindGroup(1, transient, {0}));
	done(command->Draw(3));
	done(command->SetViewport({2, 0, 2, 2}));
	done(command->BindGroup(1, transient, {Ursa::UInt32(offset)}));
	done(command->Draw(3));
	done(command->EndRendering());
	done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Array<Ursa::UInt8, 32> pixels{};
	done(readback->Read(0, pixels.data(), sizeof(pixels)));
	for (Ursa::Size pixel = 0; pixel < 8; ++pixel)
	{
		const bool left = pixel % 4 < 2;
		Expect(pixels[pixel * 4] == (left ? 255 : 0) && pixels[pixel * 4 + 1] == (left ? 0 : 255),
		       "Two objects did not use independent dynamic uniform ranges");
	}
	Expect(command->Statistics().descriptorPoolsCreated == 1 && command->Statistics().activeTransientGroups == 1,
	       "Frame descriptor creation statistics are incorrect");
	done(command->Reset());
	Expect(command->Statistics().activeTransientGroups == 0, "Completed descriptor pool was not recycled");
	done(command->Begin());
	done(command->Barrier(color, ResourceState::TransferSource, ResourceState::ColorAttachment));
	done(command->BeginRendering(target));
	done(command->BindPipeline(pipeline));
	Expect(!command->BindGroup(1, transient, {0}), "Old descriptor pool generation remained valid after reset");
	auto renewed = Take(command->CreateTransientBindGroup({objectLayout, {objectResource}}));
	done(command->BindGroup(0, frameGroup));
	done(command->BindGroup(1, renewed, {0}));
	done(command->Draw(3));
	done(command->EndRendering());
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	done(command->Reset());
	Expect(command->Statistics().descriptorPoolsCreated == 1 && command->Statistics().activeTransientGroups == 0 &&
	           command->Statistics().transientGroupsCreated == 2,
	       "Reusable command frame allocated a second descriptor pool or retained live sets");
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

void RenderSwapchainFrame(const DevicePtr& device, const SwapchainPtr& swapchain)
{
	auto acquired = Take(swapchain->Acquire());
	Expect(bool(acquired.texture), "Swapchain resize test did not acquire an image");
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(acquired.texture, acquired.initialState, ResourceState::ColorAttachment));
	RenderTarget target;
	target.color = acquired.texture;
	done(command->BeginRendering(target));
	done(command->EndRendering());
	done(command->Barrier(acquired.texture, ResourceState::ColorAttachment, ResourceState::Present));
	done(command->End());
	done(Take(device->Submit(command, swapchain))->Wait());
	done(swapchain->Present());
}

void TestSwapchainResize()
{
	auto window = Take(Ursa::Window::Create(640, 480, "Ursa swapchain resize test", false));
	DeviceDesc desc;
	desc.window = window;
#ifndef NDEBUG
	desc.validation = true;
#endif
	auto device = Take(CreateDevice(desc));
	auto swapchain = Take(device->CreateSwapchain({}));
	RenderSwapchainFrame(device, swapchain);
	for (const auto [width, height] : {std::pair<Ursa::UInt32, Ursa::UInt32>{800, 600}, {1024, 768}, {900, 700}})
	{
		window->Resize(width, height);
		window->PollEvents();
		RenderSwapchainFrame(device, swapchain);
	}
	done(device->WaitIdle());
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
		TestTextureAndOffscreen(device, false, true);
		TestTextureMipUploads(device);
		TestHdrAndDepth(device);
		TestMultipleAttachments(device);
		TestFramebufferCache(device);
		TestBufferRangesAndGroups(device);
		TestFirstInstance(device);
		TestScissorAndIndexedOffsets(device);
		TestStorageImage(device);
		done(device->WaitIdle());
		TestSwapchainResize();
		std::cout << "RHI GPU tests passed on " << Ursa::ToStdString(device->AdapterInfo().name) << '\n';
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "RHI GPU test failed: " << e.what() << '\n';
		return 1;
	}
}
