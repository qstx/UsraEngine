#include <Core/Containers.hpp>
#include <Application/FrameScheduler.hpp>
#include <Scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class Trace final : public Ursa::Component
{
public:
	explicit Trace(Ursa::Vector<char>& events) : events(events)
	{
	}

	Ursa::Result<void> OnUpdate(const Ursa::FrameContext&) override
	{
		events.push_back('U');
		return {};
	}

	Ursa::Result<void> OnFixedUpdate(const Ursa::FrameContext& context) override
	{
		Expect(std::abs(context.fixedDeltaSeconds - 0.02f) < 1e-6f, "Fixed callback step changed");
		events.push_back('F');
		return {};
	}

	Ursa::Result<void> OnLateUpdate(const Ursa::FrameContext&) override
	{
		events.push_back('L');
		return {};
	}

private:
	Ursa::Vector<char>& events;
};

void TestSchedule()
{
	Ursa::FrameScheduler scheduler;
	Expect(!scheduler.Configure(0.0f, 2), "Zero fixed step was accepted");
	Expect(!scheduler.Configure(0.02f, 0), "Zero step cap was accepted");
	Expect(static_cast<bool>(scheduler.Configure(0.02f, 2)), "Valid step configuration failed");
	Ursa::Scene scene;
	const auto node = scene.CreateNode();
	Expect(static_cast<bool>(node), "Could not create test node");
	Ursa::Vector<char> events;
	Expect(static_cast<bool>(scene.AddComponent<Trace>(node.value, events)), "Could not add test behavior");
	uint64_t frame = 0;
	auto drive = [&](float seconds)
	{
		const auto schedule = scheduler.Advance(seconds);
		Ursa::FrameContext context;
		context.deltaSeconds = schedule.deltaSeconds;
		context.fixedDeltaSeconds = scheduler.FixedSeconds();
		context.frameIndex = ++frame;
		scene.BeginFrame();
		Expect(static_cast<bool>(scene.Update(context)), "Update failed");
		for (uint32_t step = 0; step < schedule.fixedSteps; ++step)
		{
			Expect(static_cast<bool>(scene.FixedUpdate(context)), "Fixed update failed");
		}
		Expect(static_cast<bool>(scene.LateUpdate(context)), "Late update failed");
		return schedule;
	};
	Expect(drive(0.01f).fixedSteps == 0, "Premature fixed step");
	Expect(drive(0.01f).fixedSteps == 1, "Missing fixed step");
	Expect(drive(0.04f).fixedSteps == 2, "Two fixed steps were not executed");
	Expect(events == Ursa::Vector<char>({'U', 'L', 'U', 'F', 'L', 'U', 'F', 'F', 'L'}),
	       "Frame phases ran in the wrong order");
	const auto longFrame = drive(1.0f);
	Expect(longFrame.deltaSeconds == 0.25f && longFrame.fixedSteps == 2, "Long frame was not clamped and capped");
	Expect(drive(0.01f).fixedSteps == 1, "Fractional time was not retained after dropping backlog");
	scheduler.Reset();
	Expect(drive(0.01f).fixedSteps == 0, "Reset did not clear the accumulator");
}
} // namespace

int main()
{
	try
	{
		TestSchedule();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
