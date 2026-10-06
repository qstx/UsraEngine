#include <Core/Types.hpp>
#include <Application/FrameScheduler.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa
{
Result<void> FrameScheduler::Configure(Ursa::Float32 stepSeconds, Ursa::UInt32 stepLimit)
{
	if (!std::isfinite(stepSeconds) || stepSeconds <= 0.0f || stepLimit == 0)
	{
		return Result<void>::Failure("Fixed step must be positive and finite; max steps must be nonzero");
	}
	this->fixedSeconds = stepSeconds;
	this->maxSteps = stepLimit;
	Reset();
	return {};
}

FrameSchedule FrameScheduler::Advance(Ursa::Float32 elapsedSeconds)
{
	const Ursa::Float32 delta = std::max(0.0f, std::min(elapsedSeconds, 0.25f));
	accumulator += static_cast<Ursa::Float64>(delta);
	Ursa::UInt32 steps = 0;
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
