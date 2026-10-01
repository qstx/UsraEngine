#pragma once
#include <Ursa/Result.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace ursa {
namespace detail {
struct WindowAccess;
}
enum class Key { Escape, Space, Left, Right, Up, Down, W, A, S, D };

class Window final {
public:
    static Result<std::shared_ptr<Window>> create(uint32_t width, uint32_t height, const std::string &title,
                                                  bool visible = true);

    ~Window();

    Window(const Window &) = delete;

    Window &operator=(const Window &) = delete;

    void pollEvents() const;

    void waitEvents() const;

    bool shouldClose() const;

    void requestClose();

    bool keyDown(Key key) const;

    void resize(uint32_t width, uint32_t height);

    void minimize();

    void restore();

    uint32_t framebufferWidth() const;

    uint32_t framebufferHeight() const;

private:
    explicit Window(void *handle) : handle_(handle) {
    }
    friend struct detail::WindowAccess;
    void *handle_{};
};

namespace detail {
struct WindowAccess {
    static void *native(const Window &window) {
        return window.handle_;
    }
};
} // namespace detail
} // namespace ursa
