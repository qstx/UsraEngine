#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
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
	escape,
	space,
	left,
	right,
	up,
	down,
	w,
	a,
	s,
	d,
	unknown,
	tab,
	enter,
	backspace,
	Delete,
	home,
	end,
	pageUp,
	pageDown,
	insert,
	leftShift,
	rightShift,
	leftControl,
	rightControl,
	leftAlt,
	rightAlt,
	leftSuper,
	rightSuper,
	b,
	c,
	e,
	f,
	g,
	h,
	i,
	j,
	k,
	l,
	m,
	n,
	o,
	p,
	q,
	r,
	t,
	u,
	v,
	x,
	y,
	z,
	digit0,
	digit1,
	digit2,
	digit3,
	digit4,
	digit5,
	digit6,
	digit7,
	digit8,
	digit9
};

enum class MouseButton
{
	left,
	right,
	middle,
	other
};

enum class WindowEventType
{
	keyPressed,
	keyReleased,
	keyRepeated,
	character,
	cursorMoved,
	mousePressed,
	mouseReleased,
	scrolled,
	focused
};

struct WindowEvent
{
	WindowEventType type = WindowEventType::cursorMoved;
	Key key = Key::unknown;
	MouseButton button = MouseButton::other;
	uint32_t codepoint = 0;
	double x = 0.0;
	double y = 0.0;
	bool focused = false;
};

using WindowEventCallback = std::function<void(const WindowEvent&)>;

class Window final
{
public:
	static Result<std::shared_ptr<Window>> Create(uint32_t width, uint32_t height, const String& title,
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

	String ClipboardText() const;

	void SetClipboardText(const String& text);

	bool KeyDown(Key key) const;

	bool MouseDown(MouseButton button) const;

	std::pair<double, double> CursorPosition() const;

	Result<uint64_t> Subscribe(WindowEventCallback callback);

	void Unsubscribe(uint64_t subscription);

	void Resize(uint32_t width, uint32_t height);

	void Minimize();

	void Restore();

	uint32_t FramebufferWidth() const;

	uint32_t FramebufferHeight() const;

private:
	explicit Window(void* handle) : handle(handle)
	{
	}
	friend struct Detail::WindowAccess;

	void DispatchEvent(const WindowEvent& event);

	void* handle{};
	uint64_t nextSubscription = 1;
	Vector<std::pair<uint64_t, WindowEventCallback>> subscriptions;
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
