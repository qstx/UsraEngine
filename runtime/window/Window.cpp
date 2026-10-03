#include <GLFW/glfw3.h>
#include <window/Window.hpp>
#include <algorithm>
#include <mutex>

namespace ursa {
namespace {
std::mutex glfwMutex;
uint32_t windowCount = 0;

struct KeyMapping {
    Key key;
    int native;
};

constexpr KeyMapping keyMappings[] = {
    {Key::Escape, GLFW_KEY_ESCAPE}, {Key::Space, GLFW_KEY_SPACE}, {Key::Left, GLFW_KEY_LEFT},
    {Key::Right, GLFW_KEY_RIGHT}, {Key::Up, GLFW_KEY_UP}, {Key::Down, GLFW_KEY_DOWN},
    {Key::W, GLFW_KEY_W}, {Key::A, GLFW_KEY_A}, {Key::S, GLFW_KEY_S}, {Key::D, GLFW_KEY_D},
    {Key::Tab, GLFW_KEY_TAB}, {Key::Enter, GLFW_KEY_ENTER}, {Key::Backspace, GLFW_KEY_BACKSPACE},
    {Key::Delete, GLFW_KEY_DELETE}, {Key::Home, GLFW_KEY_HOME}, {Key::End, GLFW_KEY_END},
    {Key::PageUp, GLFW_KEY_PAGE_UP}, {Key::PageDown, GLFW_KEY_PAGE_DOWN}, {Key::Insert, GLFW_KEY_INSERT},
    {Key::LeftShift, GLFW_KEY_LEFT_SHIFT}, {Key::RightShift, GLFW_KEY_RIGHT_SHIFT},
    {Key::LeftControl, GLFW_KEY_LEFT_CONTROL}, {Key::RightControl, GLFW_KEY_RIGHT_CONTROL},
    {Key::LeftAlt, GLFW_KEY_LEFT_ALT}, {Key::RightAlt, GLFW_KEY_RIGHT_ALT},
    {Key::LeftSuper, GLFW_KEY_LEFT_SUPER}, {Key::RightSuper, GLFW_KEY_RIGHT_SUPER},
    {Key::B, GLFW_KEY_B}, {Key::C, GLFW_KEY_C}, {Key::E, GLFW_KEY_E}, {Key::F, GLFW_KEY_F},
    {Key::G, GLFW_KEY_G}, {Key::H, GLFW_KEY_H}, {Key::I, GLFW_KEY_I}, {Key::J, GLFW_KEY_J},
    {Key::K, GLFW_KEY_K}, {Key::L, GLFW_KEY_L}, {Key::M, GLFW_KEY_M}, {Key::N, GLFW_KEY_N},
    {Key::O, GLFW_KEY_O}, {Key::P, GLFW_KEY_P}, {Key::Q, GLFW_KEY_Q}, {Key::R, GLFW_KEY_R},
    {Key::T, GLFW_KEY_T}, {Key::U, GLFW_KEY_U}, {Key::V, GLFW_KEY_V}, {Key::X, GLFW_KEY_X},
    {Key::Y, GLFW_KEY_Y}, {Key::Z, GLFW_KEY_Z},
    {Key::Digit0, GLFW_KEY_0}, {Key::Digit1, GLFW_KEY_1}, {Key::Digit2, GLFW_KEY_2},
    {Key::Digit3, GLFW_KEY_3}, {Key::Digit4, GLFW_KEY_4}, {Key::Digit5, GLFW_KEY_5},
    {Key::Digit6, GLFW_KEY_6}, {Key::Digit7, GLFW_KEY_7}, {Key::Digit8, GLFW_KEY_8},
    {Key::Digit9, GLFW_KEY_9}
};

int glfwKey(Key key) {
    for (const auto &mapping : keyMappings)
        if (mapping.key == key)
            return mapping.native;
    return GLFW_KEY_UNKNOWN;
}

Key ursaKey(int native) {
    for (const auto &mapping : keyMappings)
        if (mapping.native == native)
            return mapping.key;
    return Key::Unknown;
}

int glfwButton(MouseButton button) {
    switch (button) {
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

MouseButton ursaButton(int native) {
    switch (native) {
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

Window *windowFor(GLFWwindow *handle) {
    return static_cast<Window *>(glfwGetWindowUserPointer(handle));
}

void send(GLFWwindow *handle, const WindowEvent &event) {
    if (auto *window = windowFor(handle))
        detail::WindowAccess::dispatch(*window, event);
}
} // namespace

Result<std::shared_ptr<Window>> Window::create(uint32_t width, uint32_t height, const String &title, bool visible) {
    if (width == 0 || height == 0)
        return Result<std::shared_ptr<Window>>::failure("Window dimensions must be positive");
    std::lock_guard<std::mutex> lock(glfwMutex);
    if (windowCount == 0 && !glfwInit())
        return Result<std::shared_ptr<Window>>::failure("GLFW initialization failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    auto *handle = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);
    if (!handle) {
        const char *message = nullptr;
        glfwGetError(&message);
        if (windowCount == 0)
            glfwTerminate();
        return Result<std::shared_ptr<Window>>::failure(String("Window creation failed: ") +
                                                        (message ? message : "unknown GLFW error"));
    }
    ++windowCount;
    auto window = std::shared_ptr<Window>(new Window(handle));
    glfwSetWindowUserPointer(handle, window.get());
    glfwSetKeyCallback(handle, [](GLFWwindow *native, int key, int, int action, int) {
        WindowEvent event;
        event.type = action == GLFW_RELEASE ? WindowEventType::KeyReleased
                                           : action == GLFW_REPEAT ? WindowEventType::KeyRepeated
                                                                   : WindowEventType::KeyPressed;
        event.key = ursaKey(key);
        send(native, event);
    });
    glfwSetCharCallback(handle, [](GLFWwindow *native, unsigned int codepoint) {
        WindowEvent event;
        event.type = WindowEventType::Character;
        event.codepoint = codepoint;
        send(native, event);
    });
    glfwSetCursorPosCallback(handle, [](GLFWwindow *native, double x, double y) {
        WindowEvent event;
        event.type = WindowEventType::CursorMoved;
        event.x = x;
        event.y = y;
        send(native, event);
    });
    glfwSetMouseButtonCallback(handle, [](GLFWwindow *native, int button, int action, int) {
        WindowEvent event;
        event.type = action == GLFW_RELEASE ? WindowEventType::MouseReleased : WindowEventType::MousePressed;
        event.button = ursaButton(button);
        send(native, event);
    });
    glfwSetScrollCallback(handle, [](GLFWwindow *native, double x, double y) {
        WindowEvent event;
        event.type = WindowEventType::Scrolled;
        event.x = x;
        event.y = y;
        send(native, event);
    });
    glfwSetWindowFocusCallback(handle, [](GLFWwindow *native, int focused) {
        WindowEvent event;
        event.type = WindowEventType::Focused;
        event.focused = focused == GLFW_TRUE;
        send(native, event);
    });
    return Result<std::shared_ptr<Window>>::success(std::move(window));
}

Window::~Window() {
    std::lock_guard<std::mutex> lock(glfwMutex);
    if (handle_) {
        glfwSetWindowUserPointer(static_cast<GLFWwindow *>(handle_), nullptr);
        glfwDestroyWindow(static_cast<GLFWwindow *>(handle_));
    }
    if (windowCount && --windowCount == 0)
        glfwTerminate();
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::waitEvents() const {
    glfwWaitEvents();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(static_cast<GLFWwindow *>(handle_)) != 0;
}

void Window::requestClose() {
    glfwSetWindowShouldClose(static_cast<GLFWwindow *>(handle_), GLFW_TRUE);
}

void Window::cancelClose() {
    glfwSetWindowShouldClose(static_cast<GLFWwindow *>(handle_), GLFW_FALSE);
}

void Window::setTitle(const String &title) {
    glfwSetWindowTitle(static_cast<GLFWwindow *>(handle_), title.c_str());
}

String Window::title() const {
    return glfwGetWindowTitle(static_cast<GLFWwindow *>(handle_));
}

String Window::clipboardText() const {
    const char *text = glfwGetClipboardString(static_cast<GLFWwindow *>(handle_));
    return text ? String(text) : String{};
}

void Window::setClipboardText(const String &text) {
    glfwSetClipboardString(static_cast<GLFWwindow *>(handle_), text.c_str());
}

bool Window::keyDown(Key key) const {
    const int native = glfwKey(key);
    return native != GLFW_KEY_UNKNOWN && glfwGetKey(static_cast<GLFWwindow *>(handle_), native) == GLFW_PRESS;
}

bool Window::mouseDown(MouseButton button) const {
    const int native = glfwButton(button);
    return native >= 0 && glfwGetMouseButton(static_cast<GLFWwindow *>(handle_), native) == GLFW_PRESS;
}

std::pair<double, double> Window::cursorPosition() const {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(static_cast<GLFWwindow *>(handle_), &x, &y);
    return {x, y};
}

Result<uint64_t> Window::subscribe(WindowEventCallback callback) {
    if (!callback)
        return Result<uint64_t>::failure("Window event callback is empty");
    const uint64_t id = nextSubscription_++;
    subscriptions_.push_back({id, std::move(callback)});
    return Result<uint64_t>::success(id);
}

void Window::unsubscribe(uint64_t subscription) {
    auto found = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                              [subscription](const auto &entry) { return entry.first == subscription; });
    if (found != subscriptions_.end())
        subscriptions_.erase(found);
}

void Window::dispatchEvent(const WindowEvent &event) {
    const auto callbacks = subscriptions_;
    for (const auto &entry : callbacks)
        entry.second(event);
}

void Window::resize(uint32_t width, uint32_t height) {
    if (width && height)
        glfwSetWindowSize(static_cast<GLFWwindow *>(handle_), static_cast<int>(width), static_cast<int>(height));
}

void Window::minimize() {
    glfwIconifyWindow(static_cast<GLFWwindow *>(handle_));
}

void Window::restore() {
    glfwRestoreWindow(static_cast<GLFWwindow *>(handle_));
}

uint32_t Window::framebufferWidth() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow *>(handle_), &w, &h);
    return static_cast<uint32_t>(w);
}

uint32_t Window::framebufferHeight() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow *>(handle_), &w, &h);
    return static_cast<uint32_t>(h);
}
} // namespace ursa
