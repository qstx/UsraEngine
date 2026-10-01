#pragma once
#include <Ursa/RHI.hpp>

namespace ursa {
class Application {
public:
    virtual ~Application() = default;

    Result<void> run(uint32_t width = 1280, uint32_t height = 720, const std::string &title = "UrsaEngine",
                     bool visible = true, uint32_t frameLimit = 0);

protected:
    virtual Result<void> onStart() = 0;

    virtual Result<void> onFrame(float deltaSeconds) = 0;

    virtual void onStop() {
    }

    std::shared_ptr<Window> window_;
    rhi::DevicePtr device_;
    rhi::SwapchainPtr swapchain_;
};
} // namespace ursa
