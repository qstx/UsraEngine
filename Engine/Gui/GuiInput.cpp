#include <Gui/GuiInput.hpp>
#include <cmath>

namespace Ursa::Gui
{
namespace
{
String Encode(UInt32 scalar)
{
	String result;
	if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) || scalar == 0)
	{
		return result;
	}
	if (scalar < 0x80)
	{
		result.push_back(static_cast<char>(scalar));
	}
	else if (scalar < 0x800)
	{
		result.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
		result.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
	}
	else if (scalar < 0x10000)
	{
		result.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
		result.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
		result.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
	}
	else
	{
		result.push_back(static_cast<char>(0xf0 | (scalar >> 18)));
		result.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3f)));
		result.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
		result.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
	}
	return result;
}
} // namespace

InputQueue::~InputQueue()
{
	Detach();
}

Result<void> InputQueue::Attach(std::shared_ptr<Window> value)
{
	Detach();
	if (!value)
	{
		return Result<void>::Failure("GUI input requires a Window");
	}
	window = std::move(value);
	const auto cursor = window->CursorPosition();
	pointer = {static_cast<Float32>(cursor.first), static_cast<Float32>(cursor.second)};
	for (Size i = 0; i < keys.size(); ++i)
	{
		keys[i] = static_cast<Key>(i) != Key::Unknown && window->KeyDown(static_cast<Key>(i));
	}
	auto result = window->Subscribe([this](const WindowEvent& event) { Push(event); });
	if (!result)
	{
		window.reset();
		return Result<void>::Failure(result.error);
	}
	subscription = result.value;
	return {};
}

void InputQueue::Detach()
{
	if (window && subscription)
	{
		window->Unsubscribe(subscription);
	}
	window.reset();
	subscription = 0;
	Cancel();
}

void InputQueue::Push(const WindowEvent& raw)
{
	if (raw.type == WindowEventType::Focused && !raw.focused)
	{
		Cancel();
		return;
	}
	if (raw.type == WindowEventType::Focused && raw.focused && window)
	{
		SynchronizeModifiers(*window);
	}
	Event event;
	event.mousePosition = pointer;
	const bool keyDown = raw.type == WindowEventType::KeyPressed || raw.type == WindowEventType::KeyRepeated;
	const Size keyIndex = static_cast<Size>(raw.key);
	if (keyIndex < keys.size() && (keyDown || raw.type == WindowEventType::KeyReleased))
	{
		keys[keyIndex] = keyDown;
	}
	const auto down = [&](Key key) { return keys[static_cast<Size>(key)]; };
	if (down(Key::LeftShift) || down(Key::RightShift))
	{
		event.modifiers |= static_cast<UInt32>(Modifiers::Shift);
	}
	if (down(Key::LeftControl) || down(Key::RightControl))
	{
		event.modifiers |= static_cast<UInt32>(Modifiers::Control);
	}
	if (down(Key::LeftAlt) || down(Key::RightAlt))
	{
		event.modifiers |= static_cast<UInt32>(Modifiers::Alt);
	}
	if (down(Key::LeftSuper) || down(Key::RightSuper))
	{
		event.modifiers |= static_cast<UInt32>(Modifiers::Command);
	}
	switch (raw.type)
	{
		case WindowEventType::KeyPressed:
		case WindowEventType::KeyRepeated:
		case WindowEventType::KeyReleased:
			event.type = keyDown ? EventType::KeyDown : EventType::KeyUp;
			event.key = raw.key;
#ifdef __APPLE__
			if (keyDown && (event.modifiers & static_cast<UInt32>(Modifiers::Command)))
#else
			if (keyDown && (event.modifiers & static_cast<UInt32>(Modifiers::Control)))
#endif
			{
				if (raw.key == Key::C)
				{
					event.commandName = "Copy";
				}
				else if (raw.key == Key::X)
				{
					event.commandName = "Cut";
				}
				else if (raw.key == Key::V)
				{
					event.commandName = "Paste";
				}
				else if (raw.key == Key::A)
				{
					event.commandName = "SelectAll";
				}
			}
			break;
		case WindowEventType::Character:
			event.text = Encode(raw.codepoint);
			if (event.text.empty())
			{
				return;
			}
			event.type = EventType::KeyDown;
			break;
		case WindowEventType::CursorMoved:
			event.delta = {static_cast<Float32>(raw.x) - pointer.x, static_cast<Float32>(raw.y) - pointer.y};
			pointer = {static_cast<Float32>(raw.x), static_cast<Float32>(raw.y)};
			event.mousePosition = pointer;
			event.type = EventType::MouseMove;
			for (Size i = 0; i < buttons.size(); ++i)
			{
				if (buttons[i])
				{
					event.type = EventType::MouseDrag;
					event.button = static_cast<Int32>(i);
					break;
				}
			}
			break;
		case WindowEventType::MousePressed:
		case WindowEventType::MouseReleased:
			event.type = raw.type == WindowEventType::MousePressed ? EventType::MouseDown : EventType::MouseUp;
			event.button = static_cast<Int32>(raw.button);
			if (event.type == EventType::MouseDown)
			{
				const auto now = std::chrono::steady_clock::now();
				const bool repeat = lastClickButton == event.button &&
				                    std::chrono::duration<double>(now - lastClickTime).count() <= 0.4 &&
				                    std::abs(pointer.x - lastClickPosition.x) <= 6 &&
				                    std::abs(pointer.y - lastClickPosition.y) <= 6;
				clickCount = repeat ? clickCount + 1 : 1;
				lastClickTime = now;
				lastClickPosition = pointer;
				lastClickButton = event.button;
			}
			event.clickCount = clickCount;
			if (event.button >= 0 && event.button < 4)
			{
				buttons[static_cast<Size>(event.button)] = event.type == EventType::MouseDown;
			}
			break;
		case WindowEventType::Scrolled:
			event.type = EventType::ScrollWheel;
			event.delta = {static_cast<Float32>(raw.x), static_cast<Float32>(raw.y)};
			break;
		default:
			return;
	}
	event.rawType = event.type;
	events.push_back(std::move(event));
}

void InputQueue::Push(const WindowEvent& event, Point position)
{
	if (event.type != WindowEventType::CursorMoved)
	{
		pointer = position;
	}
	Push(event);
}

Vector<Event> InputQueue::Drain()
{
	Vector<Event> result;
	result.swap(events);
	return result;
}

void InputQueue::Cancel()
{
	events.clear();
	keys = {};
	buttons = {};
	lastClickButton = -1;
	clickCount = 0;
	if (window)
	{
		SynchronizeModifiers(*window);
	}
	focusLost = true;
}

void InputQueue::SynchronizeModifiers(const Window& source)
{
	for (Key key : {Key::LeftShift, Key::RightShift, Key::LeftControl, Key::RightControl, Key::LeftAlt, Key::RightAlt,
	                Key::LeftSuper, Key::RightSuper})
	{
		keys[static_cast<Size>(key)] = source.KeyDown(key);
	}
}

bool InputQueue::TakeFocusLost()
{
	const bool result = focusLost;
	focusLost = false;
	return result;
}
} // namespace Ursa::Gui
