#include <Core/Types.hpp>
#include <Window/Window.hpp>
#include <cstdlib>
#include <iostream>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}
}
} // namespace

int main()
{
	auto created = Ursa::Window::Create(320, 240, "Window input test", false);
	Expect(static_cast<bool>(created), "Cannot create test window");
	auto window = std::move(created.value);

	auto empty = window->Subscribe({});
	Expect(!empty, "Empty callback must be rejected");
	Expect(!window->KeyDown(Ursa::Key::Unknown), "Unknown key must not be queried from GLFW");
	Expect(!window->MouseDown(Ursa::MouseButton::Other), "Other mouse button must not be queried from GLFW");

	int firstCalls = 0;
	int secondCalls = 0;
	Ursa::UInt64 firstId = 0;
	auto first = window->Subscribe(
	    [&](const Ursa::WindowEvent& event)
	    {
		    ++firstCalls;
		    Expect(event.type == Ursa::WindowEventType::Character && event.codepoint == 0x4E2D,
		           "Character payload was lost");
		    window->Unsubscribe(firstId);
	    });
	Expect(static_cast<bool>(first), "Cannot subscribe first callback");
	firstId = first.value;
	auto second = window->Subscribe([&](const Ursa::WindowEvent&) { ++secondCalls; });
	Expect(static_cast<bool>(second), "Cannot subscribe second callback");

	Ursa::WindowEvent character;
	character.type = Ursa::WindowEventType::Character;
	character.codepoint = 0x4E2D;
	Ursa::Detail::WindowAccess::Dispatch(*window, character);
	Expect(firstCalls == 1 && secondCalls == 1, "Unsubscribe during dispatch skipped another callback");
	Ursa::Detail::WindowAccess::Dispatch(*window, character);
	Expect(firstCalls == 1 && secondCalls == 2, "Unsubscribed callback was called again");
	window->Unsubscribe(second.value);

	int mouseCalls = 0;
	auto mouse = window->Subscribe(
	    [&](const Ursa::WindowEvent& event)
	    {
		    if (event.type == Ursa::WindowEventType::MousePressed && event.button == Ursa::MouseButton::Left)
		    {
			    ++mouseCalls;
		    }
	    });
	Expect(static_cast<bool>(mouse), "Cannot subscribe mouse callback");
	Ursa::WindowEvent pressed;
	pressed.type = Ursa::WindowEventType::MousePressed;
	pressed.button = Ursa::MouseButton::Left;
	Ursa::Detail::WindowAccess::Dispatch(*window, pressed);
	Expect(mouseCalls == 1, "Mouse button event was not delivered");

	window->Resize(400, 300);
	window->PollEvents();
	window->Minimize();
	window->PollEvents();
	window->Restore();
	window->PollEvents();
	Expect(!window->ShouldClose(), "Resize or minimize closed the window");
	return 0;
}
