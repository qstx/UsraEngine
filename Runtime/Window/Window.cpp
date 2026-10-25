#include <Core/Types.hpp>
#include <GLFW/glfw3.h>
#include <Window/Window.hpp>
#include <algorithm>
#include <limits>
#include <mutex>

namespace Ursa
{
namespace
{
std::mutex glfwMutex;
Ursa::UInt32 windowCount = 0;

struct KeyMapping
{
	Key key;
	int native;
};

constexpr KeyMapping keyMappings[] = {{Key::Escape, GLFW_KEY_ESCAPE},
                                      {Key::Space, GLFW_KEY_SPACE},
                                      {Key::Left, GLFW_KEY_LEFT},
                                      {Key::Right, GLFW_KEY_RIGHT},
                                      {Key::Up, GLFW_KEY_UP},
                                      {Key::Down, GLFW_KEY_DOWN},
                                      {Key::W, GLFW_KEY_W},
                                      {Key::A, GLFW_KEY_A},
                                      {Key::S, GLFW_KEY_S},
                                      {Key::D, GLFW_KEY_D},
                                      {Key::Tab, GLFW_KEY_TAB},
                                      {Key::Enter, GLFW_KEY_ENTER},
                                      {Key::Backspace, GLFW_KEY_BACKSPACE},
                                      {Key::Delete, GLFW_KEY_DELETE},
                                      {Key::Home, GLFW_KEY_HOME},
                                      {Key::End, GLFW_KEY_END},
                                      {Key::PageUp, GLFW_KEY_PAGE_UP},
                                      {Key::PageDown, GLFW_KEY_PAGE_DOWN},
                                      {Key::Insert, GLFW_KEY_INSERT},
                                      {Key::LeftShift, GLFW_KEY_LEFT_SHIFT},
                                      {Key::RightShift, GLFW_KEY_RIGHT_SHIFT},
                                      {Key::LeftControl, GLFW_KEY_LEFT_CONTROL},
                                      {Key::RightControl, GLFW_KEY_RIGHT_CONTROL},
                                      {Key::LeftAlt, GLFW_KEY_LEFT_ALT},
                                      {Key::RightAlt, GLFW_KEY_RIGHT_ALT},
                                      {Key::LeftSuper, GLFW_KEY_LEFT_SUPER},
                                      {Key::RightSuper, GLFW_KEY_RIGHT_SUPER},
                                      {Key::B, GLFW_KEY_B},
                                      {Key::C, GLFW_KEY_C},
                                      {Key::E, GLFW_KEY_E},
                                      {Key::F, GLFW_KEY_F},
                                      {Key::G, GLFW_KEY_G},
                                      {Key::H, GLFW_KEY_H},
                                      {Key::I, GLFW_KEY_I},
                                      {Key::J, GLFW_KEY_J},
                                      {Key::K, GLFW_KEY_K},
                                      {Key::L, GLFW_KEY_L},
                                      {Key::M, GLFW_KEY_M},
                                      {Key::N, GLFW_KEY_N},
                                      {Key::O, GLFW_KEY_O},
                                      {Key::P, GLFW_KEY_P},
                                      {Key::Q, GLFW_KEY_Q},
                                      {Key::R, GLFW_KEY_R},
                                      {Key::T, GLFW_KEY_T},
                                      {Key::U, GLFW_KEY_U},
                                      {Key::V, GLFW_KEY_V},
                                      {Key::X, GLFW_KEY_X},
                                      {Key::Y, GLFW_KEY_Y},
                                      {Key::Z, GLFW_KEY_Z},
                                      {Key::Digit0, GLFW_KEY_0},
                                      {Key::Digit1, GLFW_KEY_1},
                                      {Key::Digit2, GLFW_KEY_2},
                                      {Key::Digit3, GLFW_KEY_3},
                                      {Key::Digit4, GLFW_KEY_4},
                                      {Key::Digit5, GLFW_KEY_5},
                                      {Key::Digit6, GLFW_KEY_6},
                                      {Key::Digit7, GLFW_KEY_7},
                                      {Key::Digit8, GLFW_KEY_8},
                                      {Key::Digit9, GLFW_KEY_9}};

int GlfwKey(Key key)
{
	for (const auto& mapping : keyMappings)
	{
		if (mapping.key == key)
		{
			return mapping.native;
		}
	}
	return GLFW_KEY_UNKNOWN;
}

Key UrsaKey(int native)
{
	for (const auto& mapping : keyMappings)
	{
		if (mapping.native == native)
		{
			return mapping.key;
		}
	}
	return Key::Unknown;
}

int GlfwButton(MouseButton button)
{
	switch (button)
	{
		case MouseButton::Left:
			return GLFW_MOUSE_BUTTON_LEFT;
		case MouseButton::Right:
			return GLFW_MOUSE_BUTTON_RIGHT;
		case MouseButton::Middle:
			return GLFW_MOUSE_BUTTON_MIDDLE;
		case MouseButton::Other:
			return -1;
	}
	return -1;
}

MouseButton UrsaButton(int native)
{
	switch (native)
	{
		case GLFW_MOUSE_BUTTON_LEFT:
			return MouseButton::Left;
		case GLFW_MOUSE_BUTTON_RIGHT:
			return MouseButton::Right;
		case GLFW_MOUSE_BUTTON_MIDDLE:
			return MouseButton::Middle;
		default:
			return MouseButton::Other;
	}
}

Window* WindowFor(GLFWwindow* handle)
{
	return static_cast<Window*>(glfwGetWindowUserPointer(handle));
}

void Send(GLFWwindow* handle, const WindowEvent& event)
{
	if (auto* window = WindowFor(handle))
	{
		Detail::WindowAccess::Dispatch(*window, event);
	}
}
} // namespace

Result<std::shared_ptr<Window>> Window::Create(Ursa::UInt32 width, Ursa::UInt32 height, const String& title,
                                               bool visible)
{
	WindowCreateDesc desc;
	desc.width = width;
	desc.height = height;
	desc.title = title;
	desc.visible = visible;
	return Create(desc);
}

Result<std::shared_ptr<Window>> Window::Create(const WindowCreateDesc& desc)
{
	if (desc.width == 0 || desc.height == 0)
	{
		return Result<std::shared_ptr<Window>>::Failure("Window dimensions must be positive");
	}
	std::lock_guard<std::mutex> lock(glfwMutex);
	if (windowCount == 0 && !glfwInit())
	{
		return Result<std::shared_ptr<Window>>::Failure("GLFW initialization failed");
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_DECORATED, desc.decorated ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_FLOATING, desc.floating ? GLFW_TRUE : GLFW_FALSE);
	auto* handle = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height), desc.title.c_str(),
	                                nullptr, nullptr);
	if (!handle)
	{
		const char* message = nullptr;
		glfwGetError(&message);
		if (windowCount == 0)
		{
			glfwTerminate();
		}
		return Result<std::shared_ptr<Window>>::Failure(String("Window creation failed: ") +
		                                                (message ? message : "unknown GLFW error"));
	}
	++windowCount;
	auto window = std::shared_ptr<Window>(new Window(handle));
	glfwSetWindowUserPointer(handle, window.get());
	glfwSetKeyCallback(handle,
	                   [](GLFWwindow* native, int key, int, int action, int)
	                   {
		                   WindowEvent event;
		                   event.type = action == GLFW_RELEASE  ? WindowEventType::KeyReleased
		                                : action == GLFW_REPEAT ? WindowEventType::KeyRepeated
		                                                        : WindowEventType::KeyPressed;
		                   event.key = UrsaKey(key);
		                   Send(native, event);
	                   });
	glfwSetCharCallback(handle,
	                    [](GLFWwindow* native, unsigned int codepoint)
	                    {
		                    WindowEvent event;
		                    event.type = WindowEventType::Character;
		                    event.codepoint = codepoint;
		                    Send(native, event);
	                    });
	glfwSetCursorPosCallback(handle,
	                         [](GLFWwindow* native, Ursa::Float64 x, Ursa::Float64 y)
	                         {
		                         WindowEvent event;
		                         event.type = WindowEventType::CursorMoved;
		                         event.x = x;
		                         event.y = y;
		                         Send(native, event);
	                         });
	glfwSetMouseButtonCallback(handle,
	                           [](GLFWwindow* native, int button, int action, int)
	                           {
		                           WindowEvent event;
		                           event.type = action == GLFW_RELEASE ? WindowEventType::MouseReleased
		                                                               : WindowEventType::MousePressed;
		                           event.button = UrsaButton(button);
		                           Send(native, event);
	                           });
	glfwSetScrollCallback(handle,
	                      [](GLFWwindow* native, Ursa::Float64 x, Ursa::Float64 y)
	                      {
		                      WindowEvent event;
		                      event.type = WindowEventType::Scrolled;
		                      event.x = x;
		                      event.y = y;
		                      Send(native, event);
	                      });
	glfwSetWindowFocusCallback(handle,
	                           [](GLFWwindow* native, int focused)
	                           {
		                           WindowEvent event;
		                           event.type = WindowEventType::Focused;
		                           event.focused = focused == GLFW_TRUE;
		                           Send(native, event);
	                           });
	glfwSetWindowPosCallback(handle,
	                         [](GLFWwindow* native, int x, int y)
	                         {
		                         WindowEvent event;
		                         event.type = WindowEventType::PositionChanged;
		                         event.x = static_cast<Ursa::Float64>(x);
		                         event.y = static_cast<Ursa::Float64>(y);
		                         Send(native, event);
	                         });
	glfwSetWindowSizeCallback(handle,
	                          [](GLFWwindow* native, int width, int height)
	                          {
		                          WindowEvent event;
		                          event.type = WindowEventType::SizeChanged;
		                          event.x = static_cast<Ursa::Float64>(width);
		                          event.y = static_cast<Ursa::Float64>(height);
		                          Send(native, event);
	                          });
	glfwSetFramebufferSizeCallback(handle,
	                               [](GLFWwindow* native, int width, int height)
	                               {
		                               WindowEvent event;
		                               event.type = WindowEventType::FramebufferSizeChanged;
		                               event.x = static_cast<Ursa::Float64>(width);
		                               event.y = static_cast<Ursa::Float64>(height);
		                               Send(native, event);
	                               });
	glfwSetWindowContentScaleCallback(handle,
	                                  [](GLFWwindow* native, float x, float y)
	                                  {
		                                  WindowEvent event;
		                                  event.type = WindowEventType::ContentScaleChanged;
		                                  event.x = static_cast<Ursa::Float64>(x);
		                                  event.y = static_cast<Ursa::Float64>(y);
		                                  Send(native, event);
	                                  });
	return Result<std::shared_ptr<Window>>::Success(std::move(window));
}

Result<WindowWorkArea> Window::PrimaryMonitorWorkArea()
{
	std::lock_guard<std::mutex> lock(glfwMutex);
	const bool temporaryInitialization = windowCount == 0;
	if (temporaryInitialization && !glfwInit())
	{
		return Result<WindowWorkArea>::Failure("GLFW initialization failed while querying the monitor work area");
	}
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	if (!monitor)
	{
		if (temporaryInitialization)
		{
			glfwTerminate();
		}
		return Result<WindowWorkArea>::Failure("No primary monitor is available");
	}
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
	if (width <= 0 || height <= 0)
	{
		if (temporaryInitialization)
		{
			glfwTerminate();
		}
		return Result<WindowWorkArea>::Failure("Primary monitor has an invalid work area");
	}
	if (temporaryInitialization)
	{
		glfwTerminate();
	}
	return Result<WindowWorkArea>::Success({static_cast<Ursa::Int32>(x), static_cast<Ursa::Int32>(y),
	                                        static_cast<Ursa::UInt32>(width), static_cast<Ursa::UInt32>(height)});
}

Result<Vector<WindowMonitorInfo>> Window::Monitors()
{
	std::lock_guard<std::mutex> lock(glfwMutex);
	const bool temporaryInitialization = windowCount == 0;
	if (temporaryInitialization && !glfwInit())
	{
		return Result<Vector<WindowMonitorInfo>>::Failure("GLFW initialization failed while querying monitors");
	}
	int count = 0;
	GLFWmonitor** monitors = glfwGetMonitors(&count);
	if (!monitors || count <= 0)
	{
		if (temporaryInitialization)
		{
			glfwTerminate();
		}
		return Result<Vector<WindowMonitorInfo>>::Failure("No display monitors are available");
	}
	Vector<WindowMonitorInfo> result;
	result.reserve(static_cast<Size>(count));
	for (int index = 0; index < count; ++index)
	{
		int x = 0;
		int y = 0;
		int workX = 0;
		int workY = 0;
		int workWidth = 0;
		int workHeight = 0;
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		const GLFWvidmode* mode = glfwGetVideoMode(monitors[index]);
		glfwGetMonitorPos(monitors[index], &x, &y);
		glfwGetMonitorWorkarea(monitors[index], &workX, &workY, &workWidth, &workHeight);
		glfwGetMonitorContentScale(monitors[index], &scaleX, &scaleY);
		if (!mode || mode->width <= 0 || mode->height <= 0 || workWidth <= 0 || workHeight <= 0 || scaleX <= 0.0f ||
		    scaleY <= 0.0f)
		{
			continue;
		}
		result.push_back({{static_cast<Ursa::Int32>(x), static_cast<Ursa::Int32>(y),
		                   static_cast<Ursa::UInt32>(mode->width), static_cast<Ursa::UInt32>(mode->height)},
		                  {static_cast<Ursa::Int32>(workX), static_cast<Ursa::Int32>(workY),
		                   static_cast<Ursa::UInt32>(workWidth), static_cast<Ursa::UInt32>(workHeight)},
		                  static_cast<Ursa::Float32>(scaleX),
		                  static_cast<Ursa::Float32>(scaleY)});
	}
	if (temporaryInitialization)
	{
		glfwTerminate();
	}
	return result.empty() ? Result<Vector<WindowMonitorInfo>>::Failure("No monitor has a valid work area")
	                      : Result<Vector<WindowMonitorInfo>>::Success(std::move(result));
}

Window::~Window()
{
	std::lock_guard<std::mutex> lock(glfwMutex);
	if (handle)
	{
		glfwSetWindowUserPointer(static_cast<GLFWwindow*>(handle), nullptr);
		glfwDestroyWindow(static_cast<GLFWwindow*>(handle));
	}
	if (windowCount && --windowCount == 0)
	{
		glfwTerminate();
	}
}

void Window::PollEvents() const
{
	glfwPollEvents();
}

void Window::WaitEvents() const
{
	glfwWaitEvents();
}

void Window::WaitEventsTimeout(Float64 timeoutSeconds) const
{
	glfwWaitEventsTimeout(std::max(0.0, timeoutSeconds));
}

bool Window::ShouldClose() const
{
	return glfwWindowShouldClose(static_cast<GLFWwindow*>(handle)) != 0;
}

void Window::RequestClose()
{
	glfwSetWindowShouldClose(static_cast<GLFWwindow*>(handle), GLFW_TRUE);
}

void Window::CancelClose()
{
	glfwSetWindowShouldClose(static_cast<GLFWwindow*>(handle), GLFW_FALSE);
}

void Window::SetTitle(const String& title)
{
	glfwSetWindowTitle(static_cast<GLFWwindow*>(handle), title.c_str());
}

String Window::Title() const
{
	return glfwGetWindowTitle(static_cast<GLFWwindow*>(handle));
}

Result<void> Window::SetIcon(Ursa::UInt32 width, Ursa::UInt32 height, const Vector<Ursa::UInt8>& pixels)
{
	if (!width || !height || width > static_cast<Ursa::UInt32>(std::numeric_limits<int>::max()) ||
	    height > static_cast<Ursa::UInt32>(std::numeric_limits<int>::max()) ||
	    static_cast<Ursa::UInt64>(width) * height * 4 != pixels.size())
	{
		return Result<void>::Failure("Window icon must contain width x height RGBA8 pixels");
	}
	GLFWimage icon{static_cast<int>(width), static_cast<int>(height), const_cast<Ursa::UInt8*>(pixels.data())};
	glfwGetError(nullptr);
	glfwSetWindowIcon(static_cast<GLFWwindow*>(handle), 1, &icon);
	const char* error = nullptr;
	if (glfwGetError(&error) != GLFW_NO_ERROR)
	{
		return Result<void>::Failure(error ? error : "Cannot set window icon");
	}
	return {};
}

String Window::ClipboardText() const
{
	const char* text = glfwGetClipboardString(static_cast<GLFWwindow*>(handle));
	return text ? String(text) : String{};
}

void Window::SetClipboardText(const String& text)
{
	glfwSetClipboardString(static_cast<GLFWwindow*>(handle), text.c_str());
}

bool Window::KeyDown(Key key) const
{
	const int native = GlfwKey(key);
	return native != GLFW_KEY_UNKNOWN && glfwGetKey(static_cast<GLFWwindow*>(handle), native) == GLFW_PRESS;
}

bool Window::MouseDown(MouseButton button) const
{
	const int native = GlfwButton(button);
	return native >= 0 && glfwGetMouseButton(static_cast<GLFWwindow*>(handle), native) == GLFW_PRESS;
}

std::pair<Ursa::Float64, Ursa::Float64> Window::CursorPosition() const
{
	Ursa::Float64 x = 0.0;
	Ursa::Float64 y = 0.0;
	glfwGetCursorPos(static_cast<GLFWwindow*>(handle), &x, &y);
	return {x, y};
}

std::pair<Ursa::Float64, Ursa::Float64> Window::DesktopCursorPosition() const
{
	const auto cursor = CursorPosition();
	const auto position = Position();
	return {cursor.first + position.first, cursor.second + position.second};
}

std::pair<Ursa::Int32, Ursa::Int32> Window::Position() const
{
	int x = 0;
	int y = 0;
	glfwGetWindowPos(static_cast<GLFWwindow*>(handle), &x, &y);
	return {static_cast<Ursa::Int32>(x), static_cast<Ursa::Int32>(y)};
}

void Window::SetPosition(Ursa::Int32 x, Ursa::Int32 y)
{
	glfwSetWindowPos(static_cast<GLFWwindow*>(handle), static_cast<int>(x), static_cast<int>(y));
}

std::pair<Ursa::Float32, Ursa::Float32> Window::ContentScale() const
{
	float x = 1.0f;
	float y = 1.0f;
	glfwGetWindowContentScale(static_cast<GLFWwindow*>(handle), &x, &y);
	return {static_cast<Ursa::Float32>(x), static_cast<Ursa::Float32>(y)};
}

bool Window::Focused() const
{
	return glfwGetWindowAttrib(static_cast<GLFWwindow*>(handle), GLFW_FOCUSED) == GLFW_TRUE;
}

void Window::Focus()
{
	glfwFocusWindow(static_cast<GLFWwindow*>(handle));
}

Result<Ursa::UInt64> Window::Subscribe(WindowEventCallback callback)
{
	if (!callback)
	{
		return Result<Ursa::UInt64>::Failure("Window event callback is empty");
	}
	const Ursa::UInt64 id = nextSubscription++;
	subscriptions.push_back({id, std::move(callback)});
	return Result<Ursa::UInt64>::Success(id);
}

void Window::Unsubscribe(Ursa::UInt64 subscription)
{
	auto found = std::find_if(subscriptions.begin(), subscriptions.end(),
	                          [subscription](const auto& entry) { return entry.first == subscription; });
	if (found != subscriptions.end())
	{
		subscriptions.erase(found);
	}
}

void Window::DispatchEvent(const WindowEvent& event)
{
	const auto callbacks = subscriptions;
	for (const auto& entry : callbacks)
	{
		entry.second(event);
	}
}

void Window::Resize(Ursa::UInt32 width, Ursa::UInt32 height)
{
	if (width && height)
	{
		glfwSetWindowSize(static_cast<GLFWwindow*>(handle), static_cast<int>(width), static_cast<int>(height));
	}
}

void Window::Show()
{
	glfwShowWindow(static_cast<GLFWwindow*>(handle));
}

void Window::Minimize()
{
	glfwIconifyWindow(static_cast<GLFWwindow*>(handle));
}

void Window::Restore()
{
	glfwRestoreWindow(static_cast<GLFWwindow*>(handle));
}

Ursa::UInt32 Window::Width() const
{
	int w = 0, h = 0;
	glfwGetWindowSize(static_cast<GLFWwindow*>(handle), &w, &h);
	return static_cast<Ursa::UInt32>(w);
}

Ursa::UInt32 Window::Height() const
{
	int w = 0, h = 0;
	glfwGetWindowSize(static_cast<GLFWwindow*>(handle), &w, &h);
	return static_cast<Ursa::UInt32>(h);
}

Ursa::UInt32 Window::FramebufferWidth() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(static_cast<GLFWwindow*>(handle), &w, &h);
	return static_cast<Ursa::UInt32>(w);
}

bool Window::Minimized() const
{
	return glfwGetWindowAttrib(static_cast<GLFWwindow*>(handle), GLFW_ICONIFIED) == GLFW_TRUE;
}

Ursa::UInt32 Window::FramebufferHeight() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(static_cast<GLFWwindow*>(handle), &w, &h);
	return static_cast<Ursa::UInt32>(h);
}

} // namespace Ursa
