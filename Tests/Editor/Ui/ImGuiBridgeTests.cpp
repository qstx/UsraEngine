#include <Core/Containers.hpp>
#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
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

constexpr Ursa::UInt32 width = 64;
constexpr Ursa::UInt32 height = 64;
int clipboardReads = 0;

void TestImagesFontsAndClipping(const DevicePtr& device)
{
	auto bridge = Take(Editor::ImGuiBridge::Create(device));
	ImGui::SetCurrentContext(bridge->Context());
	const ImFont* defaultFont = ImGui::GetIO().FontDefault;
	Expect(defaultFont && String(defaultFont->GetDebugName()) == "Noto Sans SC",
	       "Bundled Chinese font is not the default Editor font");
	Expect(defaultFont->Sources.Size > 0 && defaultFont->Sources[0]->RasterizerMultiply >= 1.5f,
	       "Default Editor font contrast was not raised for small interface text");
	auto sampler = Take(device->CreateSampler({false, false}));
	auto red = Take(
	    device->CreateTexture({1, 1, Format::RgbA8Unorm, TextureUsage::Sampled | TextureUsage::TransferDestination}));
	auto green = Take(
	    device->CreateTexture({1, 1, Format::RgbA8Unorm, TextureUsage::Sampled | TextureUsage::TransferDestination}));
	constexpr Ursa::Array<Ursa::UInt8, 4> redPixel{255, 0, 0, 255};
	constexpr Ursa::Array<Ursa::UInt8, 4> greenPixel{0, 255, 0, 255};
	auto redUpload = Take(device->CreateBuffer({4, BufferUsage::TransferSource, Memory::Upload}));
	auto greenUpload = Take(device->CreateBuffer({4, BufferUsage::TransferSource, Memory::Upload}));
	done(redUpload->Write(0, redPixel.data(), redPixel.size()));
	done(greenUpload->Write(0, greenPixel.data(), greenPixel.size()));
	auto upload = Take(device->CreateCommandList());
	done(upload->Begin());
	done(upload->Barrier(red, ResourceState::Undefined, ResourceState::TransferDestination));
	done(upload->Barrier(green, ResourceState::Undefined, ResourceState::TransferDestination));
	done(upload->CopyBufferToTexture(redUpload, red));
	done(upload->CopyBufferToTexture(greenUpload, green));
	done(upload->Barrier(red, ResourceState::TransferDestination, ResourceState::ShaderRead));
	done(upload->Barrier(green, ResourceState::TransferDestination, ResourceState::ShaderRead));
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
	draw->AddText(ImVec2(2, 45), IM_COL32(255, 255, 255, 255), u8"中");
	done(bridge->EndFrame());
	const auto beforeRetirement = bridge->RegisteredTextureCount();
	bridge->RetireTexture(redId);
	bridge->RetireTexture(greenId);
	Expect(bridge->RegisteredTextureCount() == beforeRetirement,
	       "Retiring a preview image invalidated CPU draw data before recording");
	Expect(ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize())->FindGlyphNoFallback(0x4E2D) != nullptr,
	       "Dynamic Chinese glyph was not loaded into the Editor font atlas");

	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto readback =
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
	RenderTarget target;
	target.color = color;
	target.colorValue[0] = 0;
	target.colorValue[1] = 0;
	target.colorValue[2] = 0;
	target.colorValue[3] = 1;
	done(command->BeginRendering(target));
	done(bridge->Record(command, Format::RgbA8Unorm, width, height));
	done(command->EndRendering());
	done(command->Barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
	done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());

	Ursa::Array<Ursa::UInt8, width * height * 4> pixels{};
	done(readback->Read(0, pixels.data(), pixels.size()));
	const auto channel = [&](Ursa::UInt32 x, Ursa::UInt32 y, Ursa::UInt32 c) -> Ursa::UInt8
	{ return pixels[(y * width + x) * 4 + c]; };
	Expect(channel(8, 8, 0) > 240 && channel(8, 8, 1) < 10, "First ImGui image is not red");
	Expect(channel(28, 8, 1) > 240 && channel(28, 8, 0) < 10, "Second ImGui image is not green");
	Expect(channel(50, 8, 2) > 240, "Clipped ImGui panel did not render");
	Expect(channel(44, 8, 2) < 10 && channel(57, 8, 2) < 10, "ImGui scissor leaked outside clip");
	bool textVisible = false;
	for (Ursa::UInt32 y = 30; y < 50; ++y)
	{
		for (Ursa::UInt32 x = 2; x < 20; ++x)
		{
			textVisible |= channel(x, y, 0) > 30;
		}
	}
	Expect(textVisible, "ImGui font glyph did not render");
	Ursa::Size chinesePixels = 0;
	for (Ursa::UInt32 y = 44; y < height; ++y)
	{
		for (Ursa::UInt32 x = 2; x < 24; ++x)
		{
			chinesePixels += channel(x, y, 0) > 80 && channel(x, y, 1) > 80 && channel(x, y, 2) > 80;
		}
	}
	Expect(chinesePixels > 8, "Chinese fallback glyph did not render to the RHI target");
	done(bridge->BeginFrame(1.f / 60.f, width, height));
	Expect(bridge->RegisteredTextureCount() + 2 == beforeRetirement,
	       "Retired images were not released at the safe frame boundary");
	done(bridge->EndFrame());
}

void TestHighDpiDisplayScale(const DevicePtr& device)
{
	auto bridge = Take(Editor::ImGuiBridge::Create(device));
	done(bridge->BeginFrame(1.f / 60.f, 640, 480, 1280, 960));
	const ImGuiIO& io = ImGui::GetIO();
	Expect(io.DisplaySize.x == 640 && io.DisplaySize.y == 480, "ImGui display size must use logical window units");
	Expect(io.DisplayFramebufferScale.x == 2 && io.DisplayFramebufferScale.y == 2,
	       "ImGui framebuffer scale must preserve the high-DPI ratio");
	done(bridge->EndFrame());
}

void TestMissingEditorFontFallback(const DevicePtr& device)
{
	auto engineFiles = Take(OpenEngineFileSystem());
	auto entries = Take(engineFiles.List(engineScheme));
	auto memory = std::make_shared<MemoryFileProvider>();
	for (const VirtualFileEntry& entry : entries)
	{
		if (entry.path.relative == "Fonts/Editor/NotoSansSC-VF.ttf")
		{
			continue;
		}
		auto bytes = Take(engineFiles.Read(entry.path));
		done(memory->WriteAtomic(entry.path.relative, bytes, true));
	}
	VirtualFileSystem fontlessFiles;
	done(fontlessFiles.MountProvider(engineScheme, memory, MountAccess::ReadOnly));
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(std::move(fontlessFiles)));
	bool diagnosed = false;
	const UInt64 listener = Log::Subscribe(
	    [&](const LogRecord& record)
	    {
		    diagnosed |= record.category == "Editor" &&
		                 record.message.find("Unable to load the bundled Editor default font") != String::npos;
	    });
	auto bridge = Editor::ImGuiBridge::Create(device);
	Log::Unsubscribe(listener);
	ConfigureEngineFileSystem({});
	Expect(static_cast<bool>(bridge), "Editor startup failed when the bundled Chinese font was missing");
	Expect(diagnosed, "Missing Editor font did not produce a diagnostic");
	ImGui::SetCurrentContext(bridge.value->Context());
	Expect(ImGui::GetIO().FontDefault && String(ImGui::GetIO().FontDefault->GetDebugName()) != "Noto Sans SC",
	       "Missing Engine font did not retain the embedded ImGui default font");
	done(bridge.value->BeginFrame(1.f / 60.f, 64, 64));
	ImGui::GetBackgroundDrawList()->AddText(ImVec2(2, 2), IM_COL32(255, 255, 255, 255), "Editor fallback");
	done(bridge.value->EndFrame());
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
	control.type = WindowEventType::KeyPressed;
	control.key = Key::LeftControl;
	Detail::WindowAccess::Dispatch(*window, control);
	WindowEvent paste;
	paste.type = WindowEventType::KeyPressed;
	paste.key = Key::V;
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
		TestHighDpiDisplayScale(device);
		TestMissingEditorFontFallback(device);
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
