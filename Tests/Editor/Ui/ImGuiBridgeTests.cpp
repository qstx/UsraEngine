#include <Core/Containers.hpp>
#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Application/PlaySession.hpp>
#include <imgui_internal.h>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <iostream>
#include <stdexcept>

namespace Ursa::Editor
{
struct ImGuiBridgeTestAccess
{
	static void ReportFailure(ImGuiBridge& bridge, ImGuiViewport* viewport)
	{
		bridge.ReportPlatformFailure(viewport, "injected platform viewport failure");
	}
};
} // namespace Ursa::Editor

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;
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
	auto redUpload = Take(device->CreateBuffer({4, BufferUsage::TransferSource, Ursa::Rhi::Memory::Upload}));
	auto greenUpload = Take(device->CreateBuffer({4, BufferUsage::TransferSource, Ursa::Rhi::Memory::Upload}));
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
	    Take(device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Ursa::Rhi::Memory::Readback}));
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

void TestExplicitViewportDrawData(const DevicePtr& device)
{
	auto bridge = Take(Editor::ImGuiBridge::Create(device));
	ImGui::SetCurrentContext(bridge->Context());
	done(bridge->BeginFrame(1.f / 60.f, width, height));
	ImGui::GetBackgroundDrawList()->AddRectFilled({4, 4}, {24, 24}, IM_COL32(255, 0, 0, 255));
	ImGui::GetBackgroundDrawList()->AddText({25, 25}, IM_COL32(255, 255, 255, 255), "DPI");
	done(bridge->EndFrame());
	const ImDrawData* sourceData = ImGui::GetDrawData();
	Expect(sourceData && sourceData->CmdListsCount > 0, "ImGui did not produce source draw data");
	std::unique_ptr<ImDrawList> firstList(sourceData->CmdLists[0]->CloneOutput());
	std::unique_ptr<ImDrawList> secondList(sourceData->CmdLists[0]->CloneOutput());
	bool sharedFontAtlas = false;
	for (int index = 0; index < firstList->CmdBuffer.Size && index < secondList->CmdBuffer.Size; ++index)
	{
		sharedFontAtlas |= firstList->CmdBuffer[index].GetTexID() != ImTextureID_Invalid &&
		                   firstList->CmdBuffer[index].GetTexID() == secondList->CmdBuffer[index].GetTexID();
	}
	Expect(sharedFontAtlas && bridge->RegisteredTextureCount() > 0,
	       "Viewport draw data did not retain the shared font-atlas texture");
	const auto offsetList = [](ImDrawList& list, ImVec2 offset, ImU32 color)
	{
		for (ImDrawVert& vertex : list.VtxBuffer)
		{
			vertex.pos.x += offset.x;
			vertex.pos.y += offset.y;
			vertex.col = color;
		}
		for (ImDrawCmd& draw : list.CmdBuffer)
		{
			draw.ClipRect.x += offset.x;
			draw.ClipRect.y += offset.y;
			draw.ClipRect.z += offset.x;
			draw.ClipRect.w += offset.y;
		}
	};
	const ImVec2 firstDisplayPos{100, 50};
	const ImVec2 secondDisplayPos{-120, 80};
	offsetList(*firstList, firstDisplayPos, IM_COL32(255, 0, 0, 255));
	offsetList(*secondList, secondDisplayPos, IM_COL32(0, 255, 0, 255));
	const auto makeData = [&](ImDrawList* list, ImVec2 displayPos, ImVec2 framebufferScale)
	{
		ImDrawData data{};
		data.Valid = true;
		data.FrameCount = sourceData->FrameCount;
		data.DisplayPos = displayPos;
		data.DisplaySize = {32, 32};
		data.FramebufferScale = framebufferScale;
		data.CmdLists.push_back(list);
		data.CmdListsCount = 1;
		data.TotalVtxCount = list->VtxBuffer.Size;
		data.TotalIdxCount = list->IdxBuffer.Size;
		return data;
	};
	const ImDrawData firstData = makeData(firstList.get(), firstDisplayPos, {1, 1});
	const ImDrawData secondData = makeData(secondList.get(), secondDisplayPos, {2, 2});

	const auto render = [&](const ImDrawData& data)
	{
		auto target = Take(device->CreateTexture(
		    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		auto readback = Take(
		    device->CreateBuffer({width * height * 4, BufferUsage::TransferDestination, Ursa::Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(command->Barrier(target, ResourceState::Undefined, ResourceState::ColorAttachment));
		RenderTarget renderTarget;
		renderTarget.color = target;
		renderTarget.colorValue[3] = 1.0f;
		done(command->BeginRendering(renderTarget));
		done(bridge->Record(command, Format::RgbA8Unorm, width, height, &data));
		done(command->EndRendering());
		done(command->Barrier(target, ResourceState::ColorAttachment, ResourceState::TransferSource));
		done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		done(command->CopyTextureToBuffer(target, readback));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		Array<UInt8, width * height * 4> pixels{};
		done(readback->Read(0, pixels.data(), pixels.size()));
		return pixels;
	};
	const auto firstPixels = render(firstData);
	const auto secondPixels = render(secondData);
	const auto channel = [](const auto& pixels, UInt32 x, UInt32 y, UInt32 c)
	{ return pixels[(y * width + x) * 4 + c]; };
	Expect(channel(firstPixels, 16, 16, 0) > 240 && channel(firstPixels, 16, 16, 1) < 10,
	       "First viewport draw data ignored its nonzero DisplayPos");
	Expect(channel(secondPixels, 16, 16, 1) > 240 && channel(secondPixels, 16, 16, 0) < 10,
	       "Second viewport draw data reused another viewport's vertices or DisplayPos");
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

void TestPlatformViewportLifecycle()
{
	auto mainWindow = Take(Window::Create(640, 480, "ImGui viewport host", false));
	DeviceDesc deviceDesc;
	deviceDesc.window = mainWindow;
#ifndef NDEBUG
	deviceDesc.validation = true;
#endif
	auto device = Take(CreateDevice(deviceDesc));
	auto bridge = Take(Editor::ImGuiBridge::Create(device, mainWindow));
	ImGui::SetCurrentContext(bridge->Context());
	Expect(ImGui::GetIO().ConfigDpiScaleFonts && ImGui::GetIO().ConfigDpiScaleViewports,
	       "ImGui did not enable per-viewport font and style scaling");
	ImGuiViewport viewport;
	viewport.ID = ImGui::GetMainViewport()->ID + 1;
	viewport.Pos = ImVec2(24, 36);
	viewport.Size = ImVec2(320, 240);
	auto& platform = ImGui::GetPlatformIO();
	Expect(platform.Platform_CreateWindow && platform.Platform_DestroyWindow && platform.Renderer_CreateWindow &&
	           platform.Renderer_DestroyWindow,
	       "ImGui platform viewport handlers were not installed");
	platform.Platform_CreateWindow(&viewport);
	const bool platformCreated = viewport.PlatformHandle && viewport.PlatformUserData;
	const auto viewportInputWindow = bridge->InputWindowForViewport(&viewport);
	const bool inputWindowMapped = viewportInputWindow && viewportInputWindow != mainWindow &&
	                               bridge->InputWindowForViewport(ImGui::GetMainViewport()) == mainWindow;
	if (platformCreated)
	{
		mainWindow->Show();
		mainWindow->Focus();
		mainWindow->PollEvents();
		WindowEvent heldMouse;
		heldMouse.type = WindowEventType::MousePressed;
		heldMouse.button = MouseButton::Left;
		Detail::WindowAccess::Dispatch(*mainWindow, heldMouse);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		done(bridge->EndFrame());
		platform.Platform_ShowWindow(&viewport);
		mainWindow->PollEvents();
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		Expect(!viewportInputWindow->Focused() && ImGui::IsMouseDown(ImGuiMouseButton_Left),
		       "Showing a detached viewport stole focus or released the held docking mouse button");
		done(bridge->EndFrame());
		WindowEvent movedMouse;
		movedMouse.type = WindowEventType::CursorMoved;
		movedMouse.x = -28;
		movedMouse.y = 42;
		Detail::WindowAccess::Dispatch(*mainWindow, movedMouse);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const auto sourcePosition = mainWindow->Position();
		Expect(ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetIO().MousePos.x == sourcePosition.first - 28 &&
		           ImGui::GetIO().MousePos.y == sourcePosition.second + 42,
		       "Moving outside the source window lost the held mouse or its desktop coordinates");
		done(bridge->EndFrame());
		heldMouse.type = WindowEventType::MouseReleased;
		Detail::WindowAccess::Dispatch(*mainWindow, heldMouse);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		done(bridge->EndFrame());
	}
	bool savedViewportRecovered = false;
	if (platformCreated && platform.Platform_SetWindowPos && platform.Platform_GetWindowPos)
	{
		platform.Platform_SetWindowPos(&viewport, ImVec2(100000, 100000));
		const auto restored = platform.Platform_GetWindowPos(&viewport);
		viewport.Pos = restored;
		const auto workArea = Window::PrimaryMonitorWorkArea();
		savedViewportRecovered = workArea && restored.x < workArea.value.x + workArea.value.width &&
		                         restored.x + viewport.Size.x > workArea.value.x &&
		                         restored.y < workArea.value.y + workArea.value.height &&
		                         restored.y + 40.f > workArea.value.y;
	}
	if (platformCreated)
	{
		platform.Renderer_CreateWindow(&viewport);
	}
	const bool rendererCreated = viewport.RendererUserData != nullptr;
	bool viewportRenderError = false;
	const UInt64 listener =
	    Log::Subscribe([&](const LogRecord& record)
	                   { viewportRenderError |= record.message.find("platform viewport") != String::npos; });
	if (rendererCreated)
	{
		WindowEvent detachedFocus;
		detachedFocus.type = WindowEventType::Focused;
		detachedFocus.focused = true;
		Detail::WindowAccess::Dispatch(*viewportInputWindow, detachedFocus);
		WindowEvent detachedKey;
		detachedKey.type = WindowEventType::KeyPressed;
		detachedKey.key = Key::Space;
		Detail::WindowAccess::Dispatch(*viewportInputWindow, detachedKey);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool detachedKeyRouted = ImGui::IsKeyDown(ImGuiKey_Space);
		FrameInput detachedInput;
		detachedInput.keys[static_cast<Size>(Key::Space)] = detachedKeyRouted;
		const bool unfocusedGameInputBlocked = !SelectPlayInput(detachedInput, false, false).KeyDown(Key::Space);
		const bool editorTextInputBlocked = !SelectPlayInput(detachedInput, true, true).KeyDown(Key::Space);
		const bool gameViewportInputAllowed = SelectPlayInput(detachedInput, true, false).KeyDown(Key::Space);
		const auto monitors = Ursa::Window::Monitors();
		const auto& platformMonitors = ImGui::GetPlatformIO().Monitors;
		Expect(monitors && platformMonitors.Size == static_cast<int>(monitors.value.size()),
		       "ImGui platform monitor list did not preserve every connected display");
		for (Size index = 0; index < monitors.value.size(); ++index)
		{
			const auto& expected = monitors.value[index];
			const auto& actual = platformMonitors[static_cast<int>(index)];
			Expect(actual.MainPos.x == expected.bounds.x && actual.MainPos.y == expected.bounds.y &&
			           actual.WorkPos.x == expected.workArea.x && actual.WorkPos.y == expected.workArea.y &&
			           actual.DpiScale == expected.scaleX,
			       "ImGui monitor position, work area, or DPI diverged from the Window monitor service");
		}
		Expect(platform.Platform_GetWindowDpiScale && platform.Platform_GetWindowDpiScale(&viewport) > 0.0f,
		       "Floating viewport did not expose its owning window's content scale");
		char searchBuffer[64]{};
		ImGui::SetNextWindowViewport(viewport.ID);
		ImGui::SetNextWindowPos(ImVec2(viewport.Pos.x + 16.0f, viewport.Pos.y + 24.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(280.0f, 180.0f), ImGuiCond_Always);
		ImGui::Begin("Floating viewport render test", nullptr, ImGuiWindowFlags_MenuBar);
		ImGui::SetWindowFocus();
		ImGui::SetKeyboardFocusHere();
		ImGui::InputText("Asset search", searchBuffer, sizeof(searchBuffer));
		ImGui::Button("Editor commands");
		ImGui::TextUnformatted("Secondary viewport draw data");
		ImGui::End();
		done(bridge->EndFrame());
		viewport.DrawData = ImGui::GetDrawData();
		platform.Renderer_RenderWindow(&viewport, nullptr);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		ImGui::SetNextWindowViewport(viewport.ID);
		ImGui::SetNextWindowPos(ImVec2(viewport.Pos.x + 16.0f, viewport.Pos.y + 24.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(280.0f, 180.0f), ImGuiCond_Always);
		ImGui::Begin("Floating viewport render test", nullptr, ImGuiWindowFlags_MenuBar);
		ImGui::InputText("Asset search", searchBuffer, sizeof(searchBuffer));
		ImGui::Button("Editor commands");
		ImGui::End();
		done(bridge->EndFrame());
		viewport.DrawData = ImGui::GetDrawData();
		platform.Renderer_RenderWindow(&viewport, nullptr);
		WindowEvent searchCharacter;
		searchCharacter.type = WindowEventType::Character;
		searchCharacter.codepoint = 'q';
		Detail::WindowAccess::Dispatch(*viewportInputWindow, searchCharacter);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		ImGui::SetNextWindowViewport(viewport.ID);
		ImGui::SetNextWindowPos(ImVec2(viewport.Pos.x + 16.0f, viewport.Pos.y + 24.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(280.0f, 180.0f), ImGuiCond_Always);
		ImGui::Begin("Floating viewport render test", nullptr, ImGuiWindowFlags_MenuBar);
		ImGui::InputText("Asset search", searchBuffer, sizeof(searchBuffer));
		const bool searchFieldActive = ImGui::IsItemActive();
		const bool searchTypedInFloatingWindow = String(searchBuffer) == "q";
		ImGui::End();
		done(bridge->EndFrame());
		viewport.DrawData = ImGui::GetDrawData();
		platform.Renderer_RenderWindow(&viewport, nullptr);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool searchInputCaptured = ImGui::GetIO().WantCaptureKeyboard;
		const bool searchInputBlocked =
		    searchFieldActive && searchInputCaptured &&
		    !SelectPlayInput(detachedInput, true, EditorInputBlocksPlay(searchInputCaptured, false, false, false))
		         .KeyDown(Key::Space);
		done(bridge->EndFrame());
		ImVec2 commandButtonPosition{};
		const auto drawCommandButton = [&]()
		{
			ImGui::SetNextWindowViewport(viewport.ID);
			ImGui::SetNextWindowPos(ImVec2(viewport.Pos.x + 16.0f, viewport.Pos.y + 24.0f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(280.0f, 180.0f), ImGuiCond_Always);
			ImGui::Begin("Floating viewport render test", nullptr, ImGuiWindowFlags_MenuBar);
			ImGui::InputText("Asset search", searchBuffer, sizeof(searchBuffer));
			const bool clicked = ImGui::Button("Editor commands");
			if (ImGui::IsItemVisible())
			{
				const ImVec2 minimum = ImGui::GetItemRectMin();
				const ImVec2 maximum = ImGui::GetItemRectMax();
				commandButtonPosition = ImVec2((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
			}
			if (clicked)
			{
				ImGui::OpenPopup("Editor commands");
			}
			const bool popupOpen = ImGui::BeginPopup("Editor commands");
			if (popupOpen)
			{
				ImGui::MenuItem("Refresh assets");
				ImGui::EndPopup();
			}
			ImGui::End();
			return popupOpen;
		};
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		drawCommandButton();
		done(bridge->EndFrame());
		viewport.DrawData = ImGui::GetDrawData();
		platform.Renderer_RenderWindow(&viewport, nullptr);
		ImGui::GetIO().AddMousePosEvent(commandButtonPosition.x, commandButtonPosition.y);
		WindowEvent commandPressed;
		commandPressed.type = WindowEventType::MousePressed;
		commandPressed.button = MouseButton::Left;
		Detail::WindowAccess::Dispatch(*viewportInputWindow, commandPressed);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		drawCommandButton();
		done(bridge->EndFrame());
		WindowEvent commandReleased = commandPressed;
		commandReleased.type = WindowEventType::MouseReleased;
		Detail::WindowAccess::Dispatch(*viewportInputWindow, commandReleased);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool editorCommandPopupOpenedByClick = drawCommandButton();
		done(bridge->EndFrame());
		viewport.DrawData = ImGui::GetDrawData();
		platform.Renderer_RenderWindow(&viewport, nullptr);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool editorCommandPopupOpen = drawCommandButton();
		const ImGuiIO& editorInput = ImGui::GetIO();
		const bool commandMenuCaptured =
		    editorCommandPopupOpen && EditorInputBlocksPlay(editorInput.WantCaptureKeyboard, editorInput.WantTextInput,
		                                                    ImGui::GetDragDropPayload() != nullptr, true);
		const bool menuInputBlocked =
		    commandMenuCaptured && !SelectPlayInput(detachedInput, true, true).KeyDown(Key::Space);
		done(bridge->EndFrame());
		viewport.DrawData = ImGui::GetDrawData();
		platform.Renderer_RenderWindow(&viewport, nullptr);
		WindowEvent mainFocus;
		mainFocus.type = WindowEventType::Focused;
		mainFocus.focused = true;
		Detail::WindowAccess::Dispatch(*mainWindow, mainFocus);
		WindowEvent mainWindowKey;
		mainWindowKey.type = WindowEventType::KeyPressed;
		mainWindowKey.key = Key::Space;
		Detail::WindowAccess::Dispatch(*mainWindow, mainWindowKey);
		WindowEvent detachedMouse;
		detachedMouse.type = WindowEventType::MousePressed;
		detachedMouse.button = MouseButton::Left;
		Detail::WindowAccess::Dispatch(*viewportInputWindow, detachedMouse);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool detachedMouseWasDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		done(bridge->EndFrame());
		detachedFocus.focused = false;
		Detail::WindowAccess::Dispatch(*viewportInputWindow, detachedFocus);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool sharedKeyRetained = ImGui::IsKeyDown(ImGuiKey_Space);
		const bool detachedMouseReleased = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
		done(bridge->EndFrame());
		mainWindowKey.type = WindowEventType::KeyReleased;
		Detail::WindowAccess::Dispatch(*mainWindow, mainWindowKey);
		done(bridge->BeginFrame(1.0f / 60.0f, 640, 480));
		const bool detachedFocusReleased = !ImGui::IsKeyDown(ImGuiKey_Space);
		done(bridge->EndFrame());
		Expect(detachedKeyRouted, "A detached window's keyboard input was not routed through the shared ImGui context");
		Expect(unfocusedGameInputBlocked && editorTextInputBlocked && gameViewportInputAllowed,
		       "A key routed from a detached editor window was not gated by Game viewport focus and text input");
		Expect(editorCommandPopupOpen, "A floating editor command popup did not remain open for input gating");
		Expect(editorCommandPopupOpenedByClick,
		       "Clicking the floating viewport command button did not open its editor popup");
		Expect(searchFieldActive, "The floating Game search field did not retain keyboard focus");
		Expect(searchInputCaptured, "The floating Game search field did not capture keyboard input");
		Expect(searchTypedInFloatingWindow, "The floating Game search field did not receive a routed character event");
		Expect(searchInputBlocked, "The floating Game search field allowed a key through to Play mode");
		Expect(menuInputBlocked, "A floating editor command menu allowed keyboard input to reach Play mode");
		Expect(sharedKeyRetained, "Losing focus in one window released a key still held by another window");
		Expect(detachedMouseWasDown && detachedMouseReleased,
		       "A detached window mouse button did not release when its owning window lost focus");
		Expect(detachedFocusReleased, "Losing focus in a detached window left its ImGui key state stuck");
	}
	Log::Unsubscribe(listener);
	if (rendererCreated)
	{
		platform.Renderer_DestroyWindow(&viewport);
	}
	const bool rendererReleased = viewport.RendererUserData == nullptr;
	ImGuiViewport sourceViewport;
	sourceViewport.ID = ImGui::GetMainViewport()->ID + 2;
	sourceViewport.Pos = ImVec2(72, 84);
	sourceViewport.Size = ImVec2(240, 180);
	platform.Platform_CreateWindow(&sourceViewport);
	const bool sourcePlatformCreated = sourceViewport.PlatformHandle && sourceViewport.PlatformUserData;
	EditorSelectionEntry dragSource;
	dragSource.kind = EditorSelectionKind::Asset;
	dragSource.assetGuid = ResourceId::Generate();
	const auto sourceDrag = Take(BeginEditorDragSession({dragSource}, 1));
	const bool sourceDragWasActive = static_cast<bool>(ResolveEditorDragSession(sourceDrag, 1));
	if (sourcePlatformCreated)
	{
		platform.Platform_DestroyWindow(&sourceViewport);
	}
	const bool sourceDragWasCanceled = !ResolveEditorDragSession(sourceDrag, 1);
	const auto targetDrag = Take(BeginEditorDragSession({dragSource}, 2));
	const bool targetDragWasActive = static_cast<bool>(ResolveEditorDragSession(targetDrag, 2));
	if (platformCreated)
	{
		platform.Platform_DestroyWindow(&viewport);
	}
	const bool targetDragWasCanceled = !ResolveEditorDragSession(targetDrag, 2);
	const bool platformReleased = viewport.PlatformHandle == nullptr && viewport.PlatformUserData == nullptr;
	const bool sourcePlatformReleased =
	    sourceViewport.PlatformHandle == nullptr && sourceViewport.PlatformUserData == nullptr;
	viewport.PlatformHandle = nullptr;
	viewport.PlatformHandleRaw = nullptr;
	viewport.PlatformUserData = nullptr;
	viewport.RendererUserData = nullptr;
	sourceViewport.PlatformHandle = nullptr;
	sourceViewport.PlatformHandleRaw = nullptr;
	sourceViewport.PlatformUserData = nullptr;
	Expect(platformCreated, "Floating ImGui viewport did not create a native window");
	Expect(sourcePlatformCreated && sourcePlatformReleased,
	       "Closing the source ImGui viewport did not release its platform window state");
	Expect(inputWindowMapped, "A floating viewport did not resolve to its own input window");
	Expect(savedViewportRecovered, "An off-screen saved viewport was not moved back into a monitor work area");
	Expect(rendererCreated, "Floating ImGui viewport did not create a renderer swapchain");
	Expect(rendererReleased, "Floating ImGui viewport renderer state was not released");
	Expect(platformReleased, "Floating ImGui viewport platform state was not released");
	Expect(sourceDragWasActive && sourceDragWasCanceled && targetDragWasActive && targetDragWasCanceled,
	       "Closing a drag source or target viewport did not cancel its active editor drag session");
	Expect(!viewportRenderError, "Floating ImGui viewport failed to acquire, submit, or present its draw data");
	done(device->WaitIdle());
}

void TestPlatformViewportFailureFallback(const DevicePtr& device)
{
	auto bridge = Take(Editor::ImGuiBridge::Create(device));
	ImGui::SetCurrentContext(bridge->Context());
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	ImGuiViewport viewport;
	viewport.ID = ImGui::GetMainViewport()->ID + 1;
	Expect((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0,
	       "Test did not enable ImGui platform viewports before injecting a failure");
	Editor::ImGuiBridgeTestAccess::ReportFailure(*bridge, &viewport);
	Expect((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0 && viewport.PlatformRequestClose,
	       "A platform renderer failure did not disable floating viewports and request their cleanup");
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
		TestExplicitViewportDrawData(device);
		TestMissingEditorFontFallback(device);
		TestImagesFontsAndClipping(device);
		TestTextFieldPaste(device);
		TestPlatformViewportLifecycle();
		TestPlatformViewportFailureFallback(device);
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
