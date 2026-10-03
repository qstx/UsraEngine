#include <scene/EditorScenePreview.hpp>
#include <core/StringInterop.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa;
using namespace ursa::rhi;

template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
    return std::move(value.value);
}

void done(Result<void> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
}

void expect(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

class SpyComponent final : public Component {
public:
    explicit SpyComponent(int &starts, int &updates) : starts_(starts), updates_(updates) {
    }

    Result<void> onStart(const FrameContext &) override {
        ++starts_;
        return {};
    }

    Result<void> onUpdate(const FrameContext &) override {
        ++updates_;
        return {};
    }

private:
    int &starts_;
    int &updates_;
};
} // namespace

int main() {
    try {
        DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(createDevice(desc));
        auto preview = take(editor::EditorScenePreview::create(device));
        preview->camera().position = {0, 0, 5};
        preview->camera().target = {0, 0, 0};
        Scene scene;
        const auto node = take(scene.createNode());
        take(scene.addComponent<BoxRenderComponent>(node, Color{0.8f, 0.1f, 0.1f, 1.f}));
        int starts = 0;
        int updates = 0;
        take(scene.addComponent<SpyComponent>(node, starts, updates));
        expect(!scene.collectRenderData(1.f), "Game rendering should still require an active scene camera");
        constexpr uint32_t size = 64;
        auto color = take(device->createTexture({size, size, Format::RGBA8Unorm,
                                                 TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
        auto readback = take(device->createBuffer({size * size * 4, BufferUsage::TransferDestination,
                                                   Memory::Readback}));
        auto command = take(device->createCommandList());
        done(command->begin());
        done(preview->record(scene, command, color, ResourceState::Undefined, ResourceState::TransferSource));
        done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
        done(command->copyTextureToBuffer(color, readback));
        done(command->end());
        done(take(device->submit(command))->wait());
        std::array<uint8_t, size * size * 4> pixels{};
        done(readback->read(0, pixels.data(), pixels.size()));
        const size_t center = (size / 2 * size + size / 2) * 4;
        expect(pixels[center] > pixels[center + 1] * 2, "Preview camera did not render the box");
        expect(starts == 0 && updates == 0, "Edit preview invoked a component lifecycle callback");
        done(device->waitIdle());
        std::cout << "Editor camera preview test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
