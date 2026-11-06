#include <Application/SceneFrameRunner.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Gui/GuiInput.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Scripting;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

struct Entry
{
	BehaviourEvent callback;
	Gui::EventType type;
	UInt64 frame;
	Float64 time;
	Float32 delta;
};

class Runtime final : public ManagedBehaviourRuntime
{
public:
	Result<ComponentId> AddExisting(const GameObject::Ptr&, const String&, UInt64) override
	{
		return Result<ComponentId>::Failure("unused");
	}

	Result<ComponentId> CreateAndAdd(const GameObject::Ptr&, const String&) override
	{
		return Result<ComponentId>::Failure("unused");
	}

	Result<void> Destroy(UInt64) override
	{
		return {};
	}

	Result<void> Dispatch(UInt64 token, BehaviourEvent event, const FrameContext* frame, Component* component) override
	{
		if (action)
		{
			return action(token, event, frame, component);
		}
		if (!frame)
		{
			return {};
		}
		Gui::EventType type = Gui::EventType::Ignore;
		if (event == BehaviourEvent::OnGUI)
		{
			type = frame->gui->Current().type;
			frame->gui->GetControlId(1, Gui::FocusType::Passive, {0, 0, 100, 30});
			if (type == Gui::EventType::KeyDown && frame->gui->Current().key == Key::W)
			{
				frame->gui->Use();
			}
			if (type == Gui::EventType::Repaint)
			{
				repaintValue = updates;
			}
		}
		if (event == BehaviourEvent::Update)
		{
			++updates;
			Expect(!frame->input.KeyDown(Key::W) && !frame->input.KeyPressed(Key::W),
			       "Consumed GUI key reached the current simulation");
			Expect(frame->input.KeyDown(Key::D), "Mouse/other-key handling suppressed unrelated keyboard input");
		}
		entries.push_back({event, type, frame->frameIndex, frame->elapsedSeconds, frame->deltaSeconds});
		return {};
	}

	Vector<Entry> entries;
	std::function<Result<void>(UInt64, BehaviourEvent, const FrameContext*, Component*)> action;
	int updates = 0;
	int repaintValue = 0;
};

void FrameOrder()
{
	static_assert(static_cast<UInt32>(BehaviourEvent::Awake) == 0 &&
	              static_cast<UInt32>(BehaviourEvent::OnDestroy) == 7 &&
	              static_cast<UInt32>(BehaviourEvent::OnGUI) == 8);
	Runtime runtime;
	Scene scene;
	auto owner = scene.CreateGameObject("GUI owner");
	Expect(bool(owner), "Could not create GUI owner");
	Expect(bool(scene.AddComponent<ScriptBehaviourComponent>(owner.value, runtime, "GuiFixture", 1)),
	       "Could not create behaviour");
	SceneFrameRunner runner;
	Done(runner.ConfigureFixedStep(0.02f));
	Gui::Session gui;
	FrameInput input;
	input.keys[static_cast<Size>(Key::W)] = input.keys[static_cast<Size>(Key::D)] = true;
	Gui::Event down;
	down.type = Gui::EventType::KeyDown;
	down.key = Key::W;
	Gui::Event up = down;
	up.type = Gui::EventType::KeyUp;
	Done(runner.TickGui(scene, 0.05f, nullptr, input, gui, {down, up}, {320, 240}));
	Expect(runtime.updates == 1 && runner.FrameIndex() == 1, "Multiple GUI inputs advanced simulation more than once");
	Vector<Gui::EventType> guiEvents;
	int fixed = 0;
	bool started = false, updated = false;
	for (const auto& entry : runtime.entries)
	{
		if (entry.callback == BehaviourEvent::Start)
		{
			started = true;
		}
		if (entry.callback == BehaviourEvent::FixedUpdate)
		{
			++fixed;
		}
		if (entry.callback == BehaviourEvent::Update)
		{
			updated = true;
		}
		if (entry.callback == BehaviourEvent::OnGUI)
		{
			Expect(started, "GUI ran before Start");
			guiEvents.push_back(entry.type);
			if (entry.type == Gui::EventType::KeyDown || entry.type == Gui::EventType::KeyUp)
			{
				Expect(!updated, "GUI input ran after simulation");
			}
			if (entry.type == Gui::EventType::Repaint)
			{
				Expect(updated, "Repaint ran before Update");
			}
		}
	}
	const Vector<Gui::EventType> expected{Gui::EventType::Layout, Gui::EventType::KeyDown, Gui::EventType::Layout,
	                                      Gui::EventType::KeyUp,  Gui::EventType::Layout,  Gui::EventType::Repaint};
	Expect(guiEvents == expected && fixed == 2 && runtime.repaintValue == 1,
	       "GUI/simulation ordering or fixed-step count was incorrect");
	const auto time = runner.ElapsedSeconds();
	runtime.entries.clear();
	Done(runner.TickGui(scene, 5.f, nullptr, input, gui, {}, {320, 240}, false));
	Expect(runtime.updates == 1 && runner.FrameIndex() == 1 && runner.ElapsedSeconds() == time,
	       "Paused GUI advanced simulation time");
	Expect(runtime.entries.size() == 2 && runtime.entries[0].type == Gui::EventType::Layout &&
	           runtime.entries[1].type == Gui::EventType::Repaint && runtime.entries[1].delta == 0,
	       "Pause did not dispatch Layout/Repaint with zero delta");
	scene.Clear();
}

void MutationBoundary()
{
	for (int mode = 0; mode < 4; ++mode)
	{
		Runtime runtime;
		Scene scene;
		auto owner = scene.CreateGameObject("Original").value;
		auto next = scene.CreateGameObject("Next cycle").value;
		auto id = scene.AddComponent<ScriptBehaviourComponent>(owner, runtime, "Original", 1).value;
		int downOriginal = 0, downNew = 0, upNew = 0, repaintNew = 0, awakeNew = 0, startNew = 0, updateOriginal = 0;
		runtime.action = [&](UInt64 token, BehaviourEvent callback, const FrameContext* frame,
		                     Component*) -> Result<void>
		{
			if (token == 2 && callback == BehaviourEvent::Awake)
			{
				++awakeNew;
			}
			if (token == 2 && callback == BehaviourEvent::Start)
			{
				++startNew;
			}
			if (token == 1 && callback == BehaviourEvent::Update)
			{
				++updateOriginal;
			}
			if (callback != BehaviourEvent::OnGUI)
			{
				return {};
			}
			const auto type = frame->gui->Current().rawType;
			if (token == 2)
			{
				downNew += type == Gui::EventType::MouseDown;
				upNew += type == Gui::EventType::MouseUp;
				repaintNew += type == Gui::EventType::Repaint;
			}
			if (token == 1 && type == Gui::EventType::MouseDown)
			{
				++downOriginal;
				auto added = scene.AddComponent<ScriptBehaviourComponent>(next, runtime, "New", 2);
				if (!added)
				{
					return Result<void>::Failure(added.error);
				}
				if (mode == 0)
				{
					Done(scene.SetComponentEnabled(id, false));
				}
				if (mode == 1)
				{
					Done(scene.RemoveComponent(id));
				}
				if (mode == 2)
				{
					Done(scene.DestroyGameObject(owner));
				}
				frame->gui->Exit();
			}
			return {};
		};
		SceneFrameRunner runner;
		Gui::Session gui;
		Gui::Event down;
		down.type = Gui::EventType::MouseDown;
		Gui::Event up;
		up.type = Gui::EventType::MouseUp;
		Done(runner.TickGui(scene, 0.016f, nullptr, {}, gui, {down, up}, {320, 240}));
		Expect(downOriginal == 1 && downNew == 0 && upNew == 1 && repaintNew == 1 && awakeNew == 1 && startNew == 1,
		       "GUI mutation did not wait for a fresh Layout or replayed the previous event/lifecycle");
		Expect(updateOriginal == (mode == 3 ? 1 : 0), "Disabled/removed/destroyed GUI behaviour updated again");
		scene.Clear();
	}
}

void RawEvents()
{
	Gui::InputQueue input;
	WindowEvent event;
	event.type = WindowEventType::KeyPressed;
	event.key = Key::W;
	input.Push(event);
	event.type = WindowEventType::Character;
	event.codepoint = 0x1f600;
	input.Push(event);
	event.type = WindowEventType::KeyReleased;
	event.key = Key::W;
	input.Push(event);
	auto events = input.Drain();
	Expect(events.size() == 3 && events[0].type == Gui::EventType::KeyDown && events[0].text.empty() &&
	           events[1].text == "\xf0\x9f\x98\x80" && events[2].type == Gui::EventType::KeyUp,
	       "Raw physical/text event ordering was lost");
	event = {};
	event.type = WindowEventType::CursorMoved;
	event.x = 10;
	event.y = 20;
	input.Push(event);
	event.type = WindowEventType::MousePressed;
	event.button = MouseButton::Left;
	input.Push(event);
	event.type = WindowEventType::CursorMoved;
	event.x = 400;
	input.Push(event);
	event.type = WindowEventType::MouseReleased;
	input.Push(event);
	events = input.Drain();
	Expect(events.size() == 4 && events[1].mousePosition.x == 10 && events[2].type == Gui::EventType::MouseDrag &&
	           events[3].mousePosition.x == 400,
	       "Raw capture drag/release positions were not preserved");
	event.type = WindowEventType::Focused;
	event.focused = false;
	input.Push(event);
	Expect(input.Drain().empty() && input.TakeFocusLost(), "Focus loss did not cancel pending input");
}
} // namespace

int main()
{
	try
	{
		FrameOrder();
		MutationBoundary();
		RawEvents();
		std::cout << "GUI frame ordering, pause and ordered raw input passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
