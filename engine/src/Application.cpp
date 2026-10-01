#include <Ursa/Application.hpp>
#include <chrono>

namespace ursa {
Result<void> Application::run(uint32_t width, uint32_t height, const std::string &title, bool visible,
                              uint32_t frameLimit) {
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
    if (!started)
        return started;
    auto previous = std::chrono::steady_clock::now();
    Result<void> result;
    uint32_t frames = 0;
    while (!window_->shouldClose()) {
        window_->pollEvents();
        if (window_->framebufferWidth() == 0 || window_->framebufferHeight() == 0) {
            window_->waitEvents();
            previous = std::chrono::steady_clock::now();
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        result = onFrame(dt);
        if (!result)
            break;
        if (frameLimit && ++frames >= frameLimit)
            break;
    }
    auto idle = device_->waitIdle();
    onStop();
    swapchain_.reset();
    device_.reset();
    window_.reset();
    if (!result)
        return result;
    return idle;
}
} // namespace ursa
