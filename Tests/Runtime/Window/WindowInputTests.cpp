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
	const auto workAreaBeforeWindowCreation = Ursa::Window::PrimaryMonitorWorkArea();
	Expect(!workAreaBeforeWindowCreation ||
	           (workAreaBeforeWindowCreation.value.width > 0 && workAreaBeforeWindowCreation.value.height > 0),
	       "Primary monitor work area is invalid before creating a Window");
	auto created = Ursa::Window::Create(320, 240, "Window input test", false);
	Expect(static_cast<bool>(created), "Cannot create test window");
	auto window = std::move(created.value);
	Expect(window->Width() == 320 && window->Height() == 240, "Logical window size is incorrect");
	const auto monitors = Ursa::Window::Monitors();
	Expect(monitors && !monitors.value.empty(), "Window system did not enumerate a connected monitor");
	for (const auto& monitor : monitors.value)
	{
		Expect(monitor.bounds.width > 0 && monitor.bounds.height > 0 && monitor.workArea.width > 0 &&
		           monitor.workArea.height > 0 && monitor.scaleX > 0.0f && monitor.scaleY > 0.0f,
		       "Monitor bounds, work area, or content scale are invalid");
	}
	Ursa::WindowCreateDesc detachedDesc;
	detachedDesc.width = 240;
	detachedDesc.height = 180;
	detachedDesc.title = "Detached viewport test";
	detachedDesc.visible = false;
	detachedDesc.decorated = false;
	detachedDesc.floating = true;
	auto detachedCreated = Ursa::Window::Create(detachedDesc);
	Expect(static_cast<bool>(detachedCreated), "Cannot create a configured detached window");
	auto detached = std::move(detachedCreated.value);
	Expect(detached->Width() == 240 && detached->Height() == 180,
	       "Configured detached window has the wrong logical size");
	detached->SetPosition(-320, 48);
	const auto position = detached->Position();
	const auto localCursor = detached->CursorPosition();
	const auto desktopCursor = detached->DesktopCursorPosition();
	Expect(position.first < 0 && desktopCursor.first == localCursor.first + position.first &&
	           desktopCursor.second == localCursor.second + position.second,
	       "Desktop cursor coordinates were not translated from the owning window");
	const auto scale = detached->ContentScale();
	Expect(scale.first > 0.0f && scale.second > 0.0f, "Window content scale is invalid");
	const auto workArea = Ursa::Window::PrimaryMonitorWorkArea();
	Expect(!workArea || (workArea.value.width > 0 && workArea.value.height > 0),
	       "Primary monitor work area is invalid");
	Expect(!window->SetIcon(0, 1, {}), "Empty window icon must be rejected");
	Expect(!window->SetIcon(2, 2, {255, 255, 255, 255}), "Incomplete icon pixels must be rejected");
	Expect(!window->SetIcon(UINT32_MAX, UINT32_MAX, {}), "Oversized icon dimensions must be rejected");
#if defined(_WIN32) || defined(__linux__)
	Expect(static_cast<bool>(window->SetIcon(1, 1, {255, 255, 255, 255})), "Cannot set window icon");
#endif

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

	int firstWindowFocusEvents = 0;
	int secondWindowFocusEvents = 0;
	auto firstWindowFocus = window->Subscribe(
	    [&](const Ursa::WindowEvent& event)
	    {
		    if (event.type == Ursa::WindowEventType::Focused && !event.focused)
		    {
			    ++firstWindowFocusEvents;
		    }
	    });
	auto secondWindowFocus = detached->Subscribe(
	    [&](const Ursa::WindowEvent& event)
	    {
		    if (event.type == Ursa::WindowEventType::Focused && !event.focused)
		    {
			    ++secondWindowFocusEvents;
		    }
	    });
	Expect(firstWindowFocus && secondWindowFocus, "Cannot subscribe to both window focus streams");
	int detachedSizeEvents = 0;
	int mainSizeEvents = 0;
	Ursa::Float64 detachedReportedWidth = 0.0;
	Ursa::Float64 detachedReportedHeight = 0.0;
	auto firstWindowSize = window->Subscribe(
	    [&](const Ursa::WindowEvent& event)
	    {
		    if (event.type == Ursa::WindowEventType::SizeChanged)
		    {
			    ++mainSizeEvents;
		    }
	    });
	auto secondWindowSize = detached->Subscribe(
	    [&](const Ursa::WindowEvent& event)
	    {
		    if (event.type == Ursa::WindowEventType::SizeChanged)
		    {
			    ++detachedSizeEvents;
			    detachedReportedWidth = event.x;
			    detachedReportedHeight = event.y;
		    }
	    });
	Expect(firstWindowSize && secondWindowSize, "Cannot subscribe to window resize streams");
	detached->SetPosition(-120, 45);
	detached->Resize(250, 190);
	window->PollEvents();
	Expect(detachedSizeEvents > 0 && detachedReportedWidth == 250 && detachedReportedHeight == 190 &&
	           mainSizeEvents == 0,
	       "Detached-window resize was not routed to its own event stream");
	Ursa::WindowEvent focusLost;
	focusLost.type = Ursa::WindowEventType::Focused;
	Ursa::Detail::WindowAccess::Dispatch(*detached, focusLost);
	Expect(firstWindowFocusEvents == 0 && secondWindowFocusEvents == 1,
	       "Detached-window focus loss was delivered to the wrong window subscriber");
	Ursa::Detail::WindowAccess::Dispatch(*window, focusLost);
	Expect(firstWindowFocusEvents == 1 && secondWindowFocusEvents == 1,
	       "Main-window focus loss was not isolated from the detached window");

	window->Resize(400, 300);
	window->PollEvents();
	Expect(window->Width() == 400 && window->Height() == 300, "Logical window size did not update after resize");
	window->Minimize();
	window->PollEvents();
	window->Restore();
	window->PollEvents();
	Expect(!window->ShouldClose(), "Resize or minimize closed the window");
	return 0;
}
