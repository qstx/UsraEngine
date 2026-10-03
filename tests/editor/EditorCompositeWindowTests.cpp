#include <ui/ImGuiBridge.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <scene/EditorGizmo.hpp>
#include <application/PlaySession.hpp>
#include <core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa;
using namespace ursa::rhi;

template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(toStdString(result.error));
    return std::move(result.value);
}

void done(Result<void> result) {
    if (!result)
        throw std::runtime_error(toStdString(result.error));
}

void compositeFrame(const DevicePtr &device, const SwapchainPtr &swapchain, Renderer3D &renderer,
                    editor::ImGuiBridge &gui, const std::shared_ptr<Window> &window, Scene &selection,
                    NodeId selected, uint32_t previewSize, const Scene *game = nullptr) {
    window->pollEvents();
    auto preview = take(device->createTexture({previewSize, previewSize, Format::RGBA8Unorm,
                                                TextureUsage::ColorAttachment | TextureUsage::Sampled}));
    auto sampler = take(device->createSampler({true, false}));
    const auto image = take(gui.registerTexture(preview, sampler));
    done(gui.beginFrame(1.f / 60.f, window->framebufferWidth(), window->framebufferHeight()));
    auto *draw = ImGui::GetBackgroundDrawList();
    draw->AddImage(image, ImVec2(20, 20), ImVec2(20.f + previewSize, 20.f + previewSize));
    draw->AddText(ImVec2(20, 2), IM_COL32_WHITE, "Scene preview");
    Camera gizmoCamera;
    gizmoCamera.position = {0, 0, 5};
    gizmoCamera.target = {0, 0, 0};
    const auto gizmo = editor::drawGizmo(selection, selected, gizmoCamera,
                                          {20, 20, static_cast<float>(previewSize), static_cast<float>(previewSize)},
                                          editor::GizmoOperation::Translate);
    if (!gizmo)
        throw std::runtime_error(toStdString(gizmo.error));
    done(gui.endFrame());

    auto acquired = take(swapchain->acquire());
    if (!acquired.texture)
        throw std::runtime_error("Window unexpectedly minimized during compositing test");
    auto command = take(device->createCommandList());
    done(command->begin());
    Camera camera;
    camera.position = {0, 0, 5};
    if (game) {
        auto frame = take(game->collectRenderData(1.f));
        done(renderer.recordOffscreenModels(command, preview, ResourceState::Undefined, ResourceState::ShaderRead,
                                            frame.viewProjection, frame.boxes, frame.lighting, frame.cameraPosition));
    } else {
        const Vector<ModelBoxInstance> boxes{{Mat4::identity(), {0.9f, 0.2f, 0.1f, 1.f}, {}}};
        done(renderer.recordOffscreenModels(command, preview, ResourceState::Undefined, ResourceState::ShaderRead,
                                            camera.viewProjection(1.f), boxes, {}, camera.position));
    }
    done(command->barrier(acquired.texture, acquired.initialState, ResourceState::ColorAttachment));
    RenderTarget target;
    target.color = acquired.texture;
    done(command->beginRendering(target));
    done(gui.record(command, swapchain->format(), acquired.texture->desc.width, acquired.texture->desc.height));
    done(command->endRendering());
    done(command->barrier(acquired.texture, ResourceState::ColorAttachment, ResourceState::Present));
    done(command->end());
    const auto fence = take(device->submit(command, swapchain));
    done(swapchain->present());
    done(fence->wait());
    gui.unregisterTexture(image);
}
} // namespace

int main() {
    try {
        auto window = take(Window::create(640, 480, "Ursa Editor compositing", true));
        DeviceDesc desc;
        desc.window = window;
#ifndef NDEBUG
        desc.validation = true;
#endif
        auto device = take(createDevice(desc));
        auto swapchain = take(device->createSwapchain({}));
        auto renderer = take(Renderer3D::create(device));
        auto gui = take(editor::ImGuiBridge::create(device, window));
        Scene selection;
        const auto selected = take(selection.createNode());
        compositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 128);
        done(editor::applyGizmoWorldTransform(selection, selected, Mat4::translation({0.5f, 0, 0})));
        window->resize(800, 600);
        compositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 160);
        const auto root = std::filesystem::current_path() /
                          ("editor_window_play_" + toStdString(ResourceId::generate().toString()));
        std::filesystem::create_directory(root);
        auto resources = take(ResourceSession::open({root}));
        ComponentRegistry registry;
        done(registerBuiltinComponents(registry));
        auto draft = take(editor::SceneDraft::create(ResourceKind::Scene, registry, *resources));
        const NodeId gameRoot = draft->scene().rootNodes().front();
        auto cameraNode = take(draft->scene().createNode(gameRoot));
        LocalTransform cameraPose;
        cameraPose.position = {0, 0, 5};
        done(draft->scene().setLocalTransform(cameraNode, cameraPose));
        const auto cameraId = take(draft->scene().addComponent<CameraComponent>(cameraNode));
        done(draft->scene().setActiveCamera(cameraId));
        auto boxNode = take(draft->scene().createNode(gameRoot));
        take(draft->scene().addComponent<BoxRenderComponent>(boxNode, Color{0.2f, 0.8f, 0.4f, 1.f}));
        editor::PlaySession play;
        done(play.start(*draft, ResourceId::generate(), registry, *resources));
        done(play.tick(1.f / 30.f, window.get(), {}));
        window->minimize();
        window->restore();
        compositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 160, play.scene());
        play.stop();
        compositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 160, &draft->scene());
        std::filesystem::remove_all(root);
        done(device->waitIdle());
        gui.reset();
        renderer.reset();
        swapchain.reset();
        device.reset();
        window.reset();
        std::cout << "Editor scene and UI compositing window test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
