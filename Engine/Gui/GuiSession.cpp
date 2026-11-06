#include <Gui/GuiSession.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Ursa::Gui
{
namespace
{
std::atomic<UInt64> nextCallback{1};

UInt64 Mix(UInt64 seed, UInt64 value)
{
	return (seed ^ value) * 1099511628211ull;
}

void Validate(Rect rect)
{
	if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
	    rect.width < 0 || rect.height < 0)
	{
		throw std::invalid_argument("GUI rectangle must be finite with nonnegative dimensions");
	}
}

Rect Intersect(Rect a, Rect b)
{
	const Float32 x = std::max(a.x, b.x);
	const Float32 y = std::max(a.y, b.y);
	return {x, y, std::max(0.f, std::min(a.x + a.width, b.x + b.width) - x),
	        std::max(0.f, std::min(a.y + a.height, b.y + b.height) - y)};
}

Size Previous(const String& text, Size cursor)
{
	if (cursor == 0)
	{
		return 0;
	}
	--cursor;
	while (cursor > 0 && (static_cast<UInt8>(text[cursor]) & 0xc0) == 0x80)
	{
		--cursor;
	}
	return cursor;
}

Size Next(const String& text, Size cursor)
{
	if (cursor >= text.size())
	{
		return text.size();
	}
	++cursor;
	while (cursor < text.size() && (static_cast<UInt8>(text[cursor]) & 0xc0) == 0x80)
	{
		++cursor;
	}
	return cursor;
}
} // namespace

bool Rect::Contains(Point point) const
{
	return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
}

void Session::Require(UInt64 token) const
{
	if (thread != std::this_thread::get_id() || !inComponent || !inEvent || (token && token != callback))
	{
		throw std::logic_error("GUI call requires the current owner-thread OnGUI callback; Event wrapper is stale or "
		                       "belongs to another session");
	}
	if (exited)
	{
		throw std::logic_error("GUI event has exited");
	}
}

void Session::BeginEvent(Event value, Point size)
{
	if (thread != std::this_thread::get_id() || inEvent || !std::isfinite(size.x) || !std::isfinite(size.y) ||
	    size.x <= 0 || size.y <= 0)
	{
		throw std::logic_error("GUI event requires an idle owner thread and a drawable viewport");
	}
	event = std::move(value);
	event.rawType = event.type;
	viewport = size;
	inEvent = true;
	exited = false;
	if (event.type == EventType::Layout)
	{
		seen.clear();
		seenComponents.clear();
		focusOrder.clear();
		diagnostics.clear();
	}
	if (event.type == EventType::Repaint)
	{
		commands.clear();
	}
	if (event.type == EventType::KeyDown && event.key == Key::Tab && event.text.empty())
	{
		FocusNext((event.modifiers & static_cast<UInt32>(Modifiers::Shift)) != 0);
		event.type = EventType::Used;
	}
}

void Session::EndEvent()
{
	if (thread != std::this_thread::get_id() || !inEvent || inComponent)
	{
		throw std::logic_error("GUI event ended with an active or invalid component scope");
	}
	if (event.rawType == EventType::Layout && !exited)
	{
		for (auto it = layout.begin(); it != layout.end();)
		{
			if (!seenComponents.count(it->first))
			{
				it = layout.erase(it);
			}
			else
			{
				++it;
			}
		}
		for (auto it = controls.begin(); it != controls.end();)
		{
			if (!seen.count(it->first))
			{
				if (hot == it->first)
				{
					hot = 0;
				}
				if (keyboard == it->first)
				{
					keyboard = 0;
				}
				identities.erase(it->second.identity);
				textStates.erase(it->first);
				it = controls.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
	if (exited && (event.rawType == EventType::Layout || event.rawType == EventType::Repaint))
	{
		commands.clear();
	}
	inEvent = false;
}

UInt64 Session::BeginComponent(UInt64 value)
{
	if (thread != std::this_thread::get_id() || !inEvent || inComponent || exited || value == 0)
	{
		throw std::logic_error("Invalid or nested GUI component callback");
	}
	inComponent = true;
	component = value;
	callback = nextCallback.fetch_add(1);
	ordinal = 0;
	callIndex = 0;
	changed = false;
	checkpoint = commands.size();
	ids.clear();
	ordinals.clear();
	groups = {{{0, 0}, {0, 0, viewport.x, viewport.y}}};
	if (event.rawType == EventType::Layout)
	{
		seenComponents.insert(component);
		layout[component].clear();
	}
	return callback;
}

void Session::EndComponent(bool failed)
{
	if (thread != std::this_thread::get_id() || !inComponent)
	{
		throw std::logic_error("Invalid GUI component end");
	}
	const bool unbalanced = !exited && (!ids.empty() || groups.size() != 1);
	const bool mismatched =
	    !exited && !failed && event.rawType != EventType::Layout && callIndex != layout[component].size();
	if (failed || unbalanced || mismatched)
	{
		commands.resize(checkpoint);
	}
	inComponent = false;
	groups.clear();
	ids.clear();
	ordinals.clear();
	if (unbalanced)
	{
		throw std::logic_error("OnGUI ended with unmatched group or ID scopes");
	}
	if (mismatched)
	{
		throw std::logic_error(
		    "OnGUI controls differ from the associated Layout; call GUIUtility.ExitGUI after changing structure");
	}
}

UInt64 Session::CallbackToken() const
{
	Require();
	return callback;
}

const Event& Session::Current(UInt64 token) const
{
	Require(token);
	return event;
}

void Session::Use(UInt64 token)
{
	Require(token);
	if (event.type == EventType::Layout || event.type == EventType::Repaint)
	{
		diagnostics.push_back("Event.Use cannot consume Layout or Repaint");
		return;
	}
	event.type = EventType::Used;
}

void Session::RequireId(Int32 id) const
{
	Require();
	const auto found = controls.find(id);
	if (id <= 0 || found == controls.end() || found->second.component != component || !seen.count(id))
	{
		throw std::invalid_argument("GUI control ID is stale or belongs to another component");
	}
}

EventType Session::GetTypeForControl(Int32 id, UInt64 token) const
{
	Require(token);
	RequireId(id);
	if ((event.type == EventType::MouseDown || event.type == EventType::MouseUp || event.type == EventType::MouseDrag ||
	     event.type == EventType::MouseMove || event.type == EventType::ScrollWheel) &&
	    hot != 0 && hot != id)
	{
		return EventType::Ignore;
	}
	if ((event.type == EventType::KeyDown || event.type == EventType::KeyUp ||
	     event.type == EventType::ValidateCommand || event.type == EventType::ExecuteCommand) &&
	    keyboard != id)
	{
		return EventType::Ignore;
	}
	return event.type;
}

Int32 Session::GetControlId(Int32 hint, FocusType focus, Rect rect)
{
	Require();
	Validate(rect);
	if (focus != FocusType::Passive && focus != FocusType::Keyboard)
	{
		throw std::invalid_argument("Invalid GUI FocusType");
	}
	UInt64 identity = Mix(1469598103934665603ull, component);
	for (Int32 id : ids)
	{
		identity = Mix(identity, static_cast<UInt32>(id));
	}
	identity = Mix(Mix(Mix(identity, ordinal++), static_cast<UInt32>(hint)), static_cast<UInt32>(focus));
	Int32 id = 0;
	if (event.rawType == EventType::Layout)
	{
		auto found = identities.find(identity);
		if (found == identities.end())
		{
			if (nextId == std::numeric_limits<Int32>::max())
			{
				throw std::overflow_error("GUI control ID space exhausted");
			}
			id = nextId++;
			identities.emplace(identity, id);
			controls.emplace(id, Control{component, identity, focus});
		}
		else
		{
			id = found->second;
		}
		layout[component].push_back(id);
		seen.insert(id);
		if (focus == FocusType::Keyboard)
		{
			focusOrder.push_back(id);
		}
	}
	else
	{
		const auto found = identities.find(identity);
		const auto& expected = layout[component];
		if (found == identities.end() || callIndex >= expected.size() || expected[callIndex] != found->second)
		{
			throw std::logic_error("OnGUI control identity does not match Layout");
		}
		id = found->second;
	}
	++callIndex;
	return id;
}

Int32 Session::HotControl() const
{
	Require();
	return hot;
}

void Session::SetHotControl(Int32 id)
{
	Require();
	if (id)
	{
		RequireId(id);
	}
	if (hot && controls.at(hot).component != component)
	{
		throw std::logic_error("Another component owns HotControl");
	}
	hot = id;
}

Int32 Session::KeyboardControl() const
{
	Require();
	return keyboard;
}

void Session::SetKeyboardControl(Int32 id)
{
	Require();
	if (id)
	{
		RequireId(id);
		if (controls.at(id).focus != FocusType::Keyboard)
		{
			throw std::invalid_argument("Passive GUI control cannot take keyboard focus");
		}
	}
	keyboard = id;
}

bool Session::Changed() const
{
	Require();
	return changed;
}

void Session::SetChanged(bool value)
{
	Require();
	changed = value;
}

void Session::Exit()
{
	Require();
	exited = true;
	if (event.rawType != EventType::Layout && event.rawType != EventType::Repaint)
	{
		event.type = EventType::Used;
	}
}

bool Session::Exited() const
{
	return exited;
}

Point Session::ViewportSize() const
{
	Require();
	return viewport;
}

Point Session::MousePosition() const
{
	Require();
	return {event.mousePosition.x - groups.back().origin.x, event.mousePosition.y - groups.back().origin.y};
}

void Session::PushId(Int32 id)
{
	Require();
	ids.push_back(id);
	ordinals.push_back(ordinal);
	ordinal = 0;
}

void Session::PopId()
{
	Require();
	if (ids.empty())
	{
		throw std::logic_error("GUI.PopId has no matching PushId");
	}
	ids.pop_back();
	ordinal = ordinals.back();
	ordinals.pop_back();
}

Rect Session::Place(Rect rect) const
{
	Validate(rect);
	return {rect.x + groups.back().origin.x, rect.y + groups.back().origin.y, rect.width, rect.height};
}

bool Session::Hit(Rect rect) const
{
	return Intersect(Place(rect), groups.back().clip).Contains(event.mousePosition);
}

void Session::BeginGroup(Rect rect)
{
	Require();
	const Rect placed = Place(rect);
	groups.push_back({{placed.x, placed.y}, Intersect(placed, groups.back().clip)});
}

void Session::EndGroup()
{
	Require();
	if (groups.size() <= 1)
	{
		throw std::logic_error("GUI.EndGroup has no matching BeginGroup");
	}
	groups.pop_back();
}

void Session::Draw(DrawType type, Rect rect, UInt32 color, const String& text, MaterialTexturePtr texture)
{
	const Rect placed = Place(rect);
	if (event.rawType == EventType::Repaint)
	{
		commands.push_back({type, placed, groups.back().clip, color, text, texture});
	}
}

void Session::Label(Rect rect, const String& text)
{
	GetControlId(1, FocusType::Passive, rect);
	Draw(DrawType::Text, rect, 0xffffffff, text);
}

void Session::DrawTexture(Rect rect, MaterialTexturePtr texture)
{
	GetControlId(2, FocusType::Passive, rect);
	if (!texture)
	{
		throw std::invalid_argument("GUI.DrawTexture requires a valid texture");
	}
	Draw(DrawType::Texture, rect, 0xffffffff, {}, texture);
}

bool Session::Button(Rect rect, const String& text)
{
	const Int32 id = GetControlId(3, FocusType::Keyboard, rect);
	bool clicked = false;
	switch (GetTypeForControl(id))
	{
		case EventType::MouseDown:
			if (event.button == 0 && Hit(rect))
			{
				hot = id;
				keyboard = id;
				Use();
			}
			break;
		case EventType::MouseUp:
			if (hot == id && event.button == 0)
			{
				hot = 0;
				clicked = Hit(rect);
				Use();
			}
			break;
		case EventType::KeyDown:
			if (event.text.empty() && (event.key == Key::Enter || event.key == Key::Space))
			{
				clicked = true;
				Use();
			}
			break;
		default:
			break;
	}
	Draw(DrawType::Fill, rect, hot == id ? 0xff805030 : 0xff483c32);
	Draw(DrawType::Border, rect, keyboard == id ? 0xffffb55a : 0xff807060);
	Draw(DrawType::Text, rect, 0xffffffff, text);
	changed |= clicked;
	return clicked;
}

bool Session::Toggle(Rect rect, bool value, const String& text)
{
	const bool clicked = Button(rect, (value ? "[x] " : "[ ] ") + text);
	return clicked ? !value : value;
}

Float32 Session::HorizontalSlider(Rect rect, Float32 value, Float32 minimum, Float32 maximum)
{
	if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum ||
	    rect.width <= 0)
	{
		throw std::invalid_argument("GUI slider requires finite values, positive width and minimum < maximum");
	}
	const Int32 id = GetControlId(4, FocusType::Keyboard, rect);
	Float32 result = std::clamp(value, minimum, maximum);
	const EventType type = GetTypeForControl(id);
	if (type == EventType::MouseDown && event.button == 0 && Hit(rect))
	{
		hot = id;
		keyboard = id;
	}
	if (hot == id && ((type == EventType::MouseDown || type == EventType::MouseUp) && event.button == 0 ||
	                  type == EventType::MouseDrag))
	{
		result =
		    minimum + std::clamp((event.mousePosition.x - Place(rect).x) / rect.width, 0.f, 1.f) * (maximum - minimum);
		if (type == EventType::MouseUp)
		{
			hot = 0;
		}
		Use();
	}
	else if (type == EventType::KeyDown && event.text.empty() && (event.key == Key::Left || event.key == Key::Right))
	{
		result =
		    std::clamp(result + (event.key == Key::Right ? 1.f : -1.f) * (maximum - minimum) / 100.f, minimum, maximum);
		Use();
	}
	// Layout/Repaint do not report normalization as an edit.
	if (type == EventType::Layout || type == EventType::Repaint || type == EventType::Used || type == EventType::Ignore)
	{
		result = value;
	}
	changed |= result != value;
	Draw(DrawType::Fill, rect, 0xff483c32);
	const Float32 fraction = std::clamp((result - minimum) / (maximum - minimum), 0.f, 1.f);
	Draw(DrawType::Fill, {rect.x + fraction * std::max(0.f, rect.width - 8), rect.y, 8, rect.height}, 0xffffb55a);
	return result;
}

String Session::TextField(Rect rect, const String& value)
{
	if (value.size() > 1024 * 1024)
	{
		throw std::invalid_argument("GUI text exceeds one MiB");
	}
	const Int32 id = GetControlId(5, FocusType::Keyboard, rect);
	auto& state = textStates.try_emplace(id, TextState{value.size(), value.size()}).first->second;
	state.cursor = std::min(state.cursor, value.size());
	state.anchor = std::min(state.anchor, value.size());
	while (state.cursor > 0 && state.cursor < value.size() && (static_cast<UInt8>(value[state.cursor]) & 0xc0) == 0x80)
	{
		--state.cursor;
	}
	while (state.anchor > 0 && state.anchor < value.size() && (static_cast<UInt8>(value[state.anchor]) & 0xc0) == 0x80)
	{
		--state.anchor;
	}
	String result = value;
	const auto selection = [&]() { return std::minmax(state.cursor, state.anchor); };
	const auto replace = [&](const String& text)
	{
		const auto [start, end] = selection();
		if (result.size() - (end - start) + text.size() > 1024 * 1024)
		{
			throw std::invalid_argument("GUI text exceeds one MiB");
		}
		result.replace(start, end - start, text);
		state.cursor = state.anchor = start + text.size();
	};
	const EventType type = GetTypeForControl(id);
	const auto cursorAtPointer = [&]()
	{
		const Float32 position = event.mousePosition.x - Place(rect).x - 4.f + state.scroll;
		Size cursor = 0;
		Float32 previous = 0;
		while (cursor < result.size())
		{
			const Size next = Next(result, cursor);
			const Float32 end = Measure(result.substr(0, next));
			if (position < (previous + end) * 0.5f)
			{
				return cursor;
			}
			cursor = next;
			previous = end;
		}
		return result.size();
	};
	if (type == EventType::MouseDown && event.button == 0 && Hit(rect))
	{
		keyboard = hot = id;
		state.cursor = cursorAtPointer();
		if (!(event.modifiers & static_cast<UInt32>(Modifiers::Shift)))
		{
			state.anchor = state.cursor;
		}
		if (event.clickCount >= 2)
		{
			state.anchor = 0;
			state.cursor = result.size();
		}
		Use();
	}
	else if (type == EventType::MouseDrag && hot == id)
	{
		state.cursor = cursorAtPointer();
		Use();
	}
	else if (type == EventType::MouseUp && event.button == 0 && hot == id)
	{
		hot = 0;
		Use();
	}
	else if (type == EventType::KeyDown)
	{
		bool handled = true;
		if (!event.text.empty())
		{
			replace(event.text);
		}
		else if (event.key == Key::Backspace)
		{
			if (state.cursor == state.anchor)
			{
				state.anchor = Previous(result, state.cursor);
			}
			replace({});
		}
		else if (event.key == Key::Delete)
		{
			if (state.cursor == state.anchor)
			{
				state.anchor = Next(result, state.cursor);
			}
			replace({});
		}
		else if (event.key == Key::Left || event.key == Key::Right || event.key == Key::Home || event.key == Key::End)
		{
			const bool shift = (event.modifiers & static_cast<UInt32>(Modifiers::Shift)) != 0;
			if (!shift && state.cursor != state.anchor && (event.key == Key::Left || event.key == Key::Right))
			{
				state.cursor = event.key == Key::Left ? std::min(state.cursor, state.anchor)
				                                      : std::max(state.cursor, state.anchor);
			}
			else
			{
				state.cursor = event.key == Key::Home   ? 0
				               : event.key == Key::End  ? result.size()
				               : event.key == Key::Left ? Previous(result, state.cursor)
				                                        : Next(result, state.cursor);
			}
			if (!shift)
			{
				state.anchor = state.cursor;
			}
		}
		else
		{
			handled = false;
		}
		if (handled)
		{
			Use();
		}
	}
	else if (type == EventType::ValidateCommand || type == EventType::ExecuteCommand)
	{
		const bool copy = event.commandName == "Copy";
		const bool cut = event.commandName == "Cut";
		const bool paste = event.commandName == "Paste";
		const bool selectAll = event.commandName == "SelectAll";
		const bool supported =
		    selectAll || (paste && readClipboard) || ((copy || cut) && writeClipboard && state.cursor != state.anchor);
		if (supported)
		{
			if (type == EventType::ExecuteCommand)
			{
				if (selectAll)
				{
					state.anchor = 0;
					state.cursor = result.size();
				}
				else if (paste)
				{
					replace(readClipboard());
				}
				else
				{
					const auto [start, end] = selection();
					writeClipboard(result.substr(start, end - start));
					if (cut)
					{
						replace({});
					}
				}
			}
			Use();
		}
	}
	changed |= result != value;
	Draw(DrawType::Fill, rect, 0xff201b16);
	Draw(DrawType::Border, rect, keyboard == id ? 0xffffb55a : 0xff807060);
	const Float32 caret = Measure(result.substr(0, state.cursor));
	const Float32 available = std::max(0.f, rect.width - 10.f);
	if (caret < state.scroll)
	{
		state.scroll = caret;
	}
	if (caret > state.scroll + available)
	{
		state.scroll = caret - available;
	}
	BeginGroup(rect);
	if (keyboard == id)
	{
		const auto [start, end] = selection();
		const Float32 left = Measure(result.substr(0, start)) - state.scroll + 4;
		const Float32 right = Measure(result.substr(0, end)) - state.scroll + 4;
		if (start != end)
		{
			Draw(DrawType::Fill, {left, 2, right - left, std::max(0.f, rect.height - 4)}, 0xff785538);
		}
		Draw(DrawType::Fill, {caret - state.scroll + 4, 3, 1, std::max(0.f, rect.height - 6)}, 0xffeeeeee);
	}
	Draw(DrawType::Text, {-state.scroll, 0, rect.width + state.scroll, rect.height}, 0xffffffff, result);
	EndGroup();
	return result;
}

Float32 Session::Measure(const String& text) const
{
	if (measureText)
	{
		return measureText(text);
	}
	Float32 result = 0;
	for (Size cursor = 0; cursor < text.size(); cursor = Next(text, cursor))
	{
		result += 9.f;
	}
	return result;
}

void Session::FocusNext(bool backwards)
{
	if (focusOrder.empty())
	{
		keyboard = 0;
		return;
	}
	const auto found = std::find(focusOrder.begin(), focusOrder.end(), keyboard);
	const Size count = focusOrder.size();
	const Size index =
	    found == focusOrder.end() ? (backwards ? 0 : count - 1) : static_cast<Size>(found - focusOrder.begin());
	keyboard = focusOrder[(index + (backwards ? count - 1 : 1)) % count];
}

void Session::CancelInput()
{
	if (thread != std::this_thread::get_id())
	{
		throw std::logic_error("GUI session belongs to another thread");
	}
	hot = 0;
	keyboard = 0;
}

void Session::Reset()
{
	if (thread != std::this_thread::get_id() || inEvent)
	{
		throw std::logic_error("Cannot reset an active GUI session");
	}
	CancelInput();
	controls.clear();
	identities.clear();
	layout.clear();
	seen.clear();
	seenComponents.clear();
	focusOrder.clear();
	textStates.clear();
	commands.clear();
	diagnostics.clear();
	nextId = 1;
	exited = false;
	callback = 0;
}
} // namespace Ursa::Gui
