#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <cstdint>

namespace ursa {
struct FrameSchedule {
    float deltaSeconds = 0.0f;
    uint32_t fixedSteps = 0;
};

class FrameScheduler final {
public:
    Result<void> configure(float fixedSeconds, uint32_t maxSteps);

    FrameSchedule advance(float elapsedSeconds);

    void reset();

    float fixedSeconds() const {
        return fixedSeconds_;
    }

    uint32_t maxSteps() const {
        return maxSteps_;
    }

private:
    double accumulator_ = 0.0;
    float fixedSeconds_ = 1.0f / 60.0f;
    uint32_t maxSteps_ = 5;
};
} // namespace ursa
