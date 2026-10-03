#include <Application/FrameScheduler.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa
{
Result<void> FrameScheduler::Configure(float fixedSeconds, uint32_t maxSteps)
{
	if (!std::isfinite(fixedSeconds) || fixedSeconds <= 0.0f || maxSteps == 0)
	{
		return Result<void>::Failure("Fixed step must be positive and finite; max steps must be nonzero");
	}
	this->fixedSeconds = fixedSeconds;
	this->maxSteps = maxSteps;
	Reset();
	return {};
}

FrameSchedule FrameScheduler::Advance(float elapsedSeconds)
{
	const float delta = std::max(0.0f, std::min(elapsedSeconds, 0.25f));
	accumulator += static_cast<double>(delta);
	uint32_t steps = 0;
	while (accumulator + 1e-9 >= fixedSeconds && steps < maxSteps)
	{
		accumulator -= fixedSeconds;
		++steps;
	}
	if (steps == maxSteps && accumulator >= fixedSeconds)
	{
		accumulator = std::fmod(accumulator, fixedSeconds);
	}
	return {delta, steps};
}

void FrameScheduler::Reset()
{
	accumulator = 0.0;
}
} // namespace Ursa
