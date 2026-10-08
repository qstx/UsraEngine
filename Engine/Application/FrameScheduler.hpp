#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <cstdint>

namespace Ursa
{
struct FrameSchedule
{
	Ursa::Float32 deltaSeconds = 0.0f;
	Ursa::UInt32 fixedSteps = 0;
	Ursa::Float64 fixedElapsedSeconds = 0.0;
};

class FrameScheduler final
{
public:
	Result<void> Configure(Ursa::Float32 stepSeconds, Ursa::UInt32 stepLimit);

	FrameSchedule Advance(Ursa::Float32 elapsedSeconds);

	void Reset();

	void ResetAccumulator();

	Ursa::Float32 FixedSeconds() const
	{
		return fixedSeconds;
	}

	Ursa::UInt32 MaxSteps() const
	{
		return maxSteps;
	}

private:
	Ursa::Float64 accumulator = 0.0;
	Ursa::Float32 fixedSeconds = 1.0f / 60.0f;
	Ursa::UInt32 maxSteps = 5;
	Ursa::Float64 fixedElapsedSeconds = 0.0;
};
} // namespace Ursa
