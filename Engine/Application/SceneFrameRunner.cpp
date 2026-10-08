#include <Core/Types.hpp>
#include <Application/SceneFrameRunner.hpp>

namespace Ursa
{
Result<void> SceneFrameRunner::ConfigureFixedStep(Float32 seconds, UInt32 maxSteps)
{
	return scheduler.Configure(seconds, maxSteps);
}

Result<void> SceneFrameRunner::Tick(Scene& scene, Float32 elapsed, Window* window, FrameInput input)
{
	const FrameSchedule schedule = scheduler.Advance(elapsed);
	for (Size index = 0; index < input.keys.size(); ++index)
	{
		input.keyPressed[index] = input.keys[index] && !previousInput.keys[index];
		input.keyReleased[index] = !input.keys[index] && previousInput.keys[index];
	}
	previousInput = input;
	elapsedSeconds += schedule.deltaSeconds;
	const UInt64 currentFrame = ++frameIndex;

	FrameContext context;
	context.deltaSeconds = schedule.deltaSeconds;
	context.fixedDeltaSeconds = scheduler.FixedSeconds();
	context.elapsedSeconds = elapsedSeconds;
	context.fixedElapsedSeconds = schedule.fixedElapsedSeconds;
	context.frameIndex = currentFrame;
	context.window = window;
	context.input = input;

	scene.BeginFrame();
	for (UInt32 step = 0; step < schedule.fixedSteps; ++step)
	{
		FrameContext fixedContext = context;
		fixedContext.deltaSeconds = scheduler.FixedSeconds();
		fixedContext.elapsedSeconds = schedule.fixedElapsedSeconds -
		                              static_cast<Float64>(schedule.fixedSteps - step - 1) * scheduler.FixedSeconds();
		fixedContext.fixedElapsedSeconds = fixedContext.elapsedSeconds;
		auto fixed = scene.FixedUpdate(fixedContext);
		if (!fixed)
		{
			return fixed;
		}
	}
	auto updated = scene.Update(context);
	if (!updated)
	{
		return updated;
	}
	return scene.LateUpdate(context);
}

void SceneFrameRunner::Reset()
{
	scheduler.Reset();
	previousInput = {};
	frameIndex = 0;
	elapsedSeconds = 0.0;
}

void SceneFrameRunner::ResetAccumulator()
{
	scheduler.ResetAccumulator();
	previousInput = {};
}
} // namespace Ursa
