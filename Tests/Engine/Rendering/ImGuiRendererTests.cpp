#include <Rendering/Gui/ImGuiRenderer.hpp>
#include <Rendering/Resources/RenderFrameContext.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Core/Log.hpp>
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace
{
using namespace Ursa;
using namespace Ursa::Rendering;
using namespace Ursa::Rhi;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(result.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

using Context = std::unique_ptr<ImGuiContext, void (*)(ImGuiContext*)>;

Context MakeContext()
{
	Context context(ImGui::CreateContext(), ImGui::DestroyContext);
	ImGuiContextScope scope(context.get());
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = {64, 64};
	io.DeltaTime = 1.f / 60.f;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
	io.Fonts->AddFontDefault();
	return context;
}

const ImDrawData* MakeDrawData(ImGuiContext* context, ImTextureID image, bool first)
{
	ImGuiContextScope scope(context);
	ImGui::NewFrame();
	auto* draw = ImGui::GetBackgroundDrawList();
	if (first)
	{
		draw->PushClipRect({2, 2}, {16, 28});
		draw->AddImage(image, {2, 2}, {28, 28});
		draw->PopClipRect();
	}
	else
	{
		draw->AddImage(image, {32, 2}, {60, 28});
	}
	draw->AddText({2, 36}, IM_COL32_WHITE, first ? "First" : "Second");
	ImGui::Render();
	return ImGui::GetDrawData();
}
} // namespace

int main()
{
#ifdef _MSC_VER
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
	Ursa::LogSession logging;
	try
	{
		auto firstContext = MakeContext();
		auto secondContext = MakeContext();
		ImGui::SetCurrentContext(firstContext.get());
		try
		{
			ImGuiContextScope scope(secondContext.get());
			throw std::runtime_error("context scope test");
		}
		catch (const std::runtime_error&)
		{
		}
		Expect(ImGui::GetCurrentContext() == firstContext.get(), "Exception did not restore the preceding context");
		DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(CreateDevice(desc));
		auto files = Take(OpenEngineFileSystem());
		const auto shader = Take(VirtualPath::Parse("ursa:Shaders/Runtime/GUI.ursashaderasset"));
		auto firstRenderer = Take(ImGuiRenderer::Create(device, files, shader));
		auto secondRenderer = Take(ImGuiRenderer::Create(device, files, shader));
		const Array<UInt8, 4> red{255, 0, 0, 255};
		const Array<UInt8, 4> green{0, 255, 0, 255};
		const auto firstImage = Take(firstRenderer->UploadImage(red.data(), 1, 1));
		const auto secondImage = Take(secondRenderer->UploadImage(green.data(), 1, 1));
		Expect(firstImage == secondImage, "Independent renderer IDs should have independent namespaces");
		Expect(!firstRenderer->UploadImage(nullptr, 1, 1), "Null image data was accepted");
		const auto* firstData = MakeDrawData(firstContext.get(), firstImage, true);
		const auto* secondData = MakeDrawData(secondContext.get(), secondImage, false);
		Done(firstRenderer->UpdateAtlas(firstData));
		Done(secondRenderer->UpdateAtlas(secondData));
		Expect(ImGui::GetCurrentContext() == firstContext.get(), "Atlas upload changed the current context");
		auto frames = Take(RenderFrameQueue::Create(device));
		auto* frame = Take(frames->Begin());
		const auto command = frame->Command();
		auto color = Take(device->CreateTexture(
		    {64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		auto readback =
		    Take(device->CreateBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Ursa::Rhi::Memory::Readback}));
		Done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
		RenderTarget target;
		target.color = color;
		target.colorValue[3] = 1;
		Done(command->BeginRendering(target));
		Done(firstRenderer->Record(command, Format::RgbA8Unorm, 64, 64, firstData));
		std::weak_ptr<void> firstLifetime = firstRenderer->LastRecording();
		Done(frames->Retain(firstRenderer->LastRecording()));
		Done(secondRenderer->Record(command, Format::RgbA8Unorm, 64, 64, secondData));
		Done(frames->Retain(secondRenderer->LastRecording()));
		Expect(ImGui::GetCurrentContext() == firstContext.get(), "Recording changed the current context");
		Done(command->EndRendering());
		Done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
		Done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(color, readback));
		const auto firstSubmission = Take(frames->Complete());
		// Begin another frame and recreate the target while the previous submission
		// is retained by its actual fence, then release every renderer owner.
		auto* resizedFrame = Take(frames->Begin());
		auto resizedColor = Take(device->CreateTexture({128, 96, Format::RgbA8Unorm, TextureUsage::ColorAttachment}));
		const auto resizedCommand = resizedFrame->Command();
		Done(resizedCommand->Barrier(resizedColor, ResourceState::Undefined, ResourceState::ColorAttachment));
		RenderTarget resizedTarget;
		resizedTarget.color = resizedColor;
		Done(resizedCommand->BeginRendering(resizedTarget));
		Done(firstRenderer->Record(resizedCommand, Format::RgbA8Unorm, 128, 96, firstData));
		Done(frames->Retain(firstRenderer->LastRecording()));
		Done(frames->Retain(resizedColor));
		Done(resizedCommand->EndRendering());
		// Destroy the owners before even submitting: only the frame's recording
		// tokens retain the buffers, pipeline, sampler, and image registrations.
		firstRenderer->UnregisterTexture(firstImage);
		firstRenderer.reset();
		secondRenderer.reset();
		Expect(!firstLifetime.expired(), "GUI submission lost its resources when the renderer was destroyed");
		Done(Take(frames->Complete())->Wait());
		Done(firstSubmission->Wait());
		Array<UInt8, 64 * 64 * 4> pixels{};
		Done(readback->Read(0, pixels.data(), pixels.size()));
		const auto channel = [&](Size x, Size y, Size component) { return pixels[(y * 64 + x) * 4 + component]; };
		Expect(channel(8, 8, 0) > 240 && channel(8, 8, 1) < 10, "First context did not retain its red image");
		Expect(channel(40, 8, 1) > 240 && channel(40, 8, 0) < 10, "Second context used the first texture namespace");
		Expect(channel(20, 8, 0) < 10, "Explicit draw-data clipping escaped its rectangle");
		bool textVisible = false;
		for (Size y = 36; y < 54; ++y)
		{
			for (Size x = 2; x < 50; ++x)
			{
				textVisible |= channel(x, y, 0) > 30;
			}
		}
		Expect(textVisible, "Independent font atlas text was not rendered");
		Done(frames->Drain());
		Expect(firstLifetime.expired(), "Completed GUI recording was not released when the frame queue drained");
		Done(device->WaitIdle());
		std::cout << "Independent ImGui contexts, explicit draw data and GPU recording retention passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
