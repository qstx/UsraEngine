#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace ursa {
namespace detail {
struct WindowAccess;
}
enum class Key {
    Escape, Space, Left, Right, Up, Down, W, A, S, D, Unknown, Tab, Enter, Backspace, Delete, Home, End,
    PageUp, PageDown, Insert, LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt, LeftSuper,
    RightSuper, B, C, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, T, U, V, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9
};

enum class MouseButton { Left, Right, Middle, Other };

enum class WindowEventType {
    KeyPressed, KeyReleased, KeyRepeated, Character, CursorMoved, MousePressed, MouseReleased, Scrolled, Focused
};

struct WindowEvent {
    WindowEventType type = WindowEventType::CursorMoved;
    Key key = Key::Unknown;
    MouseButton button = MouseButton::Other;
    uint32_t codepoint = 0;
    double x = 0.0;
    double y = 0.0;
    bool focused = false;
};

using WindowEventCallback = std::function<void(const WindowEvent &)>;

class Window final {
public:
    static Result<std::shared_ptr<Window>> create(uint32_t width, uint32_t height, const String &title,
                                                  bool visible = true);

    ~Window();

    Window(const Window &) = delete;

    Window &operator=(const Window &) = delete;

    void pollEvents() const;

    void waitEvents() const;

    bool shouldClose() const;

    void requestClose();

    void cancelClose();

    void setTitle(const String &title);

    String title() const;

    String clipboardText() const;

    void setClipboardText(const String &text);

    bool keyDown(Key key) const;

    bool mouseDown(MouseButton button) const;

    std::pair<double, double> cursorPosition() const;

    Result<uint64_t> subscribe(WindowEventCallback callback);

    void unsubscribe(uint64_t subscription);

    void resize(uint32_t width, uint32_t height);

    void minimize();

    void restore();

    uint32_t framebufferWidth() const;

    uint32_t framebufferHeight() const;

private:
    explicit Window(void *handle) : handle_(handle) {
    }
    friend struct detail::WindowAccess;

    void dispatchEvent(const WindowEvent &event);

    void *handle_{};
    uint64_t nextSubscription_ = 1;
    Vector<std::pair<uint64_t, WindowEventCallback>> subscriptions_;
};

namespace detail {
struct WindowAccess {
    static void *native(const Window &window) {
        return window.handle_;
    }

    static void dispatch(Window &window, const WindowEvent &event) {
        window.dispatchEvent(event);
    }
};
} // namespace detail
} // namespace ursa
