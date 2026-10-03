#include <application/FrameScheduler.hpp>
#include <algorithm>
#include <cmath>

namespace ursa {
Result<void> FrameScheduler::configure(float fixedSeconds, uint32_t maxSteps) {
    if (!std::isfinite(fixedSeconds) || fixedSeconds <= 0.0f || maxSteps == 0)
        return Result<void>::failure("Fixed step must be positive and finite; max steps must be nonzero");
    fixedSeconds_ = fixedSeconds;
    maxSteps_ = maxSteps;
    reset();
    return {};
}

FrameSchedule FrameScheduler::advance(float elapsedSeconds) {
    const float delta = std::max(0.0f, std::min(elapsedSeconds, 0.25f));
    accumulator_ += static_cast<double>(delta);
    uint32_t steps = 0;
    while (accumulator_ + 1e-9 >= fixedSeconds_ && steps < maxSteps_) {
        accumulator_ -= fixedSeconds_;
        ++steps;
    }
    if (steps == maxSteps_ && accumulator_ >= fixedSeconds_)
        accumulator_ = std::fmod(accumulator_, fixedSeconds_);
    return {delta, steps};
}

void FrameScheduler::reset() {
    accumulator_ = 0.0;
}
} // namespace ursa
