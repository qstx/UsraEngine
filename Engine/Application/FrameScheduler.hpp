#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <cstdint>

namespace Ursa
{
struct FrameSchedule
{
	float deltaSeconds = 0.0f;
	uint32_t fixedSteps = 0;
};

class FrameScheduler final
{
public:
	Result<void> Configure(float fixedSeconds, uint32_t maxSteps);

	FrameSchedule Advance(float elapsedSeconds);

	void Reset();

	float FixedSeconds() const
	{
		return fixedSeconds;
	}

	uint32_t MaxSteps() const
	{
		return maxSteps;
	}

private:
	double accumulator = 0.0;
	float fixedSeconds = 1.0f / 60.0f;
	uint32_t maxSteps = 5;
};
} // namespace Ursa
