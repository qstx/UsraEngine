#include <core/StringInterop.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <scene/Scene.hpp>
#include <iostream>
#include <memory>
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
        ursa::rhi::DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(ursa::rhi::createDevice(desc));
        auto renderer = take(ursa::Renderer3D::create(device));
        ursa::Scene scene;
        auto cameraNode = take(scene.createNode());
        ursa::LocalTransform cameraPose;
        cameraPose.position = {0.0f, 0.0f, 5.0f};
        done(scene.setLocalTransform(cameraNode, cameraPose));
        done(scene.setActiveCamera(take(scene.addComponent<ursa::CameraComponent>(cameraNode))));
        auto boxNode = take(scene.createNode());
        take(scene.addComponent<ursa::BoxRenderComponent>(boxNode));
        auto material = std::make_shared<ursa::MaterialInstance>(ursa::MaterialType::Unlit);
        done(material->setBaseColor({1.0f, 0.0f, 0.0f, 1.0f}));
        scene.findComponent<ursa::BoxRenderComponent>(boxNode)->material = material;
        auto frame = take(scene.collectRenderData(1.0f));
        auto color = take(device->createTexture(
            {64, 64, ursa::rhi::Format::RGBA8Unorm,
             ursa::rhi::TextureUsage::ColorAttachment | ursa::rhi::TextureUsage::TransferSource}));
        auto submitted = renderer->renderOffscreenModels(color, frame.viewProjection, frame.boxes, frame.lighting,
                                                         frame.cameraPosition);
        done(take(std::move(submitted))->wait());
        auto readback = take(device->createBuffer(
            {64 * 64 * 4, ursa::rhi::BufferUsage::TransferDestination, ursa::rhi::Memory::Readback}));
        auto command = take(device->createCommandList());
        done(command->begin());
        done(command->barrier(readback, ursa::rhi::ResourceState::Undefined,
                              ursa::rhi::ResourceState::TransferDestination));
        done(command->copyTextureToBuffer(color, readback));
        done(command->end());
        done(take(device->submit(command))->wait());
        uint8_t center[4]{};
        done(readback->read((32 * 64 + 32) * 4, center, sizeof(center)));
        if (center[0] < 240 || center[1] > 20 || center[2] > 20)
            throw std::runtime_error("Material scene smoke pixel mismatch");
        renderer.reset();
        done(device->waitIdle());
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
