#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Memory/MemoryContainers.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Inspector/ReferencePicker.hpp>
#include <Ui/EditorIcons.hpp>
#include <stb_image.h>
#include <ImGuizmo.h>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Core/Log.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#if defined(_WIN32)
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include "../../ThirdParty/glfw/include/GLFW/glfw3.h"
#include "../../ThirdParty/glfw/include/GLFW/glfw3native.h"
#endif

namespace Ursa::Editor
{
namespace
{
String LayoutSettingsName(const String& layoutKey)
{
	if (layoutKey.empty())
	{
		return "EditorLayout.ini";
	}
	Ursa::UInt64 hash = 14695981039346656037ull;
	for (unsigned char character : layoutKey)
	{
		hash = (hash ^ character) * 1099511628211ull;
	}
	std::ostringstream filename;
	filename << "EditorLayout/" << std::hex << std::setfill('0') << std::setw(16) << hash << ".ini";
	return ToUrsaString(filename.str());
}

ImVec2 KeepPlatformWindowVisible(ImVec2 position, ImVec2 size)
{
	const auto monitors = Window::Monitors();
	if (!monitors || monitors.value.empty())
	{
		return position;
	}
	const float width = std::max(size.x, 1.0f);
	const float height = std::max(size.y, 1.0f);
	const float visibleWidth = std::min({120.0f, width, 240.0f});
	const float visibleHeight = std::min({40.0f, height, 80.0f});
	const auto visibleOn = [&](const WindowWorkArea& area)
	{
		const float workRight = static_cast<float>(area.x) + static_cast<float>(area.width);
		const float workBottom = static_cast<float>(area.y) + static_cast<float>(area.height);
		const float overlapWidth =
		    std::min(position.x + width, workRight) - std::max(position.x, static_cast<float>(area.x));
		const float overlapHeight =
		    std::min(position.y + height, workBottom) - std::max(position.y, static_cast<float>(area.y));
		return overlapWidth >= visibleWidth && overlapHeight >= visibleHeight;
	};
	if (std::any_of(monitors.value.begin(), monitors.value.end(),
	                [&](const WindowMonitorInfo& monitor) { return visibleOn(monitor.workArea); }))
	{
		return position;
	}
	const auto distanceSquared = [&](const WindowWorkArea& area)
	{
		const float right = static_cast<float>(area.x) + static_cast<float>(area.width);
		const float bottom = static_cast<float>(area.y) + static_cast<float>(area.height);
		const float gapX = position.x + width < area.x ? static_cast<float>(area.x) - position.x - width
		                   : position.x > right        ? position.x - right
		                                               : 0.0f;
		const float gapY = position.y + height < area.y ? static_cast<float>(area.y) - position.y - height
		                   : position.y > bottom        ? position.y - bottom
		                                                : 0.0f;
		return gapX * gapX + gapY * gapY;
	};
	const auto nearest = std::min_element(monitors.value.begin(), monitors.value.end(),
	                                      [&](const WindowMonitorInfo& left, const WindowMonitorInfo& right)
	                                      { return distanceSquared(left.workArea) < distanceSquared(right.workArea); });
	const auto& area = nearest->workArea;
	const float minimumX = static_cast<float>(area.x) - width + visibleWidth;
	const float maximumX = static_cast<float>(area.x) + static_cast<float>(area.width) - visibleWidth;
	const float minimumY = static_cast<float>(area.y);
	const float maximumY = static_cast<float>(area.y) + static_cast<float>(area.height) - visibleHeight;
	return ImVec2(std::clamp(position.x, minimumX, maximumX), std::clamp(position.y, minimumY, maximumY));
}

struct GuiVertex
{
	Ursa::Float32 position[2];
	Ursa::Float32 uv[2];
	Ursa::Float32 color[4];
};

struct PlatformWindowState
{
	std::shared_ptr<Window> window;
	Rhi::SwapchainPtr swapchain;
};

void* NativePlatformHandle(void* window)
{
#if defined(_WIN32)
	return window ? glfwGetWin32Window(static_cast<GLFWwindow*>(window)) : nullptr;
#else
	(void)window;
	return nullptr;
#endif
}

Result<void> ApplyImGuiRenderState(const Shader::RenderStateDeclaration& state, Rhi::BlendState& blendState,
                                   Rhi::CullMode& cullMode, bool& blendEnabled, bool& depthTestEnabled,
                                   bool& depthWriteEnabled)
{
	if (state.name == "Blend")
	{
		if (state.value == "Off")
		{
			blendEnabled = false;
			blendState.enabled = false;
			return {};
		}
		if (state.value != "SrcAlpha OneMinusSrcAlpha")
		{
			return Result<void>::Failure("Unsupported ImGui Shader Blend state: " + state.value);
		}
		blendEnabled = true;
		blendState.enabled = true;
		blendState.sourceColor = Rhi::BlendFactor::SourceAlpha;
		blendState.destinationColor = Rhi::BlendFactor::OneMinusSourceAlpha;
		blendState.sourceAlpha = Rhi::BlendFactor::SourceAlpha;
		blendState.destinationAlpha = Rhi::BlendFactor::OneMinusSourceAlpha;
		return {};
	}
	if (state.name == "Cull")
	{
		if (state.value == "Off")
		{
			cullMode = Rhi::CullMode::None;
			return {};
		}
		return Result<void>::Failure("Unsupported ImGui Shader Cull state: " + state.value);
	}
	if (state.name == "ZTest")
	{
		if (state.value == "Off" || state.value == "Disabled")
		{
			depthTestEnabled = false;
			return {};
		}
		return Result<void>::Failure("Unsupported ImGui Shader ZTest state: " + state.value);
	}
	if (state.name == "ZWrite")
	{
		if (state.value == "Off")
		{
			depthWriteEnabled = false;
			return {};
		}
		return Result<void>::Failure("Unsupported ImGui Shader ZWrite state: " + state.value);
	}
	return Result<void>::Failure("Unsupported ImGui Shader render state: " + state.name);
}

ImGuiKey ToImGuiKey(Key key)
{
	switch (key)
	{
		case Key::Tab:
			return ImGuiKey_Tab;
		case Key::Left:
			return ImGuiKey_LeftArrow;
		case Key::Right:
			return ImGuiKey_RightArrow;
		case Key::Up:
			return ImGuiKey_UpArrow;
		case Key::Down:
			return ImGuiKey_DownArrow;
		case Key::PageUp:
			return ImGuiKey_PageUp;
		case Key::PageDown:
			return ImGuiKey_PageDown;
		case Key::Home:
			return ImGuiKey_Home;
		case Key::End:
			return ImGuiKey_End;
		case Key::Insert:
			return ImGuiKey_Insert;
		case Key::Delete:
			return ImGuiKey_Delete;
		case Key::Backspace:
			return ImGuiKey_Backspace;
		case Key::Space:
			return ImGuiKey_Space;
		case Key::Enter:
			return ImGuiKey_Enter;
		case Key::Escape:
			return ImGuiKey_Escape;
		case Key::LeftShift:
			return ImGuiKey_LeftShift;
		case Key::RightShift:
			return ImGuiKey_RightShift;
		case Key::LeftControl:
			return ImGuiKey_LeftCtrl;
		case Key::RightControl:
			return ImGuiKey_RightCtrl;
		case Key::LeftAlt:
			return ImGuiKey_LeftAlt;
		case Key::RightAlt:
			return ImGuiKey_RightAlt;
		case Key::LeftSuper:
			return ImGuiKey_LeftSuper;
		case Key::RightSuper:
			return ImGuiKey_RightSuper;
		case Key::A:
			return ImGuiKey_A;
		case Key::B:
			return ImGuiKey_B;
		case Key::C:
			return ImGuiKey_C;
		case Key::D:
			return ImGuiKey_D;
		case Key::E:
			return ImGuiKey_E;
		case Key::F:
			return ImGuiKey_F;
		case Key::G:
			return ImGuiKey_G;
		case Key::H:
			return ImGuiKey_H;
		case Key::I:
			return ImGuiKey_I;
		case Key::J:
			return ImGuiKey_J;
		case Key::K:
			return ImGuiKey_K;
		case Key::L:
			return ImGuiKey_L;
		case Key::M:
			return ImGuiKey_M;
		case Key::N:
			return ImGuiKey_N;
		case Key::O:
			return ImGuiKey_O;
		case Key::P:
			return ImGuiKey_P;
		case Key::Q:
			return ImGuiKey_Q;
		case Key::R:
			return ImGuiKey_R;
		case Key::S:
			return ImGuiKey_S;
		case Key::T:
			return ImGuiKey_T;
		case Key::U:
			return ImGuiKey_U;
		case Key::V:
			return ImGuiKey_V;
		case Key::W:
			return ImGuiKey_W;
		case Key::X:
			return ImGuiKey_X;
		case Key::Y:
			return ImGuiKey_Y;
		case Key::Z:
			return ImGuiKey_Z;
		default:
			break;
	}
	if (key >= Key::Digit0 && key <= Key::Digit9)
	{
		return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(key) - static_cast<int>(Key::Digit0));
	}
	return ImGuiKey_None;
}

} // namespace

ImGuiBridge::ImGuiBridge(Rhi::DevicePtr selectedDevice, std::shared_ptr<Window> selectedWindow, const String& layoutKey)
    : device(std::move(selectedDevice)), window(std::move(selectedWindow)),
      layoutSettingsName(LayoutSettingsName(layoutKey))
{
}

Result<std::unique_ptr<ImGuiBridge>>
ImGuiBridge::Create(Rhi::DevicePtr selectedDevice, std::shared_ptr<Window> selectedWindow, const String& layoutKey)
{
	if (!selectedDevice)
	{
		return Result<std::unique_ptr<ImGuiBridge>>::Failure("ImGui RHI bridge requires a device");
	}
	auto bridge =
	    std::unique_ptr<ImGuiBridge>(new ImGuiBridge(std::move(selectedDevice), std::move(selectedWindow), layoutKey));
	auto initialized = bridge->Initialize();
	if (!initialized)
	{
		return Result<std::unique_ptr<ImGuiBridge>>::Failure(initialized.error);
	}
	return Result<std::unique_ptr<ImGuiBridge>>::Success(std::move(bridge));
}

Result<void> ImGuiBridge::Initialize()
{
	IMGUI_CHECKVERSION();
	context = ImGui::CreateContext();
	ImGui::SetCurrentContext(context);
	auto& io = ImGui::GetIO();
	io.BackendRendererUserData = this;
	io.BackendRendererName = "Ursa RHI";
	io.BackendPlatformName = "Ursa Window";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigDpiScaleFonts = true;
	io.ConfigDpiScaleViewports = true;
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;
	if (window)
	{
		auto root = ResolveUserDataDirectory(PathToUrsaString(CurrentExecutablePath().stem()));
		if (!root)
		{
			return Result<void>::Failure(root.error);
		}
		auto provider = DirectoryFileProvider::Open(root.value, true);
		if (!provider)
		{
			return Result<void>::Failure(provider.error);
		}
		auto mounted = settingsFiles.MountProvider(userDataScheme, provider.value, MountAccess::ReadWrite);
		if (!mounted)
		{
			return mounted;
		}
		auto path = VirtualPath::Parse(String("user:Settings/") + layoutSettingsName);
		if (!path)
		{
			return Result<void>::Failure(path.error);
		}
		auto exists = settingsFiles.Exists(path.value);
		if (!exists)
		{
			return Result<void>::Failure(exists.error);
		}
		if (exists.value)
		{
			auto bytes = settingsFiles.Read(path.value);
			if (!bytes)
			{
				return Result<void>::Failure(bytes.error);
			}
			ImGui::LoadIniSettingsFromMemory(reinterpret_cast<const char*>(bytes.value.data()), bytes.value.size());
			savedLayout = true;
		}
	}
	if (window)
	{
		auto& platform = ImGui::GetPlatformIO();
		platform.Platform_ClipboardUserData = this;
		platform.Platform_GetClipboardTextFn = &ImGuiBridge::GetClipboardText;
		platform.Platform_SetClipboardTextFn = &ImGuiBridge::SetClipboardText;
		platform.Platform_CreateWindow = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->CreatePlatformWindow(viewport);
			}
		};
		platform.Platform_DestroyWindow = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->DestroyPlatformWindow(viewport);
			}
		};
		platform.Platform_ShowWindow = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->ShowPlatformWindow(viewport);
			}
		};
		platform.Platform_SetWindowPos = [](ImGuiViewport* viewport, ImVec2 position)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->SetPlatformWindowPosition(viewport, position);
			}
		};
		platform.Platform_GetWindowPos = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			return bridge ? bridge->GetPlatformWindowPosition(viewport) : ImVec2(0, 0);
		};
		platform.Platform_SetWindowSize = [](ImGuiViewport* viewport, ImVec2 size)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->SetPlatformWindowSize(viewport, size);
			}
		};
		platform.Platform_GetWindowSize = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			return bridge ? bridge->GetPlatformWindowSize(viewport) : ImVec2(0, 0);
		};
		platform.Platform_GetWindowFramebufferScale = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			return bridge ? bridge->GetPlatformWindowFramebufferScale(viewport) : ImVec2(1, 1);
		};
		platform.Platform_SetWindowFocus = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->FocusPlatformWindow(viewport);
			}
		};
		platform.Platform_GetWindowFocus = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			return bridge && bridge->IsPlatformWindowFocused(viewport);
		};
		platform.Platform_GetWindowMinimized = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			return bridge && bridge->IsPlatformWindowMinimized(viewport);
		};
		platform.Platform_SetWindowTitle = [](ImGuiViewport* viewport, const char* title)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->SetPlatformWindowTitle(viewport, title);
			}
		};
		platform.Platform_GetWindowDpiScale = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			return bridge ? bridge->GetPlatformWindowDpiScale(viewport) : 1.0f;
		};
		platform.Renderer_CreateWindow = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->CreateRendererWindow(viewport);
			}
		};
		platform.Renderer_DestroyWindow = [](ImGuiViewport* viewport)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->DestroyRendererWindow(viewport);
			}
		};
		platform.Renderer_SetWindowSize = [](ImGuiViewport* viewport, ImVec2 size)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->ResizeRendererWindow(viewport, size);
			}
		};
		platform.Renderer_RenderWindow = [](ImGuiViewport* viewport, void*)
		{
			auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
			if (bridge)
			{
				bridge->RenderPlatformWindow(viewport);
			}
		};
		platform.Renderer_SwapBuffers = [](ImGuiViewport*, void*) {};
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
		auto* mainViewport = ImGui::GetMainViewport();
		mainViewport->PlatformHandle = Detail::WindowAccess::Native(*window);
		mainViewport->PlatformHandleRaw = NativePlatformHandle(mainViewport->PlatformHandle);
	}
	auto engineFiles = OpenEngineFileSystem();
	if (!engineFiles)
	{
		return Result<void>::Failure(engineFiles.error);
	}
	const auto editorFontPath = VirtualPath::Parse("ursa:Fonts/Editor/NotoSansSC-VF.ttf");
	if (!editorFontPath)
	{
		return Result<void>::Failure(editorFontPath.error);
	}
	auto fontBytes = engineFiles.value.Read(editorFontPath.value);
	if (!fontBytes || fontBytes.value.empty() ||
	    fontBytes.value.size() > static_cast<Ursa::Size>((std::numeric_limits<int>::max)()))
	{
		URSA_LOG_WARN("Editor", "Unable to load the bundled Editor default font: " +
		                            (fontBytes ? String("font file is empty or too large") : fontBytes.error));
	}
	else
	{
		editorFontBytes = std::move(fontBytes.value);
		ImFontConfig editorFontConfig{};
		std::memcpy(editorFontConfig.Name, "Noto Sans SC", sizeof("Noto Sans SC"));
		editorFontConfig.FontDataOwnedByAtlas = false;
		editorFontConfig.RasterizerMultiply = 1.5f;
		io.FontDefault = io.Fonts->AddFontFromMemoryTTF(
		    editorFontBytes.data(), static_cast<int>(editorFontBytes.size()), 0.0f, &editorFontConfig);
		if (io.FontDefault)
		{
			ImFontConfig embeddedFallbackConfig{};
			embeddedFallbackConfig.MergeMode = true;
			io.Fonts->AddFontDefault(&embeddedFallbackConfig);
		}
		else
		{
			editorFontBytes.clear();
			URSA_LOG_WARN("Editor", "Unable to initialize the bundled Editor font; using ImGui default font");
		}
	}
	if (!io.FontDefault)
	{
		io.FontDefault = io.Fonts->AddFontDefault();
	}
	if (!io.FontDefault)
	{
		return Result<void>::Failure("Cannot initialize an Editor font");
	}
	auto loadedShaders = LoadShaders(engineFiles.value);
	if (!loadedShaders)
	{
		return loadedShaders;
	}
	auto sampler = device->CreateSampler({true, false});
	if (!sampler)
	{
		return Result<void>::Failure(sampler.error);
	}
	atlasSampler = std::move(sampler.value);
	auto loadedIcons = LoadEditorIcons(engineFiles.value);
	if (!loadedIcons)
	{
		URSA_LOG_WARN("Editor", "Editor icons unavailable; using readable labels: " + loadedIcons.error);
	}
	if (window)
	{
		auto attached = AttachInputWindow(window);
		if (!attached)
		{
			return attached;
		}
	}
	return {};
}

ImGuiBridge::~ImGuiBridge()
{
	if (context)
	{
		ImGui::SetCurrentContext(context);
		ImGui::DestroyPlatformWindows();
		for (const auto& entry : subscriptions)
		{
			entry.second.first->Unsubscribe(entry.second.second);
		}
		subscriptions.clear();
		ImGui::GetPlatformIO().ClearRendererHandlers();
		ImGui::GetPlatformIO().ClearPlatformHandlers();
		ReleaseReferencePickerContext(context, this);
		Icons::ClearAtlas(context);
		if (editorIconsId)
		{
			device->WaitIdle();
			UnregisterTexture(editorIconsId);
		}
		ImGui::GetIO().BackendRendererUserData = nullptr;
		ImGui::DestroyContext(context);
	}
}

ImGuiBridge* ImGuiBridge::Current()
{
	if (!ImGui::GetCurrentContext())
	{
		return nullptr;
	}
	return static_cast<ImGuiBridge*>(ImGui::GetIO().BackendRendererUserData);
}

const char* ImGuiBridge::GetClipboardText(ImGuiContext* guiContext)
{
	ImGui::SetCurrentContext(guiContext);
	auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
	if (!bridge || !bridge->window)
	{
		return nullptr;
	}
	bridge->clipboardText = bridge->window->ClipboardText();
	return bridge->clipboardText.c_str();
}

void ImGuiBridge::SetClipboardText(ImGuiContext* guiContext, const char* text)
{
	ImGui::SetCurrentContext(guiContext);
	auto* bridge = static_cast<ImGuiBridge*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
	if (bridge && bridge->window)
	{
		bridge->window->SetClipboardText(text ? String(text) : String{});
	}
}

Result<void> ImGuiBridge::BeginFrame(Ursa::Float32 deltaSeconds, Ursa::UInt32 width, Ursa::UInt32 height,
                                     Ursa::UInt32 framebufferWidth, Ursa::UInt32 framebufferHeight)
{
	if (!retiredTextures.empty())
	{
		auto idle = device->WaitIdle();
		if (!idle)
		{
			return idle;
		}
		for (const auto id : retiredTextures)
		{
			UnregisterTexture(id);
		}
		retiredTextures.clear();
	}
	if (!width || !height)
	{
		return Result<void>::Failure("ImGui frame size must be positive");
	}
	if (!framebufferWidth)
	{
		framebufferWidth = width;
	}
	if (!framebufferHeight)
	{
		framebufferHeight = height;
	}
	ImGui::SetCurrentContext(context);
	auto& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<Ursa::Float32>(width), static_cast<Ursa::Float32>(height));
	io.DisplayFramebufferScale = ImVec2(static_cast<Ursa::Float32>(framebufferWidth) / width,
	                                    static_cast<Ursa::Float32>(framebufferHeight) / height);
	io.DeltaTime = std::max(deltaSeconds, 0.00001f);
	if (window)
	{
		auto* mainViewport = ImGui::GetMainViewport();
		const auto position = window->Position();
		mainViewport->Pos = ImVec2(static_cast<float>(position.first), static_cast<float>(position.second));
		mainViewport->Size = io.DisplaySize;
		mainViewport->FramebufferScale = io.DisplayFramebufferScale;
		mainViewport->DpiScale = window->ContentScale().first;
		mainViewport->WorkPos = mainViewport->Pos;
		mainViewport->WorkSize = mainViewport->Size;
		auto& monitors = ImGui::GetPlatformIO().Monitors;
		monitors.clear();
		auto availableMonitors = Window::Monitors();
		if (availableMonitors)
		{
			for (const auto& info : availableMonitors.value)
			{
				ImGuiPlatformMonitor monitor;
				monitor.MainPos = ImVec2(static_cast<float>(info.bounds.x), static_cast<float>(info.bounds.y));
				monitor.MainSize =
				    ImVec2(static_cast<float>(info.bounds.width), static_cast<float>(info.bounds.height));
				monitor.WorkPos = ImVec2(static_cast<float>(info.workArea.x), static_cast<float>(info.workArea.y));
				monitor.WorkSize =
				    ImVec2(static_cast<float>(info.workArea.width), static_cast<float>(info.workArea.height));
				monitor.DpiScale = info.scaleX;
				monitors.push_back(monitor);
			}
		}
		if (monitors.empty())
		{
			auto workArea = Window::PrimaryMonitorWorkArea();
			if (workArea)
			{
				ImGuiPlatformMonitor monitor;
				monitor.MainPos = ImVec2(static_cast<float>(workArea.value.x), static_cast<float>(workArea.value.y));
				monitor.MainSize =
				    ImVec2(static_cast<float>(workArea.value.width), static_cast<float>(workArea.value.height));
				monitor.WorkPos = monitor.MainPos;
				monitor.WorkSize = monitor.MainSize;
				monitor.DpiScale = mainViewport->DpiScale;
				monitors.push_back(monitor);
			}
		}
	}
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
	return {};
}

Result<void> ImGuiBridge::EndFrame()
{
	ImGui::SetCurrentContext(context);
	ImGui::Render();
	PruneEditorDragSessions();
	// Multi-viewport backends must update native windows before the next NewFrame().
	ImGui::UpdatePlatformWindows();
	if (window && ImGui::GetIO().WantSaveIniSettings)
	{
		size_t length = 0;
		const auto* text = reinterpret_cast<const UInt8*>(ImGui::SaveIniSettingsToMemory(&length));
		auto path = VirtualPath::Parse(String("user:Settings/") + layoutSettingsName);
		if (!path)
		{
			return Result<void>::Failure(path.error);
		}
		auto saved = settingsFiles.WriteAtomic(path.value, Vector<UInt8>(text, text + length));
		if (!saved)
		{
			return saved;
		}
		ImGui::GetIO().WantSaveIniSettings = false;
	}
	return UpdateAtlas();
}

Result<void> ImGuiBridge::AttachInputWindow(const std::shared_ptr<Window>& inputWindow)
{
	if (!inputWindow || subscriptions.find(inputWindow.get()) != subscriptions.end())
	{
		return {};
	}
	auto subscribed = inputWindow->Subscribe([this, inputWindow](const WindowEvent& event)
	                                         { HandleWindowEvent(inputWindow, event); });
	if (!subscribed)
	{
		return Result<void>::Failure(subscribed.error);
	}
	subscriptions.emplace(inputWindow.get(), std::make_pair(inputWindow, subscribed.value));
	windowFocus[inputWindow.get()] = inputWindow->Focused();
	return {};
}

bool ImGuiBridge::KeyDownInAnyWindow(Key key) const
{
	return std::any_of(pressedKeys.begin(), pressedKeys.end(),
	                   [key](const auto& entry) { return entry.second.find(key) != entry.second.end(); });
}

bool ImGuiBridge::MouseDownInAnyWindow(MouseButton button) const
{
	return std::any_of(pressedMouseButtons.begin(), pressedMouseButtons.end(),
	                   [button](const auto& entry) { return entry.second.find(button) != entry.second.end(); });
}

void ImGuiBridge::ReleaseWindowInput(Window* inputWindow)
{
	if (!context || !inputWindow)
	{
		return;
	}
	ImGui::SetCurrentContext(context);
	auto& input = ImGui::GetIO();
	if (const auto found = pressedKeys.find(inputWindow); found != pressedKeys.end())
	{
		const auto releasedKeys = found->second;
		pressedKeys.erase(found);
		for (const auto key : releasedKeys)
		{
			if (!KeyDownInAnyWindow(key))
			{
				if (const auto imguiKey = ToImGuiKey(key); imguiKey != ImGuiKey_None)
				{
					input.AddKeyEvent(imguiKey, false);
				}
			}
		}
	}
	if (const auto found = pressedMouseButtons.find(inputWindow); found != pressedMouseButtons.end())
	{
		const auto releasedButtons = found->second;
		pressedMouseButtons.erase(found);
		for (const auto button : releasedButtons)
		{
			if (!MouseDownInAnyWindow(button))
			{
				input.AddMouseButtonEvent(static_cast<int>(button), false);
			}
		}
	}
	const auto isDown = [this](Key key) { return KeyDownInAnyWindow(key); };
	leftControl = isDown(Key::LeftControl);
	rightControl = isDown(Key::RightControl);
	leftShift = isDown(Key::LeftShift);
	rightShift = isDown(Key::RightShift);
	leftAlt = isDown(Key::LeftAlt);
	rightAlt = isDown(Key::RightAlt);
	leftSuper = isDown(Key::LeftSuper);
	rightSuper = isDown(Key::RightSuper);
	input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
	input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
	input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
	input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
}

void ImGuiBridge::HandleWindowEvent(const std::shared_ptr<Window>& source, const WindowEvent& event)
{
	if (!context)
	{
		return;
	}
	ImGui::SetCurrentContext(context);
	auto& input = ImGui::GetIO();
	switch (event.type)
	{
		case WindowEventType::KeyPressed:
		case WindowEventType::KeyReleased:
		{
			const bool pressed = event.type == WindowEventType::KeyPressed;
			const bool wasDown = KeyDownInAnyWindow(event.key);
			auto& sourceKeys = pressedKeys[source.get()];
			if (pressed)
			{
				sourceKeys.insert(event.key);
			}
			else
			{
				sourceKeys.erase(event.key);
				if (sourceKeys.empty())
				{
					pressedKeys.erase(source.get());
				}
			}
			const bool isDown = KeyDownInAnyWindow(event.key);
			if (wasDown != isDown)
			{
				if (const auto key = ToImGuiKey(event.key); key != ImGuiKey_None)
				{
					input.AddKeyEvent(key, isDown);
				}
			}
			switch (event.key)
			{
				case Key::LeftControl:
					leftControl = KeyDownInAnyWindow(Key::LeftControl);
					input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
					break;
				case Key::RightControl:
					rightControl = KeyDownInAnyWindow(Key::RightControl);
					input.AddKeyEvent(ImGuiMod_Ctrl, leftControl || rightControl);
					break;
				case Key::LeftShift:
					leftShift = KeyDownInAnyWindow(Key::LeftShift);
					input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
					break;
				case Key::RightShift:
					rightShift = KeyDownInAnyWindow(Key::RightShift);
					input.AddKeyEvent(ImGuiMod_Shift, leftShift || rightShift);
					break;
				case Key::LeftAlt:
					leftAlt = KeyDownInAnyWindow(Key::LeftAlt);
					input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
					break;
				case Key::RightAlt:
					rightAlt = KeyDownInAnyWindow(Key::RightAlt);
					input.AddKeyEvent(ImGuiMod_Alt, leftAlt || rightAlt);
					break;
				case Key::LeftSuper:
					leftSuper = KeyDownInAnyWindow(Key::LeftSuper);
					input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
					break;
				case Key::RightSuper:
					rightSuper = KeyDownInAnyWindow(Key::RightSuper);
					input.AddKeyEvent(ImGuiMod_Super, leftSuper || rightSuper);
					break;
				default:
					break;
			}
			break;
		}
		case WindowEventType::KeyRepeated:
			break;
		case WindowEventType::Character:
			input.AddInputCharacter(event.codepoint);
			break;
		case WindowEventType::CursorMoved:
		{
			const auto position = source->DesktopCursorPosition();
			input.AddMousePosEvent(static_cast<float>(position.first), static_cast<float>(position.second));
			break;
		}
		case WindowEventType::MousePressed:
		case WindowEventType::MouseReleased:
			if (event.button != MouseButton::Other)
			{
				const bool pressed = event.type == WindowEventType::MousePressed;
				const bool wasDown = MouseDownInAnyWindow(event.button);
				auto& sourceButtons = pressedMouseButtons[source.get()];
				if (pressed)
				{
					sourceButtons.insert(event.button);
				}
				else
				{
					sourceButtons.erase(event.button);
					if (sourceButtons.empty())
					{
						pressedMouseButtons.erase(source.get());
					}
				}
				const bool isDown = MouseDownInAnyWindow(event.button);
				if (wasDown != isDown)
				{
					input.AddMouseButtonEvent(static_cast<int>(event.button), isDown);
				}
			}
			break;
		case WindowEventType::Scrolled:
			input.AddMouseWheelEvent(static_cast<float>(event.x), static_cast<float>(event.y));
			break;
		case WindowEventType::Focused:
		{
			windowFocus[source.get()] = event.focused;
			if (!event.focused)
			{
				ReleaseWindowInput(source.get());
			}
			const bool anyFocused =
			    std::any_of(windowFocus.begin(), windowFocus.end(), [](const auto& entry) { return entry.second; });
			input.AddFocusEvent(anyFocused);
			break;
		}
		case WindowEventType::PositionChanged:
			if (auto* viewport = ImGui::FindViewportByPlatformHandle(Detail::WindowAccess::Native(*source)))
			{
				viewport->PlatformRequestMove = true;
			}
			break;
		case WindowEventType::SizeChanged:
		case WindowEventType::FramebufferSizeChanged:
			if (auto* viewport = ImGui::FindViewportByPlatformHandle(Detail::WindowAccess::Native(*source)))
			{
				viewport->PlatformRequestResize = true;
			}
			break;
		default:
			break;
	}
}

void ImGuiBridge::DetachInputWindow(Window* inputWindow)
{
	const auto found = subscriptions.find(inputWindow);
	if (found != subscriptions.end())
	{
		ReleaseWindowInput(inputWindow);
		windowFocus.erase(inputWindow);
		found->second.first->Unsubscribe(found->second.second);
		subscriptions.erase(found);
	}
}

void ImGuiBridge::CreatePlatformWindow(ImGuiViewport* viewport)
{
	if (!viewport || viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return;
	}
	WindowCreateDesc desc;
	desc.width = static_cast<UInt32>(std::max(viewport->Size.x, 1.0f));
	desc.height = static_cast<UInt32>(std::max(viewport->Size.y, 1.0f));
	desc.title = "Ursa Editor";
	desc.visible = false;
	desc.decorated = (viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0;
	desc.floating = (viewport->Flags & ImGuiViewportFlags_TopMost) != 0;
	auto created = Window::Create(desc);
	if (!created)
	{
		ReportPlatformFailure(viewport, created.error);
		return;
	}
	auto state = std::make_unique<PlatformWindowState>();
	state->window = std::move(created.value);
	state->window->SetPosition(static_cast<Int32>(viewport->Pos.x), static_cast<Int32>(viewport->Pos.y));
	auto attached = AttachInputWindow(state->window);
	if (!attached)
	{
		ReportPlatformFailure(viewport, attached.error);
		return;
	}
	viewport->PlatformHandle = Detail::WindowAccess::Native(*state->window);
	viewport->PlatformHandleRaw = NativePlatformHandle(viewport->PlatformHandle);
	viewport->PlatformUserData = state.release();
}

void ImGuiBridge::DestroyPlatformWindow(ImGuiViewport* viewport)
{
	if (!viewport || viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return;
	}
	ClearEditorDragSessions();
	auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData);
	if (state)
	{
		if (state->swapchain)
		{
			auto idle = device->WaitIdle();
			if (!idle)
			{
				ReportPlatformFailure(viewport, idle.error);
			}
			state->swapchain.reset();
		}
		DetachInputWindow(state->window.get());
		delete state;
	}
	viewport->PlatformUserData = nullptr;
	viewport->RendererUserData = nullptr;
	viewport->PlatformHandle = nullptr;
	viewport->PlatformHandleRaw = nullptr;
}

void ImGuiBridge::ReportPlatformFailure(ImGuiViewport* viewport, const String& message)
{
	URSA_LOG_ERROR("Editor", "Unable to create or present an ImGui platform viewport: " + message);
	ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
	if (viewport)
	{
		viewport->PlatformRequestClose = true;
	}
}

void ImGuiBridge::ShowPlatformWindow(ImGuiViewport* viewport)
{
	if (viewport && viewport->ID != ImGui::GetMainViewport()->ID)
	{
		if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData); state && state->window)
		{
			state->window->Show();
		}
	}
}

void ImGuiBridge::SetPlatformWindowPosition(ImGuiViewport* viewport, ImVec2 position)
{
	if (!viewport)
	{
		return;
	}
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		if (window)
		{
			window->SetPosition(static_cast<Int32>(position.x), static_cast<Int32>(position.y));
		}
		return;
	}
	if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData); state && state->window)
	{
		position = KeepPlatformWindowVisible(position, viewport->Size);
		state->window->SetPosition(static_cast<Int32>(position.x), static_cast<Int32>(position.y));
	}
}

ImVec2 ImGuiBridge::GetPlatformWindowPosition(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return ImVec2(0, 0);
	}
	if (viewport->ID == ImGui::GetMainViewport()->ID && window)
	{
		const auto position = window->Position();
		return ImVec2(static_cast<float>(position.first), static_cast<float>(position.second));
	}
	if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData); state && state->window)
	{
		const auto position = state->window->Position();
		return ImVec2(static_cast<float>(position.first), static_cast<float>(position.second));
	}
	return ImVec2(0, 0);
}

void ImGuiBridge::SetPlatformWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
	if (!viewport)
	{
		return;
	}
	auto target = viewport->ID == ImGui::GetMainViewport()->ID
	                  ? window
	                  : (static_cast<PlatformWindowState*>(viewport->PlatformUserData)
	                         ? static_cast<PlatformWindowState*>(viewport->PlatformUserData)->window
	                         : nullptr);
	if (target)
	{
		target->Resize(static_cast<UInt32>(std::max(size.x, 1.0f)), static_cast<UInt32>(std::max(size.y, 1.0f)));
	}
}

ImVec2 ImGuiBridge::GetPlatformWindowSize(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return ImVec2(0, 0);
	}
	Window* target = nullptr;
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		target = window.get();
	}
	else if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData))
	{
		target = state->window.get();
	}
	return target ? ImVec2(static_cast<float>(target->Width()), static_cast<float>(target->Height())) : ImVec2(0, 0);
}

ImVec2 ImGuiBridge::GetPlatformWindowFramebufferScale(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return ImVec2(1, 1);
	}
	Window* target = nullptr;
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		target = window.get();
	}
	else if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData))
	{
		target = state->window.get();
	}
	if (!target || !target->Width() || !target->Height())
	{
		return ImVec2(1, 1);
	}
	return ImVec2(static_cast<float>(target->FramebufferWidth()) / target->Width(),
	              static_cast<float>(target->FramebufferHeight()) / target->Height());
}

void ImGuiBridge::FocusPlatformWindow(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return;
	}
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		if (window)
		{
			window->Focus();
		}
		return;
	}
	if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData); state && state->window)
	{
		state->window->Focus();
	}
}

bool ImGuiBridge::IsPlatformWindowFocused(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return false;
	}
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return window && window->Focused();
	}
	if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData))
	{
		return state->window && state->window->Focused();
	}
	return false;
}

bool ImGuiBridge::IsPlatformWindowMinimized(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return true;
	}
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return window && window->Minimized();
	}
	if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData))
	{
		return !state->window || state->window->Minimized();
	}
	return true;
}

void ImGuiBridge::SetPlatformWindowTitle(ImGuiViewport* viewport, const char* title)
{
	if (!viewport)
	{
		return;
	}
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		if (window)
		{
			window->SetTitle(title ? String(title) : String{});
		}
		return;
	}
	if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData); state && state->window)
	{
		state->window->SetTitle(title ? String(title) : String{});
	}
}

float ImGuiBridge::GetPlatformWindowDpiScale(ImGuiViewport* viewport)
{
	if (!viewport)
	{
		return 1.0f;
	}
	Window* target = nullptr;
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		target = window.get();
	}
	else if (auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData))
	{
		target = state->window.get();
	}
	if (!target)
	{
		return 1.0f;
	}
	return target->ContentScale().first;
}

void ImGuiBridge::CreateRendererWindow(ImGuiViewport* viewport)
{
	if (!viewport || viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return;
	}
	auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData);
	if (!state || !state->window)
	{
		return;
	}
	Rhi::SwapchainDesc desc;
	desc.window = state->window;
	auto created = device->CreateSwapchain(desc);
	if (!created)
	{
		ReportPlatformFailure(viewport, created.error);
		return;
	}
	state->swapchain = std::move(created.value);
	viewport->RendererUserData = state;
}

void ImGuiBridge::DestroyRendererWindow(ImGuiViewport* viewport)
{
	if (!viewport || viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return;
	}
	auto* state = static_cast<PlatformWindowState*>(viewport->RendererUserData);
	if (state && state->swapchain)
	{
		auto idle = device->WaitIdle();
		if (!idle)
		{
			ReportPlatformFailure(viewport, idle.error);
		}
		state->swapchain.reset();
	}
	viewport->RendererUserData = nullptr;
}

void ImGuiBridge::ResizeRendererWindow(ImGuiViewport*, ImVec2)
{
}

void ImGuiBridge::RenderPlatformWindow(ImGuiViewport* viewport)
{
	if (!viewport || viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return;
	}
	auto* state = static_cast<PlatformWindowState*>(viewport->RendererUserData);
	if (!state || !state->swapchain)
	{
		return;
	}
	auto acquired = state->swapchain->Acquire();
	if (!acquired)
	{
		ReportPlatformFailure(viewport, acquired.error);
		return;
	}
	if (!acquired.value.texture)
	{
		return;
	}
	auto command = device->CreateCommandList();
	if (!command)
	{
		ReportPlatformFailure(viewport, command.error);
		return;
	}
	auto result = command.value->Begin();
	if (result)
	{
		result = command.value->Barrier(acquired.value.texture, acquired.value.initialState,
		                                Rhi::ResourceState::ColorAttachment);
	}
	Rhi::RenderTarget target;
	target.color = acquired.value.texture;
	if (result)
	{
		result = command.value->BeginRendering(target);
	}
	if (result)
	{
		result = Record(command.value, state->swapchain->Format(), acquired.value.texture->desc.width,
		                acquired.value.texture->desc.height, viewport->DrawData);
	}
	if (result)
	{
		result = command.value->EndRendering();
	}
	if (result)
	{
		result = command.value->Barrier(acquired.value.texture, Rhi::ResourceState::ColorAttachment,
		                                Rhi::ResourceState::Present);
	}
	if (result)
	{
		result = command.value->End();
	}
	Rhi::FencePtr fence;
	if (result)
	{
		auto submitted = device->Submit(command.value, state->swapchain);
		if (!submitted)
		{
			result = Result<void>::Failure(submitted.error);
		}
		else
		{
			fence = std::move(submitted.value);
		}
	}
	if (result)
	{
		result = state->swapchain->Present();
	}
	if (result)
	{
		result = fence->Wait();
	}
	if (!result)
	{
		ReportPlatformFailure(viewport, result.error);
	}
}

Result<void> ImGuiBridge::RenderPlatformWindows()
{
	if (!context || !window)
	{
		return {};
	}
	ImGui::SetCurrentContext(context);
	for (int index = 1; index < ImGui::GetPlatformIO().Viewports.Size; ++index)
	{
		auto* viewport = ImGui::GetPlatformIO().Viewports[index];
		auto* state = static_cast<PlatformWindowState*>(viewport->PlatformUserData);
		if (state && state->window && state->window->ShouldClose())
		{
			viewport->PlatformRequestClose = true;
			state->window->CancelClose();
		}
	}
	ImGui::RenderPlatformWindowsDefault();
	return {};
}

std::shared_ptr<Window> ImGuiBridge::InputWindowForViewport(const ImGuiViewport* viewport) const
{
	if (!viewport || !context)
	{
		return window;
	}
	ImGui::SetCurrentContext(context);
	if (viewport->ID == ImGui::GetMainViewport()->ID)
	{
		return window;
	}
	const auto* state = static_cast<const PlatformWindowState*>(viewport->PlatformUserData);
	return state ? state->window : std::shared_ptr<Window>{};
}

Result<void> ImGuiBridge::ClearSavedLayout()
{
	if (!settingsFiles.IsMounted(userDataScheme))
	{
		savedLayout = false;
		return {};
	}
	auto path = VirtualPath::Parse(String("user:Settings/") + layoutSettingsName);
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	auto exists = settingsFiles.Exists(path.value);
	if (!exists)
	{
		return Result<void>::Failure(exists.error);
	}
	if (exists.value)
	{
		auto removed = settingsFiles.Remove(path.value);
		if (!removed)
		{
			return removed;
		}
	}
	savedLayout = false;
	return {};
}

Result<void> ImGuiBridge::LoadShaders(const VirtualFileSystem& files)
{
	auto shaderPath = VirtualPath::Parse(String(engineScheme) + ":Shaders/Editor/ImGui.ursashaderasset");
	if (!shaderPath)
	{
		return Result<void>::Failure(shaderPath.error);
	}
	auto shader = Shader::LoadShaderAssetResource(files, shaderPath.value);
	if (!shader)
	{
		return Result<void>::Failure(shader.error);
	}
	if (shader.value.subShaders.empty() || shader.value.subShaders.front().passes.empty() ||
	    shader.value.subShaders.front().passes.front().variants.empty())
	{
		return Result<void>::Failure("Imported ImGui Shader has no compiled graphics Pass");
	}
	const auto& subShader = shader.value.subShaders.front();
	const auto& pass = subShader.passes.front();
	const auto& variant = pass.variants.front();
	for (const Shader::RenderStateDeclaration& state : subShader.renderStates)
	{
		auto applied =
		    ApplyImGuiRenderState(state, blendState, cullMode, blendEnabled, depthTestEnabled, depthWriteEnabled);
		if (!applied)
		{
			return applied;
		}
	}
	for (const Shader::RenderStateDeclaration& state : pass.renderStates)
	{
		auto applied =
		    ApplyImGuiRenderState(state, blendState, cullMode, blendEnabled, depthTestEnabled, depthWriteEnabled);
		if (!applied)
		{
			return applied;
		}
	}
	auto reflection = Shader::BuildShaderBindingLayout(shader.value, variant);
	if (!reflection)
	{
		return Result<void>::Failure(reflection.error);
	}
	for (const Rhi::LayoutBinding& binding : reflection.value.descriptors)
	{
		if (binding.type == Rhi::BindingType::SampledTexture)
		{
			textureBinding = binding.binding;
		}
		else if (binding.type == Rhi::BindingType::Sampler)
		{
			samplerBinding = binding.binding;
		}
		else
		{
			return Result<void>::Failure("ImGui Shader declares an unsupported descriptor type");
		}
	}
	if (reflection.value.descriptors.size() != 2)
	{
		return Result<void>::Failure("ImGui Shader must declare one texture and one sampler");
	}
	auto layoutResult = Shader::CreateShaderBindGroupLayout(reflection.value, *device);
	if (!layoutResult)
	{
		return Result<void>::Failure(layoutResult.error);
	}
	layout = std::move(layoutResult.value);
	for (const Shader::ShaderStageAsset& stage : variant.stages)
	{
		if (stage.target != Shader::ShaderTarget::VulkanSpirv)
		{
			return Result<void>::Failure("ImGui Shader has no Vulkan SPIR-V stage");
		}
		Rhi::ShaderStage stageType;
		if (stage.stage == Shader::ProgramStage::Vertex)
		{
			stageType = Rhi::ShaderStage::Vertex;
		}
		else if (stage.stage == Shader::ProgramStage::Fragment)
		{
			stageType = Rhi::ShaderStage::Fragment;
		}
		else
		{
			return Result<void>::Failure("ImGui Shader cannot contain a compute stage");
		}
		auto created = device->CreateShader({stageType, Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint});
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		if (stage.stage == Shader::ProgramStage::Vertex)
		{
			vertexShader = std::move(created.value);
		}
		else
		{
			fragmentShader = std::move(created.value);
		}
	}
	if (!vertexShader || !fragmentShader)
	{
		return Result<void>::Failure("ImGui Shader requires vertex and fragment stages");
	}
	return {};
}

Result<void> ImGuiBridge::ReloadShaders()
{
	auto files = OpenEngineFileSystem();
	if (!files)
	{
		return Result<void>::Failure(files.error);
	}
	ImGuiBridge replacement(device, {}, {});
	auto loaded = replacement.LoadShaders(files.value);
	if (!loaded)
	{
		return loaded;
	}
	UnorderedMap<ImTextureID, Rhi::BindGroupPtr> replacementGroups;
	for (const auto& entry : textureResources)
	{
		auto resources = entry.second;
		for (auto& resource : resources)
		{
			resource.binding = resource.texture ? replacement.textureBinding : replacement.samplerBinding;
		}
		auto group = device->CreateBindGroup({replacement.layout, std::move(resources)});
		if (!group)
		{
			return Result<void>::Failure(group.error);
		}
		replacementGroups.emplace(entry.first, std::move(group.value));
	}
	layout = std::move(replacement.layout);
	vertexShader = std::move(replacement.vertexShader);
	fragmentShader = std::move(replacement.fragmentShader);
	blendState = replacement.blendState;
	cullMode = replacement.cullMode;
	blendEnabled = replacement.blendEnabled;
	depthTestEnabled = replacement.depthTestEnabled;
	depthWriteEnabled = replacement.depthWriteEnabled;
	textureBinding = replacement.textureBinding;
	samplerBinding = replacement.samplerBinding;
	groups = std::move(replacementGroups);
	pipelines.clear();
	return {};
}

Result<ImTextureID> ImGuiBridge::RegisterTexture(Rhi::TexturePtr texture, Rhi::SamplerPtr sampler)
{
	if (!texture || !sampler)
	{
		return Result<ImTextureID>::Failure("ImGui image requires a texture and sampler");
	}
	if (!Rhi::Has(texture->desc.usage, Rhi::TextureUsage::Sampled))
	{
		return Result<ImTextureID>::Failure("ImGui image texture must have Sampled usage");
	}
	Vector<Rhi::BindingResource> resources;
	resources.push_back({textureBinding, {}, std::move(texture), {}});
	resources.push_back({samplerBinding, {}, {}, std::move(sampler)});
	auto group = device->CreateBindGroup({layout, resources});
	if (!group)
	{
		return Result<ImTextureID>::Failure(group.error);
	}
	const ImTextureID id = nextTextureId++;
	groups.emplace(id, std::move(group.value));
	textureResources.emplace(id, std::move(resources));
	return Result<ImTextureID>::Success(id);
}

void ImGuiBridge::UnregisterTexture(ImTextureID id)
{
	groups.erase(id);
	textureResources.erase(id);
}

void ImGuiBridge::RetireTexture(ImTextureID id)
{
	if (groups.find(id) != groups.end())
	{
		retiredTextures.push_back(id);
	}
}

Result<ImTextureID> ImGuiBridge::UploadImage(const UInt8* pixels, UInt32 width, UInt32 height)
{
	const Size byteCount = static_cast<Size>(width) * height * 4;
	auto upload = device->CreateBuffer({byteCount, Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload});
	if (!upload)
	{
		return Result<ImTextureID>::Failure(upload.error);
	}
	auto written = upload.value->Write(0, pixels, byteCount);
	if (!written)
	{
		return Result<ImTextureID>::Failure(written.error);
	}
	auto texture = device->CreateTexture(
	    {width, height, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination});
	if (!texture)
	{
		return Result<ImTextureID>::Failure(texture.error);
	}
	auto command = device->CreateCommandList();
	if (!command)
	{
		return Result<ImTextureID>::Failure(command.error);
	}
	auto result = command.value->Begin();
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::Undefined,
		                                Rhi::ResourceState::TransferDestination);
	}
	if (result)
	{
		result = command.value->CopyBufferToTexture(upload.value, texture.value);
	}
	if (result)
	{
		result = command.value->Barrier(texture.value, Rhi::ResourceState::TransferDestination,
		                                Rhi::ResourceState::ShaderRead);
	}
	if (result)
	{
		result = command.value->End();
	}
	if (!result)
	{
		return Result<ImTextureID>::Failure(result.error);
	}
	auto submitted = device->Submit(command.value);
	if (!submitted)
	{
		return Result<ImTextureID>::Failure(submitted.error);
	}
	result = submitted.value->Wait();
	if (!result)
	{
		return Result<ImTextureID>::Failure(result.error);
	}
	auto registered = RegisterTexture(std::move(texture.value), atlasSampler);
	if (!registered)
	{
		return Result<ImTextureID>::Failure(registered.error);
	}
	return registered;
}

Result<void> ImGuiBridge::LoadEditorIcons(const VirtualFileSystem& files)
{
	const auto imagePath = VirtualPath::Parse("ursa:Icons/Editor/EditorIcons.png");
	const auto manifestPath = VirtualPath::Parse("ursa:Icons/Editor/EditorIcons.json");
	auto bytes = files.Read(imagePath.value);
	auto manifest = files.Read(manifestPath.value);
	if (!bytes || !manifest || bytes.value.empty() ||
	    bytes.value.size() > static_cast<Size>((std::numeric_limits<int>::max)()))
	{
		return Result<void>::Failure("Missing or invalid Engine icon resource");
	}
	int width = 0;
	int height = 0;
	int channels = 0;
	using ImagePtr = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
	ImagePtr pixels(
	    stbi_load_from_memory(bytes.value.data(), static_cast<int>(bytes.value.size()), &width, &height, &channels, 4),
	    stbi_image_free);
	if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096)
	{
		return Result<void>::Failure("Invalid Editor icon PNG");
	}
	auto uploaded = UploadImage(pixels.get(), static_cast<UInt32>(width), static_cast<UInt32>(height));
	if (!uploaded)
	{
		return Result<void>::Failure(uploaded.error);
	}
	String text(reinterpret_cast<const char*>(manifest.value.data()), manifest.value.size());
	auto configured = Icons::SetAtlas(context, uploaded.value, text);
	if (!configured)
	{
		UnregisterTexture(uploaded.value);
		return configured;
	}
	editorIconsId = uploaded.value;
	return {};
}

Result<void> ImGuiBridge::UploadAtlas(ImTextureData* data)
{
	if (!data || data->Width <= 0 || data->Height <= 0 || !data->Pixels)
	{
		return Result<void>::Failure("ImGui requested an invalid font atlas");
	}
	const auto width = static_cast<Ursa::UInt32>(data->Width);
	const auto height = static_cast<Ursa::UInt32>(data->Height);
	if (width > std::numeric_limits<Ursa::Size>::max() / 4 / height)
	{
		return Result<void>::Failure("ImGui font atlas size overflow");
	}
	const Ursa::Size byteCount = static_cast<Ursa::Size>(width) * height * 4;
	Ursa::Vector<Ursa::UInt8> rgba(byteCount);
	if (data->Format == ImTextureFormat_RGBA32)
	{
		std::memcpy(rgba.data(), data->GetPixels(), byteCount);
	}
	else if (data->Format == ImTextureFormat_Alpha8)
	{
		for (Ursa::Size i = 0; i < static_cast<Ursa::Size>(width) * height; ++i)
		{
			rgba[i * 4 + 0] = 255;
			rgba[i * 4 + 1] = 255;
			rgba[i * 4 + 2] = 255;
			rgba[i * 4 + 3] = data->Pixels[i];
		}
	}
	else
	{
		return Result<void>::Failure("Unsupported ImGui font atlas format");
	}
	auto registered = UploadImage(rgba.data(), width, height);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	if (auto found = atlasIds.find(data); found != atlasIds.end())
	{
		UnregisterTexture(found->second);
	}
	atlasIds[data] = registered.value;
	data->SetTexID(registered.value);
	data->SetStatus(ImTextureStatus_OK);
	return {};
}

Result<void> ImGuiBridge::UpdateAtlas()
{
	auto* drawData = ImGui::GetDrawData();
	if (!drawData || !drawData->Textures)
	{
		return {};
	}
	for (ImTextureData* data : *drawData->Textures)
	{
		if (data->Status == ImTextureStatus_WantCreate || data->Status == ImTextureStatus_WantUpdates)
		{
			auto uploaded = UploadAtlas(data);
			if (!uploaded)
			{
				return uploaded;
			}
		}
		else if (data->Status == ImTextureStatus_WantDestroy)
		{
			if (auto found = atlasIds.find(data); found != atlasIds.end())
			{
				UnregisterTexture(found->second);
				atlasIds.erase(found);
			}
			data->SetTexID(ImTextureID_Invalid);
			data->SetStatus(ImTextureStatus_Destroyed);
		}
	}
	return {};
}

Result<void> ImGuiBridge::Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat,
                                 Ursa::UInt32 framebufferWidth, Ursa::UInt32 framebufferHeight)
{
	ImGui::SetCurrentContext(context);
	return Record(command, colorFormat, framebufferWidth, framebufferHeight, ImGui::GetDrawData());
}

Result<void> ImGuiBridge::Record(const Rhi::CommandListPtr& command, Rhi::Format colorFormat,
                                 Ursa::UInt32 framebufferWidth, Ursa::UInt32 framebufferHeight, const ImDrawData* data)
{
	Memory::TemporaryScope recordingMemory;
	if (!command || !framebufferWidth || !framebufferHeight)
	{
		return Result<void>::Failure("ImGui record requires a command list and nonzero framebuffer");
	}
	ImGui::SetCurrentContext(context);
	if (!data || data->TotalVtxCount == 0 || data->TotalIdxCount == 0)
	{
		return {};
	}
	const int formatKey = static_cast<int>(colorFormat);
	auto pipeline = pipelines.find(formatKey);
	if (pipeline == pipelines.end())
	{
		Rhi::GraphicsPipelineDesc desc;
		desc.vertex = vertexShader;
		desc.fragment = fragmentShader;
		desc.layout = layout;
		desc.colorFormat = colorFormat;
		desc.blend = blendEnabled;
		desc.blendState = blendState;
		desc.cullMode = cullMode;
		desc.depthTest = depthTestEnabled;
		desc.depthWrite = depthWriteEnabled;
		desc.depthCompare = depthCompare;
		desc.vertexStride = sizeof(GuiVertex);
		desc.attributes = {
		    {0, 0, Rhi::VertexFormat::Float2}, {1, 8, Rhi::VertexFormat::Float2}, {2, 16, Rhi::VertexFormat::Float4}};
		auto created = device->CreateGraphicsPipeline(desc);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		pipeline = pipelines.emplace(formatKey, std::move(created.value)).first;
	}
	Ursa::TemporaryVector<GuiVertex> vertices{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Editor)};
	Ursa::TemporaryVector<Ursa::UInt32> indices{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Editor)};
	vertices.reserve(static_cast<Ursa::Size>(data->TotalVtxCount));
	indices.reserve(static_cast<Ursa::Size>(data->TotalIdxCount));
	for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
	{
		const ImDrawList* list = data->CmdLists[listIndex];
		for (const ImDrawVert& source : list->VtxBuffer)
		{
			GuiVertex vertex{};
			vertex.position[0] = (source.pos.x - data->DisplayPos.x) / data->DisplaySize.x * 2.f - 1.f;
			vertex.position[1] = 1.f - (source.pos.y - data->DisplayPos.y) / data->DisplaySize.y * 2.f;
			vertex.uv[0] = source.uv.x;
			vertex.uv[1] = source.uv.y;
			for (int channel = 0; channel < 4; ++channel)
			{
				vertex.color[channel] = static_cast<Ursa::Float32>((source.col >> (channel * 8)) & 255u) / 255.f;
			}
			vertices.push_back(vertex);
		}
		for (ImDrawIdx index : list->IdxBuffer)
		{
			indices.push_back(static_cast<Ursa::UInt32>(index));
		}
	}
	auto vertexBufferResult =
	    device->CreateBuffer({vertices.size() * sizeof(GuiVertex), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload});
	if (!vertexBufferResult)
	{
		return Result<void>::Failure(vertexBufferResult.error);
	}
	auto indexBufferResult =
	    device->CreateBuffer({indices.size() * sizeof(Ursa::UInt32), Rhi::BufferUsage::Index, Rhi::Memory::Upload});
	if (!indexBufferResult)
	{
		return Result<void>::Failure(indexBufferResult.error);
	}
	auto result = vertexBufferResult.value->Write(0, vertices.data(), vertices.size() * sizeof(GuiVertex));
	if (result)
	{
		result = indexBufferResult.value->Write(0, indices.data(), indices.size() * sizeof(Ursa::UInt32));
	}
	if (!result)
	{
		return result;
	}
	vertexBuffer = std::move(vertexBufferResult.value);
	indexBuffer = std::move(indexBufferResult.value);
	result = command->BindPipeline(pipeline->second);
	if (result)
	{
		result = command->BindVertexBuffer(vertexBuffer);
	}
	if (result)
	{
		result = command->BindIndexBuffer(indexBuffer);
	}
	if (!result)
	{
		return result;
	}
	Ursa::UInt32 globalVertex = 0;
	Ursa::UInt32 globalIndex = 0;
	for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex)
	{
		const ImDrawList* list = data->CmdLists[listIndex];
		for (const ImDrawCmd& draw : list->CmdBuffer)
		{
			if (draw.UserCallback)
			{
				if (draw.UserCallback == ImDrawCallback_ResetRenderState)
				{
					result = command->BindPipeline(pipeline->second);
					if (result)
					{
						result = command->BindVertexBuffer(vertexBuffer);
					}
					if (result)
					{
						result = command->BindIndexBuffer(indexBuffer);
					}
				}
				else
				{
					draw.UserCallback(list, &draw);
				}
				if (!result)
				{
					return result;
				}
				continue;
			}
			const Ursa::Float32 minX = (draw.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x;
			const Ursa::Float32 minY = (draw.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y;
			const Ursa::Float32 maxX = (draw.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x;
			const Ursa::Float32 maxY = (draw.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
			const Ursa::Int32 left = std::max<Ursa::Int32>(0, static_cast<Ursa::Int32>(std::floor(minX)));
			const Ursa::Int32 top = std::max<Ursa::Int32>(0, static_cast<Ursa::Int32>(std::floor(minY)));
			const Ursa::Int32 right = std::min<Ursa::Int32>(static_cast<Ursa::Int32>(framebufferWidth),
			                                                static_cast<Ursa::Int32>(std::ceil(maxX)));
			const Ursa::Int32 bottom = std::min<Ursa::Int32>(static_cast<Ursa::Int32>(framebufferHeight),
			                                                 static_cast<Ursa::Int32>(std::ceil(maxY)));
			if (right <= left || bottom <= top || draw.ElemCount == 0)
			{
				continue;
			}
			const auto group = groups.find(draw.GetTexID());
			if (group == groups.end())
			{
				return Result<void>::Failure("ImGui draw references an unregistered texture");
			}
			result = command->BindGroup(group->second);
			if (result)
			{
				result = command->SetScissor(
				    {left, top, static_cast<Ursa::UInt32>(right - left), static_cast<Ursa::UInt32>(bottom - top)});
			}
			if (result)
			{
				result = command->DrawIndexed(draw.ElemCount, 1, 0, globalIndex + draw.IdxOffset,
				                              static_cast<Ursa::Int32>(globalVertex + draw.VtxOffset));
			}
			if (!result)
			{
				return result;
			}
		}
		globalVertex += static_cast<Ursa::UInt32>(list->VtxBuffer.Size);
		globalIndex += static_cast<Ursa::UInt32>(list->IdxBuffer.Size);
	}
	return {};
}
} // namespace Ursa::Editor
