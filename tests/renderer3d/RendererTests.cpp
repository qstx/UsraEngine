#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <scene/Scene.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

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

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class Draw>
ursa::Vector<uint8_t> readFrameWith(const ursa::rhi::DevicePtr &device, Draw draw, uint32_t width = 64,
                                    uint32_t height = 64) {
    using namespace ursa::rhi;
    auto color = take(device->createTexture(
        {width, height, Format::RGBA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    done(take(draw(color))->wait());
    auto readback = take(device->createBuffer(
        {static_cast<size_t>(width) * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
    auto command = take(device->createCommandList());
    done(command->begin());
    done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(command->copyTextureToBuffer(color, readback));
    done(command->end());
    done(take(device->submit(command))->wait());
    ursa::Vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    done(readback->read(0, pixels.data(), pixels.size()));
    return pixels;
}

ursa::Vector<uint8_t> readFrame(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer,
                                const ursa::Camera &camera, const ursa::Vector<ursa::BoxInstance> &boxes,
                                uint32_t width = 64, uint32_t height = 64) {
    return readFrameWith(
        device, [&](const ursa::rhi::TexturePtr &color) { return renderer.renderOffscreen(color, camera, boxes); },
        width, height);
}

ursa::Array<uint8_t, 4> pixel(const ursa::Vector<uint8_t> &image, uint32_t width, uint32_t x, uint32_t y) {
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
    return {image[offset], image[offset + 1], image[offset + 2], image[offset + 3]};
}

void testInstances(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    ursa::Camera camera;
    camera.position = {0, 0, 4};
    camera.target = {};
    const ursa::Vector<ursa::BoxInstance> boxes{
        {{{-0.75f, 0, 0}, {0.8f, 0.8f, 0.8f}}, {1, 0, 0, 1}},
        {{{0.75f, 0, 0}, {0.8f, 0.8f, 0.8f}}, {0, 0, 1, 1}},
    };
    const auto image = readFrame(device, renderer, camera, boxes);
    const auto left = pixel(image, 64, 18, 32);
    const auto right = pixel(image, 64, 46, 32);
    expect(left[0] > 100 && left[2] < 30, "Left instance color or transform is wrong");
    expect(right[2] > 100 && right[0] < 30, "Right instance color or transform is wrong");

    camera.position = {2, 0, 4};
    camera.target = {2, 0, 0};
    const auto moved = readFrame(device, renderer, camera, boxes);
    const auto center = pixel(moved, 64, 32, 32);
    expect(center[0] < 30 && center[2] < 40, "Changing the camera did not change the image");
}

void testDepth(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    ursa::Camera camera;
    camera.position = {0, 0, 4};
    camera.target = {};
    const ursa::Vector<ursa::BoxInstance> boxes{
        {{{0, 0, 0}, {1, 1, 1}}, {1, 0, 0, 1}},
        {{{0, 0, -1}, {1, 1, 1}}, {0, 1, 0, 1}},
    };
    const auto image = readFrame(device, renderer, camera, boxes);
    const auto center = pixel(image, 64, 32, 32);
    expect(center[0] > 100 && center[1] < 30, "Farther instance covered the nearer instance");
    const auto wide = readFrame(device, renderer, camera, boxes, 80, 40);
    const auto wideCenter = pixel(wide, 80, 40, 20);
    expect(wideCenter[0] > 100 && wideCenter[1] < 30, "Rendering failed after target resize");
}

void testModelMatrices(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    ursa::Camera camera;
    camera.position = {0, 0, 4};
    camera.target = {};
    const ursa::Vector<ursa::BoxInstance> legacy{{{{0, 0, 0}, {0.7f, 0.7f, 0.7f}}, {1, 0, 0, 1}}};
    const auto oldImage = readFrame(device, renderer, camera, legacy);
    const auto newImage = readFrameWith(device, [&](const ursa::rhi::TexturePtr &color) {
        return renderer.renderOffscreenModels(color, camera.viewProjection(1.0f),
                                              {{{legacy[0].transform.matrix()}, legacy[0].color}});
    });
    expect(pixel(oldImage, 64, 32, 32) == pixel(newImage, 64, 32, 32), "Model-matrix path changed the legacy result");

    const auto parent =
        ursa::Mat4::translation({1, 0, 0}) * ursa::Mat4::rotation(ursa::Quat::fromAxisAngle({0, 1, 0}, 3.14159265f));
    const auto child = ursa::Mat4::translation({1, 0, 0}) * ursa::Mat4::scale({0.7f, 0.7f, 0.7f});
    const auto rotated = readFrameWith(device, [&](const ursa::rhi::TexturePtr &color) {
        return renderer.renderOffscreenModels(color, camera.viewProjection(1.0f), {{parent * child, {0, 1, 0, 1}}});
    });
    const auto center = pixel(rotated, 64, 32, 32);
    expect(center[1] > 100 && center[0] < 30, "Rotated parent did not place its child at the center");
}

void testSceneCollection(const ursa::rhi::DevicePtr &device, ursa::Renderer3D &renderer) {
    ursa::Scene scene;
    const auto cameraNode = take(scene.createNode());
    ursa::LocalTransform cameraTransform;
    cameraTransform.position = {0, 0, 4};
    done(scene.setLocalTransform(cameraNode, cameraTransform));
    const auto camera = take(scene.addComponent<ursa::CameraComponent>(cameraNode));
    const auto parent = take(scene.createNode());
    ursa::LocalTransform parentTransform;
    parentTransform.position = {1, 0, 0};
    parentTransform.rotation = ursa::Quat::fromAxisAngle({0, 1, 0}, 3.14159265f);
    done(scene.setLocalTransform(parent, parentTransform));
    const auto child = take(scene.createNode(parent));
    ursa::LocalTransform childTransform;
    childTransform.position = {1, 0, 0};
    childTransform.scale = {0.7f, 0.7f, 0.7f};
    done(scene.setLocalTransform(child, childTransform));
    take(scene.addComponent<ursa::BoxRenderComponent>(child, ursa::Color{0, 1, 0, 1}));
    expect(!scene.collectRenderData(1.0f) && scene.collectRenderData(1.0f).error.find("camera") != ursa::String::npos,
           "Missing active camera did not return a diagnostic error");
    done(scene.setActiveCamera(camera));
    const auto data = take(scene.collectRenderData(1.0f));
    expect(data.boxes.size() == 1, "Scene did not collect its child box");
    const auto visible = readFrameWith(device, [&](const ursa::rhi::TexturePtr &color) {
        return renderer.renderOffscreenModels(color, data.viewProjection, data.boxes);
    });
    expect(pixel(visible, 64, 32, 32)[1] > 100, "Scene world transform or color was not rendered");
    done(scene.setEnabled(parent, false));
    const auto hiddenData = take(scene.collectRenderData(1.0f));
    expect(hiddenData.boxes.empty(), "Disabled parent still produced a box");
    const auto hidden = readFrameWith(device, [&](const ursa::rhi::TexturePtr &color) {
        return renderer.renderOffscreenModels(color, hiddenData.viewProjection, hiddenData.boxes);
    });
    expect(pixel(hidden, 64, 32, 32)[1] < 30, "Disabled box remained visible");
    done(scene.setComponentEnabled(camera, false));
    expect(!scene.collectRenderData(1.0f), "Disabled camera was accepted");
}
} // namespace

int main() {
    try {
        ursa::rhi::DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(ursa::rhi::createDevice(desc));
        auto renderer = take(ursa::Renderer3D::create(device));
        testInstances(device, *renderer);
        testDepth(device, *renderer);
        testModelMatrices(device, *renderer);
        testSceneCollection(device, *renderer);
        renderer.reset();
        done(device->waitIdle());
        std::cout << "3D renderer GPU tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "3D renderer GPU test failed: " << error.what() << '\n';
        return 1;
    }
}
