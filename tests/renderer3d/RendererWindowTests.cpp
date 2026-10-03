#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <iostream>
#include <stdexcept>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
    return std::move(result.value);
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
}
} // namespace

int main() {
    try {
        auto window = take(ursa::Window::create(640, 480, "Ursa 3D renderer test", false));
        ursa::rhi::DeviceDesc desc;
        desc.window = window;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(ursa::rhi::createDevice(desc));
        auto swapchain = take(device->createSwapchain({}));
        auto renderer = take(ursa::Renderer3D::create(device));
        ursa::Camera camera;
        const ursa::Vector<ursa::BoxInstance> boxes{{{{0, 0, 0}, {1, 1, 1}}, {0.8f, 0.2f, 0.1f, 1}}};

        done(renderer->render(swapchain, camera, boxes));
        window->resize(800, 600);
        window->pollEvents();
        done(renderer->render(swapchain, camera, boxes));
        window->minimize();
        window->pollEvents();
        done(renderer->render(swapchain, camera, boxes));
        window->restore();
        window->pollEvents();
        done(renderer->render(swapchain, camera, boxes));
        done(device->waitIdle());
        renderer.reset();
        swapchain.reset();
        device.reset();
        window.reset();
        std::cout << "3D renderer window test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "3D renderer window test failed: " << error.what() << '\n';
        return 1;
    }
}
