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
	Focused,
	PositionChanged,
	SizeChanged,
	FramebufferSizeChanged,
	ContentScaleChanged
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

struct WindowCreateDesc
{
	Ursa::UInt32 width = 1;
	Ursa::UInt32 height = 1;
	String title;
	bool visible = true;
	bool decorated = true;
	bool floating = false;
	bool focused = true;
	bool focusOnShow = true;
};

struct WindowWorkArea
{
	Ursa::Int32 x = 0;
	Ursa::Int32 y = 0;
	Ursa::UInt32 width = 0;
	Ursa::UInt32 height = 0;
};

struct WindowMonitorInfo
{
	WindowWorkArea bounds;
	WindowWorkArea workArea;
	Ursa::Float32 scaleX = 1.0f;
	Ursa::Float32 scaleY = 1.0f;
};

class Window final
{
public:
	static Result<std::shared_ptr<Window>> Create(Ursa::UInt32 width, Ursa::UInt32 height, const String& title,
	                                              bool visible = true);

	static Result<std::shared_ptr<Window>> Create(const WindowCreateDesc& desc);

	static Result<WindowWorkArea> PrimaryMonitorWorkArea();

	static Result<Vector<WindowMonitorInfo>> Monitors();

	~Window();

	Window(const Window&) = delete;

	Window& operator=(const Window&) = delete;

	void PollEvents() const;

	void WaitEvents() const;
	void WaitEventsTimeout(Float64 timeoutSeconds) const;

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

	void SetCursorCaptured(bool captured);

	bool CursorCaptured() const
	{
		return cursorCaptured;
	}

	std::pair<Ursa::Float64, Ursa::Float64> DesktopCursorPosition() const;

	std::pair<Ursa::Int32, Ursa::Int32> Position() const;

	void SetPosition(Ursa::Int32 x, Ursa::Int32 y);

	std::pair<Ursa::Float32, Ursa::Float32> ContentScale() const;

	bool Focused() const;

	void Focus();

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
	bool cursorCaptured = false;
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
