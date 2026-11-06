#pragma once

#include <Application/FrameScheduler.hpp>
#include <Scene/Scene.hpp>
#include <Gui/GuiSession.hpp>

namespace Ursa
{
class SceneFrameRunner final
{
public:
	Result<void> ConfigureFixedStep(Float32 seconds, UInt32 maxSteps = 5);

	Result<void> Tick(Scene& scene, Float32 elapsedSeconds, Window* window, FrameInput input);

	Result<void> TickGui(Scene& scene, Float32 elapsedSeconds, Window* window, FrameInput input, Gui::Session& gui,
	                     const Vector<Gui::Event>& events, Gui::Point viewport, bool simulate = true);

	void Reset();

	void ResetAccumulator();
	void SynchronizeInput(const FrameInput& physical);
	FrameContext CurrentContext(Window* window = nullptr, FrameInput input = {}) const;

	Float64 ElapsedSeconds() const
	{
		return elapsedSeconds;
	}

	UInt64 FrameIndex() const
	{
		return frameIndex;
	}

private:
	Result<void> RunFrame(Scene& scene, Float32 elapsed, Window* window, FrameInput input, Gui::Session* gui,
	                      const Vector<Gui::Event>& events, Gui::Point viewport, bool simulate);
	FrameScheduler scheduler;
	FrameInput previousInput;
	Array<bool, FrameInput::keyCount> suppressedKeys{};
	Array<bool, 4> suppressedButtons{};
	Float64 fixedElapsedSeconds = 0;
	UInt64 frameIndex = 0;
	Float64 elapsedSeconds = 0.0;
};
} // namespace Ursa
