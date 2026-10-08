#pragma once

#include <Application/FrameScheduler.hpp>
#include <Scene/Scene.hpp>

namespace Ursa
{
class SceneFrameRunner final
{
public:
	Result<void> ConfigureFixedStep(Float32 seconds, UInt32 maxSteps = 5);

	Result<void> Tick(Scene& scene, Float32 elapsedSeconds, Window* window, FrameInput input);

	void Reset();

	void ResetAccumulator();

	Float64 ElapsedSeconds() const
	{
		return elapsedSeconds;
	}

	UInt64 FrameIndex() const
	{
		return frameIndex;
	}

private:
	FrameScheduler scheduler;
	FrameInput previousInput;
	UInt64 frameIndex = 0;
	Float64 elapsedSeconds = 0.0;
};
} // namespace Ursa
