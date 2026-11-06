#include <Gui/GuiSession.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace Ursa;
using namespace Ursa::Gui;
constexpr Rect button{10, 10, 80, 24};

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

template <class F> void Reject(F action, const char* message)
{
	bool rejected = false;
	try
	{
		action();
	}
	catch (const std::exception&)
	{
		rejected = true;
	}
	Expect(rejected, message);
}

template <class F> void Pass(Session& session, Event event, F body, UInt64 owner = 1)
{
	session.BeginEvent(std::move(event), {320, 240});
	session.BeginComponent(owner);
	body();
	session.EndComponent();
	session.EndEvent();
}

Event Mouse(EventType type, float x = 20, float y = 20)
{
	Event result;
	result.type = type;
	result.mousePosition = {x, y};
	return result;
}

void ButtonsAndScopes()
{
	Session session;
	int clicks = 0;
	UInt64 oldToken = 0;
	const auto draw = [&]() { clicks += session.Button(button, "Button"); };
	Pass(session, {EventType::Layout},
	     [&]()
	     {
		     oldToken = session.CallbackToken();
		     draw();
		     session.Use();
		     Expect(session.Current().type == EventType::Layout, "Layout was consumed");
	     });
	Expect(!session.Diagnostics().empty(), "Layout Use should diagnose");
	Expect(session.Commands().empty(), "Layout generated rendering");
	Pass(session, Mouse(EventType::MouseDown),
	     [&]()
	     {
		     Reject([&]() { session.Current(oldToken); }, "Old Event wrapper remained usable");
		     draw();
		     Expect(session.Current().type == EventType::Used && session.Current().rawType == EventType::MouseDown,
		            "MouseDown consumption lost raw type");
	     });
	Pass(session, {EventType::Layout}, draw);
	Pass(session, Mouse(EventType::MouseUp), draw);
	Pass(session, {EventType::Layout}, draw);
	Pass(session, {EventType::Repaint}, draw);
	Expect(clicks == 1, "One click across several passes produced duplicate actions");
	Expect(!session.Commands().empty(), "Repaint generated no drawing");
	Pass(session, {EventType::Layout}, draw);
	Pass(session, Mouse(EventType::MouseDown), draw);
	Pass(session, {EventType::Layout}, draw);
	Pass(session, Mouse(EventType::MouseUp, 300, 200), draw);
	Expect(clicks == 1, "Release outside the button activated it");
	Pass(session, {EventType::Layout},
	     [&]()
	     {
		     session.Button(button, "Button");
		     bool rejected = false;
		     std::thread worker(
		         [&]()
		         {
			         try
			         {
				         session.Current();
			         }
			         catch (...)
			         {
				         rejected = true;
			         }
		         });
		     worker.join();
		     Expect(rejected, "Background thread accessed Event.Current");
	     });
	Reject([&]() { session.Current(); }, "Event escaped callback");
}

void ConsumptionAndIdentity()
{
	Session session;
	Int32 first = 0, second = 0;
	session.BeginEvent({EventType::Layout}, {320, 240});
	session.BeginComponent(1);
	first = session.GetControlId(7, FocusType::Keyboard, button);
	session.EndComponent();
	session.BeginComponent(2);
	second = session.GetControlId(7, FocusType::Keyboard, button);
	Expect(first != second, "Components shared a control ID");
	session.EndComponent();
	session.EndEvent();
	session.BeginEvent(Mouse(EventType::MouseDown), {320, 240});
	session.BeginComponent(1);
	Expect(first == session.GetControlId(7, FocusType::Keyboard, button), "Input ID differed from Layout");
	session.Use();
	session.EndComponent();
	session.BeginComponent(2);
	Expect(second == session.GetControlId(7, FocusType::Keyboard, button), "Second component ID changed");
	Expect(session.Current().type == EventType::Used, "Use was not shared with later components");
	Reject([&]() { session.SetHotControl(first); }, "Foreign ID accepted");
	session.EndComponent();
	session.EndEvent();
	Int32 dynamic = 0;
	Pass(session, {EventType::Layout},
	     [&]()
	     {
		     session.PushId(20);
		     dynamic = session.GetControlId(1, FocusType::Keyboard, button);
		     session.PopId();
		     session.PushId(30);
		     session.GetControlId(1, FocusType::Keyboard, button);
		     session.PopId();
	     });
	Pass(session, {EventType::Layout},
	     [&]()
	     {
		     session.PushId(10);
		     session.GetControlId(1, FocusType::Keyboard, button);
		     session.PopId();
		     session.PushId(20);
		     Expect(dynamic == session.GetControlId(1, FocusType::Keyboard, button),
		            "Explicit dynamic scope lost identity");
		     session.PopId();
	     });
	Pass(session, {EventType::Layout}, [&]() { session.GetControlId(42, FocusType::Passive, button); });
	session.BeginEvent({EventType::Repaint}, {320, 240});
	session.BeginComponent(1);
	Reject([&]() { session.GetControlId(43, FocusType::Passive, button); }, "Layout identity mismatch accepted");
	session.EndComponent(true);
	session.EndEvent();
}

void TextAndCommands()
{
	Session session;
	String value = "initial";
	String clipboard = "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80";
	int reads = 0, writes = 0;
	session.readClipboard = [&]()
	{
		++reads;
		return clipboard;
	};
	session.writeClipboard = [&](const String& text)
	{
		++writes;
		clipboard = text;
	};
	const auto field = [&]() { value = session.TextField(button, value); };
	const auto run = [&](Event event)
	{
		Pass(session, {EventType::Layout}, field);
		Pass(session, std::move(event), field);
	};
	run(Mouse(EventType::MouseDown));
	run(Mouse(EventType::MouseUp));
	Event command;
	command.type = EventType::ValidateCommand;
	command.commandName = "SelectAll";
	run(command);
	Expect(value == "initial", "ValidateCommand edited text");
	command.type = EventType::ExecuteCommand;
	run(command);
	command.commandName = "Paste";
	command.type = EventType::ValidateCommand;
	run(command);
	Expect(reads == 0 && writes == 0 && value == "initial", "Paste validation touched clipboard or text");
	command.type = EventType::ExecuteCommand;
	run(command);
	Expect(reads == 1 && value == clipboard, "Paste did not insert complete Unicode once");
	Pass(session, {EventType::Layout}, field);
	Pass(session, {EventType::Repaint}, field);
	Expect(reads == 1 && value == clipboard, "Repaint repeated paste");
	Event key;
	key.type = EventType::KeyDown;
	key.key = Key::Backspace;
	run(key);
	Expect(value == "\xe4\xb8\xad\xe6\x96\x87", "Backspace split a Unicode scalar");
	key.key = Key::Home;
	run(key);
	key.key = Key::Right;
	key.modifiers = static_cast<UInt32>(Modifiers::Shift);
	run(key);
	command.commandName = "Copy";
	command.type = EventType::ValidateCommand;
	run(command);
	Expect(writes == 0, "Copy validation changed clipboard");
	command.type = EventType::ExecuteCommand;
	run(command);
	Expect(writes == 1 && clipboard == "\xe4\xb8\xad", "Unicode selection Copy failed");
	key.key = Key::Right;
	key.modifiers = 0;
	run(key);
	key.key = Key::Backspace;
	run(key);
	Expect(value == "\xe6\x96\x87", "Right arrow did not collapse a Unicode selection to its right edge");
	command.commandName = "SelectAll";
	run(command);
	command.commandName = "Cut";
	command.type = EventType::ValidateCommand;
	run(command);
	Expect(writes == 1 && value == "\xe6\x96\x87", "Cut validation changed text or clipboard");
	command.type = EventType::ExecuteCommand;
	run(command);
	Expect(writes == 2 && clipboard == "\xe6\x96\x87" && value.empty(), "Cut did not execute exactly once");
	key.key = Key::Unknown;
	key.text = "\xf0\x9f\x98\x80";
	run(key);
	Expect(value == key.text, "Committed supplementary Unicode text was not inserted once");
	key.text.clear();
	key.key = Key::Home;
	run(key);
	key.key = Key::Delete;
	run(key);
	Expect(value.empty(), "Delete split a supplementary Unicode scalar");
	Pass(session, {EventType::Layout}, field);
	Pass(session, {EventType::Repaint}, field);
	Expect(reads == 1 && writes == 2 && value.empty(), "Layout/Repaint repeated a clipboard or Unicode edit");
}

void ExitAndRestoration()
{
	Session session;
	Pass(session, {EventType::Layout}, [&]() { session.Button(button, "Button"); });
	Pass(session, Mouse(EventType::MouseDown),
	     [&]()
	     {
		     session.Button(button, "Button");
		     session.BeginGroup(button);
		     session.PushId(1);
		     session.Exit();
	     });
	Expect(session.Exited(), "ExitGUI did not terminate dispatch");
	Pass(session, {EventType::Layout}, [&]() { session.Label(button, "Changed structure"); });
	Pass(session, {EventType::Repaint},
	     [&]()
	     {
		     session.Label(button, "Changed structure");
		     session.Exit();
	     });
	Expect(session.Commands().empty(), "Exited Repaint retained partial drawing");
	Pass(session, {EventType::Layout}, [&]() { session.Label(button, "Changed structure"); });
	Pass(session, {EventType::Repaint}, [&]() { session.Label(button, "Changed structure"); });
	Expect(!session.Commands().empty(), "ExitGUI prevented later frames");
	session.BeginEvent({EventType::Layout}, {320, 240});
	session.BeginComponent(1);
	session.BeginGroup(button);
	Reject([&]() { session.EndComponent(); }, "Unbalanced GUI scope accepted");
	session.BeginComponent(2);
	session.Label(button, "Other component survived");
	session.EndComponent();
	session.EndEvent();
	Session other;
	Pass(other, {EventType::Layout}, [&]() { other.Button(button, "Other"); });
	Pass(other, Mouse(EventType::MouseDown),
	     [&]()
	     {
		     other.Button(button, "Other");
		     Expect(other.HotControl() != 0, "Session capture failed");
	     });
	other.CancelInput();
	Pass(other, {EventType::Layout},
	     [&]()
	     {
		     Expect(other.HotControl() == 0 && other.KeyboardControl() == 0, "Cancel retained focus");
		     other.Button(button, "Other");
	     });
}

void FocusCaptureAndEditing()
{
	Session session;
	Int32 first = 0, second = 0;
	const auto controls = [&]()
	{
		first = session.GetControlId(1, FocusType::Keyboard, button);
		second = session.GetControlId(2, FocusType::Keyboard, button);
	};
	Pass(session, {EventType::Layout}, controls);
	Event tab;
	tab.type = EventType::KeyDown;
	tab.key = Key::Tab;
	Pass(session, tab,
	     [&]()
	     {
		     controls();
		     Expect(session.KeyboardControl() == first && session.Current().type == EventType::Used,
		            "Tab did not focus first Layout control");
	     });
	Pass(session, {EventType::Layout}, controls);
	Pass(session, tab,
	     [&]()
	     {
		     controls();
		     Expect(session.KeyboardControl() == second, "Tab did not advance focus");
	     });
	Pass(session, {EventType::Layout}, controls);
	tab.modifiers = static_cast<UInt32>(Modifiers::Shift);
	Pass(session, tab,
	     [&]()
	     {
		     controls();
		     Expect(session.KeyboardControl() == first, "Shift+Tab did not reverse focus");
		     session.SetHotControl(first);
		     Expect(session.GetTypeForControl(second) == EventType::Used,
		            "Used key became available to another control");
	     });
	Pass(session, {EventType::Layout}, [&]() {});
	Pass(session, {EventType::Layout},
	     [&]()
	     {
		     Expect(session.HotControl() == 0 && session.KeyboardControl() == 0,
		            "Removing a control retained its capture/focus");
	     });
	float value = 0.5f;
	const Rect sliderRect{10, 10, 100, 20};
	const auto slider = [&]() { value = session.HorizontalSlider(sliderRect, value, 0, 1); };
	Pass(session, {EventType::Layout}, slider);
	Pass(session, Mouse(EventType::MouseDown, 20, 20),
	     [&]()
	     {
		     slider();
		     Expect(session.Changed(), "Slider mouse press did not report an input edit");
	     });
	Pass(session, {EventType::Layout}, slider);
	Pass(session, Mouse(EventType::MouseDrag, 400, 20), slider);
	Expect(value == 1, "Captured slider did not drag beyond its rectangle");
	Pass(session, {EventType::Layout}, slider);
	Pass(session, Mouse(EventType::MouseUp, 400, 20),
	     [&]()
	     {
		     slider();
		     Expect(session.HotControl() == 0, "Outside slider release retained capture");
	     });
	Pass(session, {EventType::Layout}, slider);
	Pass(session, {EventType::Repaint},
	     [&]()
	     {
		     slider();
		     Expect(!session.Changed() && value == 1, "Slider repaint reported another edit");
	     });
	Pass(session, {EventType::Layout},
	     [&]()
	     {
		     Reject([&]() { session.HorizontalSlider(sliderRect, 0, 1, 1); }, "Invalid slider range accepted");
		     Reject([&]() { session.Label({0, 0, -1, 3}, "Invalid"); }, "Negative GUI rectangle accepted");
	     });
	bool toggled = false;
	const auto toggle = [&]() { toggled = session.Toggle(button, toggled, "Toggle"); };
	Pass(session, {EventType::Layout}, toggle);
	Pass(session, Mouse(EventType::MouseDown), toggle);
	Pass(session, {EventType::Layout}, toggle);
	Pass(session, Mouse(EventType::MouseUp),
	     [&]()
	     {
		     toggle();
		     Expect(session.Changed() && toggled, "Toggle did not change once on release");
	     });
	Pass(session, {EventType::Layout}, toggle);
	Pass(session, {EventType::Repaint},
	     [&]()
	     {
		     toggle();
		     Expect(toggled && !session.Changed(), "Toggle changed during repaint");
		     session.Use();
		     Expect(session.Current().type == EventType::Repaint, "Repaint was consumed");
	     });
	UInt64 foreignToken = 0;
	Pass(session, {EventType::Layout}, [&]() { foreignToken = session.CallbackToken(); });
	Session other;
	Pass(other, {EventType::Layout},
	     [&]() { Reject([&]() { other.Current(foreignToken); }, "Cross-session Event token accepted"); });
	int clicks = 0;
	const auto nested = [&]()
	{
		session.BeginGroup({10, 10, 30, 30});
		session.BeginGroup({5, 5, 10, 10});
		Expect(session.MousePosition().x == session.Current().mousePosition.x - 15,
		       "Group event position was not local");
		clicks += session.Button({0, 0, 50, 30}, "Clipped");
		session.EndGroup();
		session.EndGroup();
	};
	Pass(session, {EventType::Layout}, nested);
	Pass(session, Mouse(EventType::MouseDown, 30, 20), nested);
	Pass(session, {EventType::Layout}, nested);
	Pass(session, Mouse(EventType::MouseUp, 30, 20), nested);
	Expect(clicks == 0, "Clipped portion of a grouped button received input");
}
} // namespace

int main()
{
	try
	{
		ButtonsAndScopes();
		ConsumptionAndIdentity();
		TextAndCommands();
		ExitAndRestoration();
		FocusCaptureAndEditing();
		std::cout << "GUI event scope, consumption, identity, Unicode commands and ExitGUI passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
