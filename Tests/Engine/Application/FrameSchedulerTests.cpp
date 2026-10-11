#include <Application/SceneFrameRunner.hpp>
#include <Core/Types.hpp>
#include <Scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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
	explicit Trace(Ursa::Vector<char>& eventTrace) : events(eventTrace)
	{
	}

	Ursa::Result<void> Update(const Ursa::FrameContext& context) override
	{
		Expect(std::abs(context.deltaSeconds - expectedFrameDelta) < 1e-6f, "Update received the wrong frame delta");
		frameDeltas.push_back(context.deltaSeconds);
		events.push_back('U');
		return {};
	}

	Ursa::Result<void> FixedUpdate(const Ursa::FrameContext& context) override
	{
		Expect(std::abs(context.deltaSeconds - 0.02f) < 1e-6f, "FixedUpdate received the wrong delta");
		Expect(std::abs(context.fixedDeltaSeconds - 0.02f) < 1e-6f, "Fixed step changed");
		Expect(std::abs(context.elapsedSeconds - context.fixedElapsedSeconds) < 1e-8,
		       "FixedUpdate elapsed time did not use the fixed simulation clock");
		fixedTimes.push_back(context.fixedElapsedSeconds);
		fixedPressEdges.push_back(context.input.KeyPressed(Ursa::Key::W));
		fixedReleaseEdges.push_back(context.input.KeyReleased(Ursa::Key::W));
		events.push_back('F');
		return {};
	}

	Ursa::Result<void> LateUpdate(const Ursa::FrameContext&) override
	{
		events.push_back('L');
		return {};
	}

	Ursa::Float32 expectedFrameDelta = 0.0f;
	Ursa::Vector<Ursa::Float32> frameDeltas;
	Ursa::Vector<Ursa::Float64> fixedTimes;
	Ursa::Vector<bool> fixedPressEdges;
	Ursa::Vector<bool> fixedReleaseEdges;

private:
	Ursa::Vector<char>& events;
};

void TestSharedFrameOrderAndTime()
{
	Ursa::SceneFrameRunner runner;
	Expect(!runner.ConfigureFixedStep(0.0f, 2), "Zero fixed step was accepted");
	Expect(!runner.ConfigureFixedStep(0.02f, 0), "Zero step cap was accepted");
	Expect(static_cast<bool>(runner.ConfigureFixedStep(0.02f, 2)), "Valid fixed step configuration failed");

	Ursa::Scene scene;
	const auto node = scene.CreateGameObject();
	Expect(static_cast<bool>(node), "Could not create the test GameObject");
	Ursa::Vector<char> events;
	const auto traceId = scene.AddComponent<Trace>(node.value, events);
	Expect(static_cast<bool>(traceId), "Could not add the trace component");
	auto* trace = static_cast<Trace*>(scene.GetComponent(traceId.value));
	Ursa::FrameInput input;

	trace->expectedFrameDelta = 0.01f;
	Expect(static_cast<bool>(runner.Tick(scene, 0.01f, nullptr, input)), "First frame failed");
	Expect(events == Ursa::Vector<char>({'U', 'L'}), "A frame without a fixed step used the wrong phase order");

	events.clear();
	trace->expectedFrameDelta = 0.01f;
	Expect(static_cast<bool>(runner.Tick(scene, 0.01f, nullptr, input)), "Second frame failed");
	Expect(events == Ursa::Vector<char>({'F', 'U', 'L'}), "FixedUpdate did not run before Update");

	events.clear();
	trace->expectedFrameDelta = 0.04f;
	input.keys[static_cast<Ursa::Size>(Ursa::Key::W)] = true;
	Expect(static_cast<bool>(runner.Tick(scene, 0.04f, nullptr, input)), "Multi-step frame failed");
	Expect(events == Ursa::Vector<char>({'F', 'F', 'U', 'L'}), "Multiple fixed steps did not precede Update");
	Expect(trace->fixedTimes.size() == 3 && std::abs(trace->fixedTimes[0] - 0.02) < 1e-8 &&
	           std::abs(trace->fixedTimes[1] - 0.04) < 1e-8 && std::abs(trace->fixedTimes[2] - 0.06) < 1e-8,
	       "Fixed simulation time did not advance once per fixed step");
	Expect(trace->fixedPressEdges[1] && trace->fixedPressEdges[2],
	       "Fixed steps did not share the same sampled press edge");

	events.clear();
	trace->expectedFrameDelta = 0.25f;
	Expect(static_cast<bool>(runner.Tick(scene, 1.0f, nullptr, input)), "Long frame failed");
	Expect(events == Ursa::Vector<char>({'F', 'F', 'U', 'L'}), "Long frame exceeded its fixed-step cap");
	Expect(trace->fixedTimes.size() == 5 && std::abs(trace->fixedTimes.back() - 0.1) < 1e-8,
	       "Dropped fixed-step backlog advanced simulation time");
	Expect(runner.FrameIndex() == 4 && std::abs(runner.ElapsedSeconds() - 0.31) < 1e-6,
	       "Frame index or clamped elapsed time changed");

	events.clear();
	trace->expectedFrameDelta = 0.02f;
	input.keys[static_cast<Ursa::Size>(Ursa::Key::W)] = false;
	Expect(static_cast<bool>(runner.Tick(scene, 0.02f, nullptr, input)), "Key release frame failed");
	Expect(events == Ursa::Vector<char>({'F', 'U', 'L'}) && trace->fixedReleaseEdges.back(),
	       "Input release edge was not available in the sampled frame");
	Expect(std::abs(trace->fixedTimes.back() - 0.12) < 1e-8 && std::abs(runner.ElapsedSeconds() - 0.33) < 1e-6,
	       "Fixed and frame clocks diverged after additional input sampling");
}
} // namespace

int main()
{
	try
	{
		TestSharedFrameOrderAndTime();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
