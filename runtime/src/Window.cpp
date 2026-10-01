#include <GLFW/glfw3.h>
#include <Ursa/Window.hpp>
#include <mutex>

namespace ursa {
namespace {
std::mutex glfwMutex;
uint32_t windowCount = 0;

int glfwKey(Key key) {
    switch (key) {
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    case Key::Space:
        return GLFW_KEY_SPACE;
    case Key::Left:
        return GLFW_KEY_LEFT;
    case Key::Right:
        return GLFW_KEY_RIGHT;
    case Key::Up:
        return GLFW_KEY_UP;
    case Key::Down:
        return GLFW_KEY_DOWN;
    case Key::W:
        return GLFW_KEY_W;
    case Key::A:
        return GLFW_KEY_A;
    case Key::S:
        return GLFW_KEY_S;
    case Key::D:
        return GLFW_KEY_D;
    }
    return GLFW_KEY_UNKNOWN;
}
} // namespace

Result<std::shared_ptr<Window>> Window::create(uint32_t width, uint32_t height, const std::string &title,
                                               bool visible) {
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
        return Result<std::shared_ptr<Window>>::failure(std::string("Window creation failed: ") +
                                                        (message ? message : "unknown GLFW error"));
    }
    ++windowCount;
    return Result<std::shared_ptr<Window>>::success(std::shared_ptr<Window>(new Window(handle)));
}

Window::~Window() {
    std::lock_guard<std::mutex> lock(glfwMutex);
    if (handle_)
        glfwDestroyWindow(static_cast<GLFWwindow *>(handle_));
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

bool Window::keyDown(Key key) const {
    return glfwGetKey(static_cast<GLFWwindow *>(handle_), glfwKey(key)) == GLFW_PRESS;
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
