#pragma once
#include <core/Containers.hpp>
#include <rhi/RHI.hpp>
#include <application/FrameScheduler.hpp>
#include <scene/Scene.hpp>
#include <renderer3d/Renderer3D.hpp>

namespace ursa {
class Application {
public:
    virtual ~Application() = default;

    Result<void> run(uint32_t width = 1280, uint32_t height = 720, const String &title = "UrsaEngine",
                     bool visible = true, uint32_t frameLimit = 0);

    Result<void> configureFixedStep(float seconds, uint32_t maxSteps = 5);

protected:
    virtual Result<void> onStart() = 0;

    virtual Result<void> onFrame(float deltaSeconds) {
        (void)deltaSeconds;
        return {};
    }

    virtual void onStop() {
    }

    std::shared_ptr<Window> window_;
    rhi::DevicePtr device_;
    rhi::SwapchainPtr swapchain_;
    std::shared_ptr<Scene> scene_;

private:
    FrameScheduler frameScheduler_;
    std::unique_ptr<Renderer3D> sceneRenderer_;
};
} // namespace ursa
