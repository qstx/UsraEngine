#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Window/Window.hpp>
#include <Material/Material.hpp>
#include <functional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ursa::Gui
{
enum class EventType : UInt32
{
	Layout,
	Repaint,
	MouseDown,
	MouseUp,
	MouseMove,
	MouseDrag,
	ScrollWheel,
	KeyDown,
	KeyUp,
	ValidateCommand,
	ExecuteCommand,
	Used,
	Ignore
};

enum class FocusType : UInt32
{
	Passive,
	Keyboard
};
enum class Modifiers : UInt32
{
	None = 0,
	Shift = 1,
	Control = 2,
	Alt = 4,
	Command = 8
};

struct Point
{
	Float32 x = 0;
	Float32 y = 0;
};

struct Rect
{
	Float32 x = 0;
	Float32 y = 0;
	Float32 width = 0;
	Float32 height = 0;
	bool Contains(Point point) const;
};

struct Event
{
	EventType type = EventType::Ignore;
	EventType rawType = EventType::Ignore;
	Point mousePosition;
	Point delta;
	Int32 button = 0;
	Int32 clickCount = 1;
	Key key = Key::Unknown;
	UInt32 modifiers = 0;
	String text;
	String commandName;
};

enum class DrawType
{
	Fill,
	Border,
	Text,
	Texture
};

struct DrawCommand
{
	DrawType type = DrawType::Fill;
	Rect rect;
	Rect clip;
	UInt32 color = 0xffffffff;
	String text;
	MaterialTexturePtr texture;
};

// A session owns event consumption and control state. Rendering only reads its
// completed Repaint commands; no Dear ImGui input state participates here.
class Session final
{
public:
	Session() = default;
	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;
	void BeginEvent(Event event, Point viewport);
	void EndEvent();
	UInt64 BeginComponent(UInt64 component);
	void EndComponent(bool failed = false);
	UInt64 CallbackToken() const;
	const Event& Current(UInt64 token = 0) const;
	void Use(UInt64 token = 0);
	EventType GetTypeForControl(Int32 id, UInt64 token = 0) const;
	Int32 GetControlId(Int32 hint, FocusType focus, Rect rect);
	Int32 HotControl() const;
	void SetHotControl(Int32 id);
	Int32 KeyboardControl() const;
	void SetKeyboardControl(Int32 id);
	bool Changed() const;
	void SetChanged(bool value);
	void Exit();
	bool Exited() const;

	EventType LastEventType() const
	{
		return event.type;
	}

	bool HasTextFocus() const
	{
		return keyboard != 0 && textStates.count(keyboard) != 0;
	}

	bool HasKeyboardFocus() const
	{
		return keyboard != 0;
	}

	bool HasPointerCapture() const
	{
		return hot != 0;
	}

	Point ViewportSize() const;
	Point MousePosition() const;
	void PushId(Int32 id);
	void PopId();
	void BeginGroup(Rect rect);
	void EndGroup();
	void Label(Rect rect, const String& text);
	void DrawTexture(Rect rect, MaterialTexturePtr texture);
	bool Button(Rect rect, const String& text);
	bool Toggle(Rect rect, bool value, const String& text);
	Float32 HorizontalSlider(Rect rect, Float32 value, Float32 minimum, Float32 maximum);
	String TextField(Rect rect, const String& value);
	void CancelInput();
	void Reset();

	const std::vector<DrawCommand>& Commands() const
	{
		return commands;
	}

	const std::vector<String>& Diagnostics() const
	{
		return diagnostics;
	}

	std::function<String()> readClipboard;
	std::function<void(const String&)> writeClipboard;
	std::function<Float32(const String&)> measureText;

private:
	struct Control
	{
		UInt64 component = 0;
		UInt64 identity = 0;
		FocusType focus = FocusType::Passive;
	};

	struct Group
	{
		Point origin;
		Rect clip;
	};

	struct TextState
	{
		Size cursor = 0;
		Size anchor = 0;
		Float32 scroll = 0;
	};

	void Require(UInt64 token = 0) const;
	void RequireId(Int32 id) const;
	Rect Place(Rect rect) const;
	bool Hit(Rect rect) const;
	void Draw(DrawType type, Rect rect, UInt32 color, const String& text = {}, MaterialTexturePtr texture = {});
	void FocusNext(bool backwards);
	Float32 Measure(const String& text) const;
	std::thread::id thread = std::this_thread::get_id();
	Event event;
	Point viewport;
	bool inEvent = false;
	bool inComponent = false;
	bool exited = false;
	bool changed = false;
	UInt64 component = 0;
	UInt64 callback = 0;
	Size ordinal = 0;
	Size callIndex = 0;
	Size checkpoint = 0;
	Int32 hot = 0;
	Int32 keyboard = 0;
	Int32 nextId = 1;
	std::vector<Int32> ids;
	std::vector<Size> ordinals;
	std::vector<Group> groups;
	std::unordered_map<Int32, Control> controls;
	std::unordered_map<UInt64, Int32> identities;
	std::unordered_map<UInt64, std::vector<Int32>> layout;
	std::unordered_set<Int32> seen;
	std::unordered_set<UInt64> seenComponents;
	std::vector<Int32> focusOrder;
	std::unordered_map<Int32, TextState> textStates;
	std::vector<DrawCommand> commands;
	std::vector<String> diagnostics;
};
} // namespace Ursa::Gui
