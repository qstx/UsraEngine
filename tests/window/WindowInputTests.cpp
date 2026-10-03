#include <window/Window.hpp>
#include <cstdlib>
#include <iostream>

namespace {
void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    auto created = ursa::Window::create(320, 240, "Window input test", false);
    expect(static_cast<bool>(created), "Cannot create test window");
    auto window = std::move(created.value);

    auto empty = window->subscribe({});
    expect(!empty, "Empty callback must be rejected");
    expect(!window->keyDown(ursa::Key::Unknown), "Unknown key must not be queried from GLFW");
    expect(!window->mouseDown(ursa::MouseButton::Other), "Other mouse button must not be queried from GLFW");

    int firstCalls = 0;
    int secondCalls = 0;
    uint64_t firstId = 0;
    auto first = window->subscribe([&](const ursa::WindowEvent &event) {
        ++firstCalls;
        expect(event.type == ursa::WindowEventType::Character && event.codepoint == 0x4E2D,
               "Character payload was lost");
        window->unsubscribe(firstId);
    });
    expect(static_cast<bool>(first), "Cannot subscribe first callback");
    firstId = first.value;
    auto second = window->subscribe([&](const ursa::WindowEvent &) { ++secondCalls; });
    expect(static_cast<bool>(second), "Cannot subscribe second callback");

    ursa::WindowEvent character;
    character.type = ursa::WindowEventType::Character;
    character.codepoint = 0x4E2D;
    ursa::detail::WindowAccess::dispatch(*window, character);
    expect(firstCalls == 1 && secondCalls == 1, "Unsubscribe during dispatch skipped another callback");
    ursa::detail::WindowAccess::dispatch(*window, character);
    expect(firstCalls == 1 && secondCalls == 2, "Unsubscribed callback was called again");
    window->unsubscribe(second.value);

    int mouseCalls = 0;
    auto mouse = window->subscribe([&](const ursa::WindowEvent &event) {
        if (event.type == ursa::WindowEventType::MousePressed && event.button == ursa::MouseButton::Left)
            ++mouseCalls;
    });
    expect(static_cast<bool>(mouse), "Cannot subscribe mouse callback");
    ursa::WindowEvent pressed;
    pressed.type = ursa::WindowEventType::MousePressed;
    pressed.button = ursa::MouseButton::Left;
    ursa::detail::WindowAccess::dispatch(*window, pressed);
    expect(mouseCalls == 1, "Mouse button event was not delivered");

    window->resize(400, 300);
    window->pollEvents();
    window->minimize();
    window->pollEvents();
    window->restore();
    window->pollEvents();
    expect(!window->shouldClose(), "Resize or minimize closed the window");
    return 0;
}
