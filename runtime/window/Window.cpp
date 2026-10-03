#include <GLFW/glfw3.h>
#include <window/Window.hpp>
#include <algorithm>
#include <mutex>

namespace Ursa
{
namespace
{
std::mutex glfwMutex;
uint32_t windowCount = 0;

struct KeyMapping
{
	Key key;
	int native;
};

constexpr KeyMapping keyMappings[] = {{Key::escape, GLFW_KEY_ESCAPE},
                                      {Key::space, GLFW_KEY_SPACE},
                                      {Key::left, GLFW_KEY_LEFT},
                                      {Key::right, GLFW_KEY_RIGHT},
                                      {Key::up, GLFW_KEY_UP},
                                      {Key::down, GLFW_KEY_DOWN},
                                      {Key::w, GLFW_KEY_W},
                                      {Key::a, GLFW_KEY_A},
                                      {Key::s, GLFW_KEY_S},
                                      {Key::d, GLFW_KEY_D},
                                      {Key::tab, GLFW_KEY_TAB},
                                      {Key::enter, GLFW_KEY_ENTER},
                                      {Key::backspace, GLFW_KEY_BACKSPACE},
                                      {Key::Delete, GLFW_KEY_DELETE},
                                      {Key::home, GLFW_KEY_HOME},
                                      {Key::end, GLFW_KEY_END},
                                      {Key::pageUp, GLFW_KEY_PAGE_UP},
                                      {Key::pageDown, GLFW_KEY_PAGE_DOWN},
                                      {Key::insert, GLFW_KEY_INSERT},
                                      {Key::leftShift, GLFW_KEY_LEFT_SHIFT},
                                      {Key::rightShift, GLFW_KEY_RIGHT_SHIFT},
                                      {Key::leftControl, GLFW_KEY_LEFT_CONTROL},
                                      {Key::rightControl, GLFW_KEY_RIGHT_CONTROL},
                                      {Key::leftAlt, GLFW_KEY_LEFT_ALT},
                                      {Key::rightAlt, GLFW_KEY_RIGHT_ALT},
                                      {Key::leftSuper, GLFW_KEY_LEFT_SUPER},
                                      {Key::rightSuper, GLFW_KEY_RIGHT_SUPER},
                                      {Key::b, GLFW_KEY_B},
                                      {Key::c, GLFW_KEY_C},
                                      {Key::e, GLFW_KEY_E},
                                      {Key::f, GLFW_KEY_F},
                                      {Key::g, GLFW_KEY_G},
                                      {Key::h, GLFW_KEY_H},
                                      {Key::i, GLFW_KEY_I},
                                      {Key::j, GLFW_KEY_J},
                                      {Key::k, GLFW_KEY_K},
                                      {Key::l, GLFW_KEY_L},
                                      {Key::m, GLFW_KEY_M},
                                      {Key::n, GLFW_KEY_N},
                                      {Key::o, GLFW_KEY_O},
                                      {Key::p, GLFW_KEY_P},
                                      {Key::q, GLFW_KEY_Q},
                                      {Key::r, GLFW_KEY_R},
                                      {Key::t, GLFW_KEY_T},
                                      {Key::u, GLFW_KEY_U},
                                      {Key::v, GLFW_KEY_V},
                                      {Key::x, GLFW_KEY_X},
                                      {Key::y, GLFW_KEY_Y},
                                      {Key::z, GLFW_KEY_Z},
                                      {Key::digit0, GLFW_KEY_0},
                                      {Key::digit1, GLFW_KEY_1},
                                      {Key::digit2, GLFW_KEY_2},
                                      {Key::digit3, GLFW_KEY_3},
                                      {Key::digit4, GLFW_KEY_4},
                                      {Key::digit5, GLFW_KEY_5},
                                      {Key::digit6, GLFW_KEY_6},
                                      {Key::digit7, GLFW_KEY_7},
                                      {Key::digit8, GLFW_KEY_8},
                                      {Key::digit9, GLFW_KEY_9}};

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
	return Key::unknown;
}

int GlfwButton(MouseButton button)
{
	switch (button)
	{
		case MouseButton::left:
			return GLFW_MOUSE_BUTTON_LEFT;
		case MouseButton::right:
			return GLFW_MOUSE_BUTTON_RIGHT;
		case MouseButton::middle:
			return GLFW_MOUSE_BUTTON_MIDDLE;
		case MouseButton::other:
			return -1;
	}
	return -1;
}

MouseButton UrsaButton(int native)
{
	switch (native)
	{
		case GLFW_MOUSE_BUTTON_LEFT:
			return MouseButton::left;
		case GLFW_MOUSE_BUTTON_RIGHT:
			return MouseButton::right;
		case GLFW_MOUSE_BUTTON_MIDDLE:
			return MouseButton::middle;
		default:
			return MouseButton::other;
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

Result<std::shared_ptr<Window>> Window::Create(uint32_t width, uint32_t height, const String& title, bool visible)
{
	if (width == 0 || height == 0)
	{
		return Result<std::shared_ptr<Window>>::Failure("Window dimensions must be positive");
	}
	std::lock_guard<std::mutex> lock(glfwMutex);
	if (windowCount == 0 && !glfwInit())
	{
		return Result<std::shared_ptr<Window>>::Failure("GLFW initialization failed");
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
	auto* handle = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);
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
		                   event.type = action == GLFW_RELEASE  ? WindowEventType::keyReleased
		                                : action == GLFW_REPEAT ? WindowEventType::keyRepeated
		                                                        : WindowEventType::keyPressed;
		                   event.key = UrsaKey(key);
		                   Send(native, event);
	                   });
	glfwSetCharCallback(handle,
	                    [](GLFWwindow* native, unsigned int codepoint)
	                    {
		                    WindowEvent event;
		                    event.type = WindowEventType::character;
		                    event.codepoint = codepoint;
		                    Send(native, event);
	                    });
	glfwSetCursorPosCallback(handle,
	                         [](GLFWwindow* native, double x, double y)
	                         {
		                         WindowEvent event;
		                         event.type = WindowEventType::cursorMoved;
		                         event.x = x;
		                         event.y = y;
		                         Send(native, event);
	                         });
	glfwSetMouseButtonCallback(handle,
	                           [](GLFWwindow* native, int button, int action, int)
	                           {
		                           WindowEvent event;
		                           event.type = action == GLFW_RELEASE ? WindowEventType::mouseReleased
		                                                               : WindowEventType::mousePressed;
		                           event.button = UrsaButton(button);
		                           Send(native, event);
	                           });
	glfwSetScrollCallback(handle,
	                      [](GLFWwindow* native, double x, double y)
	                      {
		                      WindowEvent event;
		                      event.type = WindowEventType::scrolled;
		                      event.x = x;
		                      event.y = y;
		                      Send(native, event);
	                      });
	glfwSetWindowFocusCallback(handle,
	                           [](GLFWwindow* native, int focused)
	                           {
		                           WindowEvent event;
		                           event.type = WindowEventType::focused;
		                           event.focused = focused == GLFW_TRUE;
		                           Send(native, event);
	                           });
	return Result<std::shared_ptr<Window>>::Success(std::move(window));
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

std::pair<double, double> Window::CursorPosition() const
{
	double x = 0.0;
	double y = 0.0;
	glfwGetCursorPos(static_cast<GLFWwindow*>(handle), &x, &y);
	return {x, y};
}

Result<uint64_t> Window::Subscribe(WindowEventCallback callback)
{
	if (!callback)
	{
		return Result<uint64_t>::Failure("Window event callback is empty");
	}
	const uint64_t id = nextSubscription++;
	subscriptions.push_back({id, std::move(callback)});
	return Result<uint64_t>::Success(id);
}

void Window::Unsubscribe(uint64_t subscription)
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

void Window::Resize(uint32_t width, uint32_t height)
{
	if (width && height)
	{
		glfwSetWindowSize(static_cast<GLFWwindow*>(handle), static_cast<int>(width), static_cast<int>(height));
	}
}

void Window::Minimize()
{
	glfwIconifyWindow(static_cast<GLFWwindow*>(handle));
}

void Window::Restore()
{
	glfwRestoreWindow(static_cast<GLFWwindow*>(handle));
}

uint32_t Window::FramebufferWidth() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(static_cast<GLFWwindow*>(handle), &w, &h);
	return static_cast<uint32_t>(w);
}

uint32_t Window::FramebufferHeight() const
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(static_cast<GLFWwindow*>(handle), &w, &h);
	return static_cast<uint32_t>(h);
}
} // namespace Ursa
