#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <Rhi/RHI.hpp>
#include <Rendering/Gui/ImGuiRenderer.hpp>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Ursa::Editor
{
// Owns an ImGui context. The caller must finish the GPU submission before starting another frame.
class ImGuiBridge final
{
public:
	struct InputEvent
	{
		std::shared_ptr<Window> window;
		WindowEvent event;
		Float32 screenX = 0;
		Float32 screenY = 0;
	};

	Vector<InputEvent> DrainInputEvents();

	// Raw events are still queued for the runtime. Returning true keeps a key
	// press/character out of Editor widgets; releases always clear Editor state.
	void SetRuntimeInputOwner(std::function<bool(Window*, const WindowEvent&)> owner)
	{
		runtimeInputOwner = std::move(owner);
	}

	static ImGuiBridge* Current();

	static Result<std::unique_ptr<ImGuiBridge>>
	Create(Rhi::DevicePtr selectedDevice, std::shared_ptr<Window> selectedWindow = {}, const String& layoutKey = {});

	~ImGuiBridge();

	ImGuiBridge(const ImGuiBridge&) = delete;

	ImGuiBridge& operator=(const ImGuiBridge&) = delete;

	Result<void> BeginFrame(Ursa::Float32 deltaSeconds, Ursa::UInt32 width, Ursa::UInt32 height,
	                        Ursa::UInt32 framebufferWidth = 0, Ursa::UInt32 framebufferHeight = 0);

	Result<void> EndFrame();

	Result<void> RenderPlatformWindows();

	// Return the native input source owning a viewport, or the host window for the main viewport.
	std::shared_ptr<Window> InputWindowForViewport(const ImGuiViewport* viewport) const;

	// Called within an existing color rendering pass. The pass target must match colorFormat.
	Result<void> Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat, Ursa::UInt32 framebufferWidth,
	                    Ursa::UInt32 framebufferHeight);

	// Records a specific viewport's draw data, which may have a distinct DisplayPos and framebuffer scale.
	Result<void> Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat, Ursa::UInt32 framebufferWidth,
	                    Ursa::UInt32 framebufferHeight, const ImDrawData* drawData);

	Result<ImTextureID> RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler);

	void UnregisterTexture(ImTextureID id);

	// Preserve current CPU draw data; release at the next frame after GPU completion.
	void RetireTexture(ImTextureID id);

	Size RegisteredTextureCount() const
	{
		return renderer ? renderer->RegisteredTextureCount() : 0;
	}

	ImGuiContext* Context() const
	{
		return context;
	}

	const Rhi::DevicePtr& Device() const
	{
		return device;
	}

	bool HasSavedLayout() const
	{
		return savedLayout;
	}

	Result<void> ClearSavedLayout();

	// Call after finishing GPU submissions, before the next frame.
	Result<void> ReloadShaders();

private:
	friend struct ImGuiBridgeTestAccess;
	explicit ImGuiBridge(Rhi::DevicePtr selectedDevice, std::shared_ptr<Window> selectedWindow,
	                     const String& layoutKey);

	Result<void> Initialize();

	Result<void> AttachInputWindow(const std::shared_ptr<Window>& inputWindow);

	void HandleWindowEvent(const std::shared_ptr<Window>& source, const WindowEvent& event);

	bool KeyDownInAnyWindow(Key key) const;
	bool MouseDownInAnyWindow(MouseButton button) const;
	void ReleaseWindowInput(Window* inputWindow);

	void DetachInputWindow(Window* inputWindow);

	void CreatePlatformWindow(ImGuiViewport* viewport);

	void DestroyPlatformWindow(ImGuiViewport* viewport);

	void ReportPlatformFailure(ImGuiViewport* viewport, const String& message);

	void ShowPlatformWindow(ImGuiViewport* viewport);

	void SetPlatformWindowPosition(ImGuiViewport* viewport, ImVec2 position);

	ImVec2 GetPlatformWindowPosition(ImGuiViewport* viewport);

	void SetPlatformWindowSize(ImGuiViewport* viewport, ImVec2 size);

	ImVec2 GetPlatformWindowSize(ImGuiViewport* viewport);

	ImVec2 GetPlatformWindowFramebufferScale(ImGuiViewport* viewport);

	void FocusPlatformWindow(ImGuiViewport* viewport);

	bool IsPlatformWindowFocused(ImGuiViewport* viewport);

	bool IsPlatformWindowMinimized(ImGuiViewport* viewport);

	void SetPlatformWindowTitle(ImGuiViewport* viewport, const char* title);

	float GetPlatformWindowDpiScale(ImGuiViewport* viewport);

	void CreateRendererWindow(ImGuiViewport* viewport);

	void DestroyRendererWindow(ImGuiViewport* viewport);

	void ResizeRendererWindow(ImGuiViewport* viewport, ImVec2 size);

	void RenderPlatformWindow(ImGuiViewport* viewport);

	Result<ImTextureID> UploadImage(const UInt8* pixels, UInt32 width, UInt32 height);

	Result<void> LoadEditorIcons(const VirtualFileSystem& files);

	static const char* GetClipboardText(ImGuiContext* guiContext);

	static void SetClipboardText(ImGuiContext* guiContext, const char* text);

	Rhi::DevicePtr device;
	std::shared_ptr<Window> window;
	std::unordered_map<Window*, std::pair<std::shared_ptr<Window>, Ursa::UInt64>> subscriptions;
	std::unordered_map<Window*, bool> windowFocus;
	std::unordered_map<Window*, std::unordered_set<Key>> pressedKeys;
	std::unordered_map<Window*, std::unordered_set<MouseButton>> pressedMouseButtons;
	ImGuiContext* context = nullptr;
	String clipboardText;
	String layoutSettingsName;
	VirtualFileSystem settingsFiles;
	bool savedLayout = false;
	bool leftControl = false;
	bool rightControl = false;
	bool leftShift = false;
	bool rightShift = false;
	bool leftAlt = false;
	bool rightAlt = false;
	bool leftSuper = false;
	bool rightSuper = false;
	std::unique_ptr<Rendering::ImGuiRenderer> renderer;
	ImTextureID editorIconsId = 0;
	Vector<UInt8> editorFontBytes;
	Vector<ImTextureID> retiredTextures;
	Vector<InputEvent> rawInputEvents;
	std::function<bool(Window*, const WindowEvent&)> runtimeInputOwner;
	std::unordered_map<Window*, std::pair<Float64, Float64>> rawPointerPositions;
};
} // namespace Ursa::Editor
