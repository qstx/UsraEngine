#include <application/Application.hpp>
#include <chrono>

namespace ursa {
Result<void> Application::configureFixedStep(float seconds, uint32_t maxSteps) {
    return frameScheduler_.configure(seconds, maxSteps);
}

Result<void> Application::run(uint32_t width, uint32_t height, const String &title, bool visible, uint32_t frameLimit) {
    auto window = Window::create(width, height, title, visible);
    if (!window)
        return Result<void>::failure(window.error);
    window_ = std::move(window.value);
    rhi::DeviceDesc deviceDesc;
    deviceDesc.window = window_;
#ifndef NDEBUG
    deviceDesc.validation = true;
#endif
    auto device = rhi::createDevice(deviceDesc);
    if (!device)
        return Result<void>::failure(device.error);
    device_ = std::move(device.value);
    auto swapchain = device_->createSwapchain({});
    if (!swapchain)
        return Result<void>::failure(swapchain.error);
    swapchain_ = std::move(swapchain.value);
    auto started = onStart();
    if (!started) {
        if (scene_)
            scene_->clear();
        onStop();
        swapchain_.reset();
        device_.reset();
        window_.reset();
        return started;
    }
    if (scene_) {
        auto renderer = Renderer3D::create(device_);
        if (!renderer) {
            scene_->clear();
            onStop();
            swapchain_.reset();
            device_.reset();
            window_.reset();
            return Result<void>::failure("Cannot initialize scene renderer: " + renderer.error);
        }
        sceneRenderer_ = std::move(renderer.value);
    }
    auto previous = std::chrono::steady_clock::now();
    Result<void> result;
    uint32_t frames = 0;
    uint64_t frameIndex = 0;
    double elapsedSeconds = 0.0;
    while (!window_->shouldClose()) {
        window_->pollEvents();
        if (window_->framebufferWidth() == 0 || window_->framebufferHeight() == 0) {
            window_->waitEvents();
            previous = std::chrono::steady_clock::now();
            frameScheduler_.reset();
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        if (scene_) {
            const auto schedule = frameScheduler_.advance(dt);
            elapsedSeconds += schedule.deltaSeconds;
            FrameContext context;
            context.deltaSeconds = schedule.deltaSeconds;
            context.fixedDeltaSeconds = frameScheduler_.fixedSeconds();
            context.elapsedSeconds = elapsedSeconds;
            context.frameIndex = ++frameIndex;
            context.window = window_.get();
            context.input = FrameInput::capture(*window_);
            scene_->beginFrame();
            result = scene_->update(context);
            for (uint32_t step = 0; result && step < schedule.fixedSteps; ++step)
                result = scene_->fixedUpdate(context);
            if (result)
                result = scene_->lateUpdate(context);
            if (result) {
                const float aspect =
                    static_cast<float>(window_->framebufferWidth()) / static_cast<float>(window_->framebufferHeight());
                auto data = scene_->collectRenderData(aspect);
                if (!data)
                    result = Result<void>::failure(data.error);
                else
                    result = sceneRenderer_->renderModels(swapchain_, data.value.viewProjection, data.value.boxes,
                                                          data.value.lighting, data.value.cameraPosition);
            }
        } else {
            result = onFrame(dt);
        }
        if (!result)
            break;
        if (frameLimit && ++frames >= frameLimit)
            break;
    }
    auto idle = device_->waitIdle();
    if (scene_)
        scene_->clear();
    onStop();
    sceneRenderer_.reset();
    swapchain_.reset();
    device_.reset();
    window_.reset();
    if (!result)
        return result;
    return idle;
}
} // namespace ursa
