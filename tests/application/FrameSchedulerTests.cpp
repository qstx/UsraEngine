#include <core/Containers.hpp>
#include <application/FrameScheduler.hpp>
#include <scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

class Trace final : public ursa::Component {
public:
    explicit Trace(ursa::Vector<char> &events) : events_(events) {
    }

    ursa::Result<void> onUpdate(const ursa::FrameContext &) override {
        events_.push_back('U');
        return {};
    }

    ursa::Result<void> onFixedUpdate(const ursa::FrameContext &context) override {
        expect(std::abs(context.fixedDeltaSeconds - 0.02f) < 1e-6f, "Fixed callback step changed");
        events_.push_back('F');
        return {};
    }

    ursa::Result<void> onLateUpdate(const ursa::FrameContext &) override {
        events_.push_back('L');
        return {};
    }

private:
    ursa::Vector<char> &events_;
};

void testSchedule() {
    ursa::FrameScheduler scheduler;
    expect(!scheduler.configure(0.0f, 2), "Zero fixed step was accepted");
    expect(!scheduler.configure(0.02f, 0), "Zero step cap was accepted");
    expect(static_cast<bool>(scheduler.configure(0.02f, 2)), "Valid step configuration failed");
    ursa::Scene scene;
    const auto node = scene.createNode();
    expect(static_cast<bool>(node), "Could not create test node");
    ursa::Vector<char> events;
    expect(static_cast<bool>(scene.addComponent<Trace>(node.value, events)), "Could not add test behavior");
    uint64_t frame = 0;
    auto drive = [&](float seconds) {
        const auto schedule = scheduler.advance(seconds);
        ursa::FrameContext context;
        context.deltaSeconds = schedule.deltaSeconds;
        context.fixedDeltaSeconds = scheduler.fixedSeconds();
        context.frameIndex = ++frame;
        scene.beginFrame();
        expect(static_cast<bool>(scene.update(context)), "Update failed");
        for (uint32_t step = 0; step < schedule.fixedSteps; ++step)
            expect(static_cast<bool>(scene.fixedUpdate(context)), "Fixed update failed");
        expect(static_cast<bool>(scene.lateUpdate(context)), "Late update failed");
        return schedule;
    };
    expect(drive(0.01f).fixedSteps == 0, "Premature fixed step");
    expect(drive(0.01f).fixedSteps == 1, "Missing fixed step");
    expect(drive(0.04f).fixedSteps == 2, "Two fixed steps were not executed");
    expect(events == ursa::Vector<char>({'U', 'L', 'U', 'F', 'L', 'U', 'F', 'F', 'L'}),
           "Frame phases ran in the wrong order");
    const auto longFrame = drive(1.0f);
    expect(longFrame.deltaSeconds == 0.25f && longFrame.fixedSteps == 2, "Long frame was not clamped and capped");
    expect(drive(0.01f).fixedSteps == 1, "Fractional time was not retained after dropping backlog");
    scheduler.reset();
    expect(drive(0.01f).fixedSteps == 0, "Reset did not clear the accumulator");
}
} // namespace

int main() {
    try {
        testSchedule();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
