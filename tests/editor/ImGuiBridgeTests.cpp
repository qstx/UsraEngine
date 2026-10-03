#include <ui/ImGuiBridge.hpp>
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

constexpr uint32_t width = 64;
constexpr uint32_t height = 64;
int clipboardReads = 0;

void testImagesFontsAndClipping(const DevicePtr &device) {
    auto bridge = take(editor::ImGuiBridge::create(device));
    auto sampler = take(device->createSampler({false, false}));
    auto red = take(device->createTexture({1, 1, Format::RGBA8Unorm,
                                           TextureUsage::Sampled | TextureUsage::TransferDestination}));
    auto green = take(device->createTexture({1, 1, Format::RGBA8Unorm,
                                             TextureUsage::Sampled | TextureUsage::TransferDestination}));
    constexpr std::array<uint8_t, 4> redPixel{255, 0, 0, 255};
    constexpr std::array<uint8_t, 4> greenPixel{0, 255, 0, 255};
    auto redUpload = take(device->createBuffer({4, BufferUsage::TransferSource, Memory::Upload}));
    auto greenUpload = take(device->createBuffer({4, BufferUsage::TransferSource, Memory::Upload}));
    done(redUpload->write(0, redPixel.data(), redPixel.size()));
    done(greenUpload->write(0, greenPixel.data(), greenPixel.size()));
    auto upload = take(device->createCommandList());
    done(upload->begin());
    done(upload->barrier(red, ResourceState::Undefined, ResourceState::TransferDestination));
    done(upload->barrier(green, ResourceState::Undefined, ResourceState::TransferDestination));
    done(upload->copyBufferToTexture(redUpload, red));
    done(upload->copyBufferToTexture(greenUpload, green));
    done(upload->barrier(red, ResourceState::TransferDestination, ResourceState::ShaderRead));
    done(upload->barrier(green, ResourceState::TransferDestination, ResourceState::ShaderRead));
    done(upload->end());
    done(take(device->submit(upload))->wait());
    const auto redId = take(bridge->registerTexture(red, sampler));
    const auto greenId = take(bridge->registerTexture(green, sampler));

    done(bridge->beginFrame(1.f / 60.f, width, height));
    auto *draw = ImGui::GetBackgroundDrawList();
    draw->AddImage(redId, ImVec2(2, 2), ImVec2(18, 18));
    draw->AddImage(greenId, ImVec2(22, 2), ImVec2(38, 18));
    draw->PushClipRect(ImVec2(46, 2), ImVec2(54, 18));
    draw->AddRectFilled(ImVec2(42, 2), ImVec2(60, 18), IM_COL32(0, 0, 255, 255));
    draw->PopClipRect();
    draw->AddText(ImVec2(2, 30), IM_COL32(255, 255, 255, 255), "A");
    done(bridge->endFrame());

    auto color = take(device->createTexture({width, height, Format::RGBA8Unorm,
                                             TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
    auto readback = take(device->createBuffer({width * height * 4, BufferUsage::TransferDestination,
                                               Memory::Readback}));
    auto command = take(device->createCommandList());
    done(command->begin());
    done(command->barrier(color, ResourceState::Undefined, ResourceState::ColorAttachment));
    RenderTarget target;
    target.color = color;
    target.colorValue[0] = 0;
    target.colorValue[1] = 0;
    target.colorValue[2] = 0;
    target.colorValue[3] = 1;
    done(command->beginRendering(target));
    done(bridge->record(command, Format::RGBA8Unorm, width, height));
    done(command->endRendering());
    done(command->barrier(color, ResourceState::ColorAttachment, ResourceState::TransferSource));
    done(command->barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
    done(command->copyTextureToBuffer(color, readback));
    done(command->end());
    done(take(device->submit(command))->wait());

    std::array<uint8_t, width * height * 4> pixels{};
    done(readback->read(0, pixels.data(), pixels.size()));
    const auto channel = [&](uint32_t x, uint32_t y, uint32_t c) -> uint8_t {
        return pixels[(y * width + x) * 4 + c];
    };
    expect(channel(8, 8, 0) > 240 && channel(8, 8, 1) < 10, "First ImGui image is not red");
    expect(channel(28, 8, 1) > 240 && channel(28, 8, 0) < 10, "Second ImGui image is not green");
    expect(channel(50, 8, 2) > 240, "Clipped ImGui panel did not render");
    expect(channel(44, 8, 2) < 10 && channel(57, 8, 2) < 10, "ImGui scissor leaked outside clip");
    bool textVisible = false;
    for (uint32_t y = 30; y < 50; ++y)
        for (uint32_t x = 2; x < 20; ++x)
            textVisible |= channel(x, y, 0) > 30;
    expect(textVisible, "ImGui font glyph did not render");
    bridge->unregisterTexture(redId);
    bridge->unregisterTexture(greenId);
}

void testTextFieldPaste(const DevicePtr &device) {
    auto window = take(Window::create(640, 480, "ImGui clipboard test", false));
    auto bridge = take(editor::ImGuiBridge::create(device, window));
    ImGui::SetCurrentContext(bridge->context());
    auto &platform = ImGui::GetPlatformIO();
    expect(platform.Platform_ClipboardUserData == bridge.get() &&
               platform.Platform_GetClipboardTextFn && platform.Platform_SetClipboardTextFn,
           "ImGui platform clipboard was not connected to the window");
    platform.Platform_GetClipboardTextFn = [](ImGuiContext *) { ++clipboardReads; return "G:\\Assets\\Demo"; };

    char path[1024]{};
    done(bridge->beginFrame(1.f / 60.f, 640, 480));
    ImGui::Begin("Paste test");
    ImGui::SetKeyboardFocusHere();
    ImGui::InputText("Asset root", path, sizeof(path));
    ImGui::End();
    done(bridge->endFrame());

    done(bridge->beginFrame(1.f / 60.f, 640, 480));
    ImGui::Begin("Paste test");
    ImGui::InputText("Asset root", path, sizeof(path));
    expect(ImGui::IsItemActive(), "Asset root did not receive keyboard focus");
    ImGui::End();
    done(bridge->endFrame());

    done(bridge->beginFrame(1.f / 60.f, 640, 480));
    ImGui::Begin("Paste test");
    ImGui::InputText("Asset root", path, sizeof(path));
    ImGui::End();
    done(bridge->endFrame());

    WindowEvent control;
    control.type = WindowEventType::KeyPressed;
    control.key = Key::LeftControl;
    detail::WindowAccess::dispatch(*window, control);
    WindowEvent paste;
    paste.type = WindowEventType::KeyPressed;
    paste.key = Key::V;
    detail::WindowAccess::dispatch(*window, paste);
    done(bridge->beginFrame(1.f / 60.f, 640, 480));
    ImGui::Begin("Paste test");
    ImGui::InputText("Asset root", path, sizeof(path));
    const bool active = ImGui::IsItemActive();
    const bool controlDown = ImGui::GetIO().KeyCtrl;
    const bool vPressed = ImGui::IsKeyPressed(ImGuiKey_V);
    ImGui::End();
    done(bridge->endFrame());
    expect(active && controlDown && vPressed, "Ctrl+V did not reach the focused text field");
    expect(clipboardReads == 1 && String(path) == "G:\\Assets\\Demo",
           "Ctrl+V did not paste into Asset root");
}
} // namespace

int main() {
    try {
        DeviceDesc desc;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(createDevice(desc));
        testImagesFontsAndClipping(device);
        testTextFieldPaste(device);
        done(device->waitIdle());
        std::cout << "ImGui RHI bridge GPU test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
