#include <Core/Types.hpp>
#include <Core/Profiler.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Application/SceneFrameRunner.hpp>

namespace Ursa
{
Result<void> SceneFrameRunner::ConfigureFixedStep(Float32 seconds, UInt32 maxSteps)
{
	return scheduler.Configure(seconds, maxSteps);
}

Result<void> SceneFrameRunner::Tick(Scene& scene, Float32 elapsed, Window* window, FrameInput input)
{
	return RunFrame(scene, elapsed, window, input, nullptr, {}, {}, true);
}

Result<void> SceneFrameRunner::TickGui(Scene& scene, Float32 elapsed, Window* window, FrameInput input,
                                       Gui::Session& gui, const Vector<Gui::Event>& events, Gui::Point viewport,
                                       bool simulate)
{
	return RunFrame(scene, elapsed, window, input, &gui, events, viewport, simulate);
}

Result<void> SceneFrameRunner::RunFrame(Scene& scene, Float32 elapsed, Window* window, FrameInput input,
                                        Gui::Session* gui, const Vector<Gui::Event>& events, Gui::Point viewport,
                                        bool simulate)
{
	URSA_PROFILE_SCOPE("SceneFrameRunner.Tick");
	Memory::TemporaryScope frameMemory;
	const FrameSchedule schedule = simulate ? scheduler.Advance(elapsed) : FrameSchedule{0, 0, fixedElapsedSeconds};
	fixedElapsedSeconds = schedule.fixedElapsedSeconds;
	elapsedSeconds += schedule.deltaSeconds;
	const UInt64 currentFrame = simulate ? ++frameIndex : frameIndex;

	FrameContext context;
	context.deltaSeconds = schedule.deltaSeconds;
	context.fixedDeltaSeconds = scheduler.FixedSeconds();
	context.elapsedSeconds = elapsedSeconds;
	context.fixedElapsedSeconds = schedule.fixedElapsedSeconds;
	context.frameIndex = currentFrame;
	context.window = window;
	context.input = input;
	context.gui = gui;

	scene.BeginFrame();
	const auto cycle = [&](Gui::Event event) -> Result<void>
	{
		auto snapshot = scene.PrepareGuiCycle(context);
		if (!snapshot)
		{
			return Result<void>::Failure(snapshot.error);
		}
		gui->BeginEvent({Gui::EventType::Layout}, viewport);
		auto result = scene.DispatchGui(context, snapshot.value);
		gui->EndEvent();
		if (result && !gui->Exited())
		{
			gui->BeginEvent(std::move(event), viewport);
			result = scene.DispatchGui(context, snapshot.value);
			gui->EndEvent();
		}
		auto committed = scene.CommitChanges();
		return result ? committed : result;
	};
	if (gui)
	{
		for (const auto& event : events)
		{
			Result<void> result;
			if (event.type == Gui::EventType::KeyDown && !event.commandName.empty() && gui->HasTextFocus())
			{
				Gui::Event command = event;
				command.type = Gui::EventType::ValidateCommand;
				result = cycle(command);
				if (result && gui->LastEventType() == Gui::EventType::Used)
				{
					command.type = Gui::EventType::ExecuteCommand;
					result = cycle(std::move(command));
				}
				else if (result)
				{
					result = cycle(event);
				}
			}
			else
			{
				result = cycle(event);
			}
			if (!result)
			{
				return result;
			}
			const bool used = gui->Exited() || gui->LastEventType() == Gui::EventType::Used;
			const Size key = static_cast<Size>(event.key);
			if ((event.rawType == Gui::EventType::KeyDown || event.type == Gui::EventType::KeyDown ||
			     event.type == Gui::EventType::KeyUp) &&
			    event.key != Key::Unknown && key < suppressedKeys.size() && used)
			{
				suppressedKeys[key] = true;
			}
			if (event.type == Gui::EventType::MouseDown || event.type == Gui::EventType::MouseUp ||
			    event.type == Gui::EventType::MouseDrag)
			{
				if (event.button >= 0 && event.button < 4 && (used || gui->HasPointerCapture()))
				{
					suppressedButtons[static_cast<Size>(event.button)] = true;
				}
			}
			if (event.type == Gui::EventType::MouseMove && used)
			{
				input.pointerConsumed = true;
			}
			if (event.type == Gui::EventType::ScrollWheel)
			{
				input.scrollConsumed |= used;
				if (!used)
				{
					input.scrollX += event.delta.x;
					input.scrollY += event.delta.y;
				}
			}
		}
	}
	for (Size index = 0; index < input.keys.size(); ++index)
	{
		const bool physical = input.keys[index];
		if (gui && gui->HasTextFocus() && physical)
		{
			suppressedKeys[index] = true;
		}
		if (suppressedKeys[index])
		{
			input.keys[index] = previousInput.keys[index] = false;
			input.consumedKeys[index] = true;
			if (!physical)
			{
				suppressedKeys[index] = false;
			}
		}
		input.keyPressed[index] = input.keys[index] && !previousInput.keys[index];
		input.keyReleased[index] = !input.keys[index] && previousInput.keys[index];
	}
	for (Size index = 0; index < input.mouseButtons.size(); ++index)
	{
		const bool physical = input.mouseButtons[index];
		if (gui && gui->HasPointerCapture() && physical)
		{
			suppressedButtons[index] = true;
		}
		if (suppressedButtons[index])
		{
			input.mouseButtons[index] = previousInput.mouseButtons[index] = false;
			input.consumedButtons[index] = input.pointerConsumed = true;
			if (!physical)
			{
				suppressedButtons[index] = false;
			}
		}
		input.mousePressed[index] = input.mouseButtons[index] && !previousInput.mouseButtons[index];
		input.mouseReleased[index] = !input.mouseButtons[index] && previousInput.mouseButtons[index];
	}
	previousInput = input;
	context.input = input;
	if (simulate)
	{
		for (UInt32 step = 0; step < schedule.fixedSteps; ++step)
		{
			URSA_PROFILE_SCOPE("Scene.FixedUpdate");
			FrameContext fixedContext = context;
			fixedContext.deltaSeconds = scheduler.FixedSeconds();
			fixedContext.elapsedSeconds =
			    schedule.fixedElapsedSeconds -
			    static_cast<Float64>(schedule.fixedSteps - step - 1) * scheduler.FixedSeconds();
			fixedContext.fixedElapsedSeconds = fixedContext.elapsedSeconds;
			auto fixed = scene.FixedUpdate(fixedContext);
			if (!fixed)
			{
				return fixed;
			}
		}
		{
			URSA_PROFILE_SCOPE("Scene.Update");
			auto updated = scene.Update(context);
			if (!updated)
			{
				return updated;
			}
		}
		{
			URSA_PROFILE_SCOPE("Scene.LateUpdate");
			auto late = scene.LateUpdate(context);
			if (!late)
			{
				return late;
			}
		}
	}
	return gui ? cycle({Gui::EventType::Repaint}) : Result<void>{};
}

void SceneFrameRunner::Reset()
{
	scheduler.Reset();
	previousInput = {};
	frameIndex = 0;
	elapsedSeconds = 0.0;
	fixedElapsedSeconds = 0.0;
	suppressedKeys = {};
	suppressedButtons = {};
}

void SceneFrameRunner::ResetAccumulator()
{
	scheduler.ResetAccumulator();
	previousInput = {};
}

void SceneFrameRunner::SynchronizeInput(const FrameInput& physical)
{
	previousInput = physical;
	for (Size i = 0; i < suppressedKeys.size(); ++i)
	{
		suppressedKeys[i] = physical.keys[i];
	}
	for (Size i = 0; i < suppressedButtons.size(); ++i)
	{
		suppressedButtons[i] = physical.mouseButtons[i];
	}
}

FrameContext SceneFrameRunner::CurrentContext(Window* window, FrameInput input) const
{
	FrameContext context;
	context.fixedDeltaSeconds = scheduler.FixedSeconds();
	context.elapsedSeconds = elapsedSeconds;
	context.fixedElapsedSeconds = fixedElapsedSeconds;
	context.frameIndex = frameIndex;
	context.window = window;
	context.input = input;
	return context;
}
} // namespace Ursa
