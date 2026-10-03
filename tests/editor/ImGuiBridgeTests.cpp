#include <ui/ImGuiBridge.hpp>
#include <core/StringInterop.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

constexpr uint32_t width = 64;
constexpr uint32_t height = 64;
int clipboardReads = 0;

void TestImagesFontsAndClipping(const DevicePtr& device)
{
	auto bridge = Take(Editor::ImGuiBridge::Create(device));
	auto sampler = Take(device->CreateSampler({false, false}));
	auto red = Take(
	    device->CreateTexture({1, 1, Format::rgbA8Unorm, TextureUsage::sampled | TextureUsage::transferDestination}));
	auto green = Take(
	    device->CreateTexture({1, 1, Format::rgbA8Unorm, TextureUsage::sampled | TextureUsage::transferDestination}));
	constexpr std::array<uint8_t, 4> redPixel{255, 0, 0, 255};
	constexpr std::array<uint8_t, 4> greenPixel{0, 255, 0, 255};
	auto redUpload = Take(device->CreateBuffer({4, BufferUsage::transferSource, Memory::upload}));
	auto greenUpload = Take(device->CreateBuffer({4, BufferUsage::transferSource, Memory::upload}));
	done(redUpload->Write(0, redPixel.data(), redPixel.size()));
	done(greenUpload->Write(0, greenPixel.data(), greenPixel.size()));
	auto upload = Take(device->CreateCommandList());
	done(upload->Begin());
	done(upload->Barrier(red, ResourceState::undefined, ResourceState::transferDestination));
	done(upload->Barrier(green, ResourceState::undefined, ResourceState::transferDestination));
	done(upload->CopyBufferToTexture(redUpload, red));
	done(upload->CopyBufferToTexture(greenUpload, green));
	done(upload->Barrier(red, ResourceState::transferDestination, ResourceState::shaderRead));
	done(upload->Barrier(green, ResourceState::transferDestination, ResourceState::shaderRead));
	done(upload->End());
	done(Take(device->Submit(upload))->Wait());
	const auto redId = Take(bridge->RegisterTexture(red, sampler));
	const auto greenId = Take(bridge->RegisterTexture(green, sampler));

	done(bridge->BeginFrame(1.f / 60.f, width, height));
	auto* draw = ImGui::GetBackgroundDrawList();
	draw->AddImage(redId, ImVec2(2, 2), ImVec2(18, 18));
	draw->AddImage(greenId, ImVec2(22, 2), ImVec2(38, 18));
	draw->PushClipRect(ImVec2(46, 2), ImVec2(54, 18));
	draw->AddRectFilled(ImVec2(42, 2), ImVec2(60, 18), IM_COL32(0, 0, 255, 255));
	draw->PopClipRect();
	draw->AddText(ImVec2(2, 30), IM_COL32(255, 255, 255, 255), "A");
	done(bridge->EndFrame());

	auto color = Take(device->CreateTexture(
	    {width, height, Format::rgbA8Unorm, TextureUsage::colorAttachment | TextureUsage::transferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::transferDestination, Memory::readback}));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::undefined, ResourceState::colorAttachment));
	RenderTarget target;
	target.color = color;
	target.colorValue[0] = 0;
	target.colorValue[1] = 0;
	target.colorValue[2] = 0;
	target.colorValue[3] = 1;
	done(command->BeginRendering(target));
	done(bridge->Record(command, Format::rgbA8Unorm, width, height));
	done(command->EndRendering());
	done(command->Barrier(color, ResourceState::colorAttachment, ResourceState::transferSource));
	done(command->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());

	std::array<uint8_t, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	const auto channel = [&](uint32_t x, uint32_t y, uint32_t c) -> uint8_t { return pixels[(y * width + x) * 4 + c]; };
	Expect(channel(8, 8, 0) > 240 && channel(8, 8, 1) < 10, "First ImGui image is not red");
	Expect(channel(28, 8, 1) > 240 && channel(28, 8, 0) < 10, "Second ImGui image is not green");
	Expect(channel(50, 8, 2) > 240, "Clipped ImGui panel did not render");
	Expect(channel(44, 8, 2) < 10 && channel(57, 8, 2) < 10, "ImGui scissor leaked outside clip");
	bool textVisible = false;
	for (uint32_t y = 30; y < 50; ++y)
	{
		for (uint32_t x = 2; x < 20; ++x)
		{
			textVisible |= channel(x, y, 0) > 30;
		}
	}
	Expect(textVisible, "ImGui font glyph did not render");
	bridge->UnregisterTexture(redId);
	bridge->UnregisterTexture(greenId);
}

void TestTextFieldPaste(const DevicePtr& device)
{
	auto window = Take(Window::Create(640, 480, "ImGui clipboard test", false));
	auto bridge = Take(Editor::ImGuiBridge::Create(device, window));
	ImGui::SetCurrentContext(bridge->Context());
	auto& platform = ImGui::GetPlatformIO();
	Expect(platform.Platform_ClipboardUserData == bridge.get() && platform.Platform_GetClipboardTextFn &&
	           platform.Platform_SetClipboardTextFn,
	       "ImGui platform clipboard was not connected to the window");
	platform.Platform_GetClipboardTextFn = [](ImGuiContext*)
	{
		++clipboardReads;
		return "G:\\Assets\\Demo";
	};

	char path[1024]{};
	done(bridge->BeginFrame(1.f / 60.f, 640, 480));
	ImGui::Begin("Paste test");
	ImGui::SetKeyboardFocusHere();
	ImGui::InputText("Asset root", path, sizeof(path));
	ImGui::End();
	done(bridge->EndFrame());

	done(bridge->BeginFrame(1.f / 60.f, 640, 480));
	ImGui::Begin("Paste test");
	ImGui::InputText("Asset root", path, sizeof(path));
	Expect(ImGui::IsItemActive(), "Asset root did not receive keyboard focus");
	ImGui::End();
	done(bridge->EndFrame());

	done(bridge->BeginFrame(1.f / 60.f, 640, 480));
	ImGui::Begin("Paste test");
	ImGui::InputText("Asset root", path, sizeof(path));
	ImGui::End();
	done(bridge->EndFrame());

	WindowEvent control;
	control.type = WindowEventType::keyPressed;
	control.key = Key::leftControl;
	Detail::WindowAccess::Dispatch(*window, control);
	WindowEvent paste;
	paste.type = WindowEventType::keyPressed;
	paste.key = Key::v;
	Detail::WindowAccess::Dispatch(*window, paste);
	done(bridge->BeginFrame(1.f / 60.f, 640, 480));
	ImGui::Begin("Paste test");
	ImGui::InputText("Asset root", path, sizeof(path));
	const bool active = ImGui::IsItemActive();
	const bool controlDown = ImGui::GetIO().KeyCtrl;
	const bool vPressed = ImGui::IsKeyPressed(ImGuiKey_V);
	ImGui::End();
	done(bridge->EndFrame());
	Expect(active && controlDown && vPressed, "Ctrl+V did not reach the focused text field");
	Expect(clipboardReads == 1 && String(path) == "G:\\Assets\\Demo", "Ctrl+V did not paste into Asset root");
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
		TestImagesFontsAndClipping(device);
		TestTextFieldPaste(device);
		done(device->WaitIdle());
		std::cout << "ImGui RHI bridge GPU test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
