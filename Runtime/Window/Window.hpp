#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace Ursa
{
namespace Detail
{
struct WindowAccess;
}
enum class Key
{
	Escape,
	Space,
	Left,
	Right,
	Up,
	Down,
	W,
	A,
	S,
	D,
	Unknown,
	Tab,
	Enter,
	Backspace,
	Delete,
	Home,
	End,
	PageUp,
	PageDown,
	Insert,
	LeftShift,
	RightShift,
	LeftControl,
	RightControl,
	LeftAlt,
	RightAlt,
	LeftSuper,
	RightSuper,
	B,
	C,
	E,
	F,
	G,
	H,
	I,
	J,
	K,
	L,
	M,
	N,
	O,
	P,
	Q,
	R,
	T,
	U,
	V,
	X,
	Y,
	Z,
	Digit0,
	Digit1,
	Digit2,
	Digit3,
	Digit4,
	Digit5,
	Digit6,
	Digit7,
	Digit8,
	Digit9
};

enum class MouseButton
{
	Left,
	Right,
	Middle,
	Other
};

enum class WindowEventType
{
	KeyPressed,
	KeyReleased,
	KeyRepeated,
	Character,
	CursorMoved,
	MousePressed,
	MouseReleased,
	Scrolled,
	Focused
};

struct WindowEvent
{
	WindowEventType type = WindowEventType::CursorMoved;
	Key key = Key::Unknown;
	MouseButton button = MouseButton::Other;
	Ursa::UInt32 codepoint = 0;
	Ursa::Float64 x = 0.0;
	Ursa::Float64 y = 0.0;
	bool focused = false;
};

using WindowEventCallback = std::function<void(const WindowEvent&)>;

class Window final
{
public:
	static Result<std::shared_ptr<Window>> Create(Ursa::UInt32 width, Ursa::UInt32 height, const String& title,
	                                              bool visible = true);

	~Window();

	Window(const Window&) = delete;

	Window& operator=(const Window&) = delete;

	void PollEvents() const;

	void WaitEvents() const;

	bool ShouldClose() const;

	void RequestClose();

	void CancelClose();

	void SetTitle(const String& title);

	String Title() const;

	// RGBA8 pixels, ordered from the top-left corner.
	Result<void> SetIcon(Ursa::UInt32 width, Ursa::UInt32 height, const Vector<Ursa::UInt8>& pixels);

	String ClipboardText() const;

	void SetClipboardText(const String& text);

	bool KeyDown(Key key) const;

	bool MouseDown(MouseButton button) const;

	std::pair<Ursa::Float64, Ursa::Float64> CursorPosition() const;

	Result<Ursa::UInt64> Subscribe(WindowEventCallback callback);

	void Unsubscribe(Ursa::UInt64 subscription);

	void Resize(Ursa::UInt32 width, Ursa::UInt32 height);

	void Show();

	void Minimize();

	bool Minimized() const;

	void Restore();

	Ursa::UInt32 Width() const;

	Ursa::UInt32 Height() const;

	Ursa::UInt32 FramebufferWidth() const;

	Ursa::UInt32 FramebufferHeight() const;

private:
	explicit Window(void* nativeHandle) : handle(nativeHandle)
	{
	}
	friend struct Detail::WindowAccess;

	void DispatchEvent(const WindowEvent& event);

	void* handle{};
	Ursa::UInt64 nextSubscription = 1;
	Vector<std::pair<Ursa::UInt64, WindowEventCallback>> subscriptions;
};

namespace Detail
{
struct WindowAccess
{
	static void* Native(const Window& window)
	{
		return window.handle;
	}

	static void Dispatch(Window& window, const WindowEvent& event)
	{
		window.DispatchEvent(event);
	}
};
} // namespace Detail
} // namespace Ursa
