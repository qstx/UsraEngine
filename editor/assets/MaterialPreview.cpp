#include <assets/MaterialPreview.hpp>

namespace ursa::editor {
Result<std::unique_ptr<MaterialPreview>> MaterialPreview::create(rhi::DevicePtr device, ImGuiBridge &gui,
                                                                   uint32_t size) {
    if (!device || !size)
        return Result<std::unique_ptr<MaterialPreview>>::failure("Material preview needs a device and size");
    auto renderer = Renderer3D::create(device);
    if (!renderer)
        return Result<std::unique_ptr<MaterialPreview>>::failure(renderer.error);
    auto color = device->createTexture({size, size, rhi::Format::RGBA8Unorm,
                                        rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled |
                                            rhi::TextureUsage::TransferSource});
    if (!color)
        return Result<std::unique_ptr<MaterialPreview>>::failure(color.error);
    auto sampler = device->createSampler({true, false});
    if (!sampler)
        return Result<std::unique_ptr<MaterialPreview>>::failure(sampler.error);
    auto imageId = gui.registerTexture(color.value, sampler.value);
    if (!imageId)
        return Result<std::unique_ptr<MaterialPreview>>::failure(imageId.error);
    return Result<std::unique_ptr<MaterialPreview>>::success(std::unique_ptr<MaterialPreview>(
        new MaterialPreview(std::move(device), gui, std::move(renderer.value), std::move(color.value),
                             std::move(sampler.value), imageId.value)));
}

MaterialPreview::~MaterialPreview() {
    gui_.unregisterTexture(imageId_);
}

Result<void> MaterialPreview::render(const std::shared_ptr<MaterialInstance> &material) {
    if (!material)
        return Result<void>::failure("Cannot preview an empty material");
    auto command = device_->createCommandList();
    if (!command)
        return Result<void>::failure(command.error);
    auto result = command.value->begin();
    if (!result)
        return result;
    Camera camera;
    camera.position = {2.5f, 1.8f, 3.5f};
    camera.target = {0, 0, 0};
    const Vector<ModelBoxInstance> boxes{{Mat4::identity(), {}, material}};
    SceneLighting lighting;
    lighting.ambientIntensity = 0.5f;
    lighting.directional.push_back({{-0.4f, -0.7f, -0.5f}, {}, 2.f});
    result = renderer_->recordOffscreenModels(command.value, color_,
                                               rendered_ ? rhi::ResourceState::ShaderRead
                                                         : rhi::ResourceState::Undefined,
                                               rhi::ResourceState::ShaderRead, camera.viewProjection(1.f), boxes,
                                               lighting, camera.position);
    if (result)
        result = command.value->end();
    if (!result)
        return result;
    auto submitted = device_->submit(command.value);
    if (!submitted)
        return Result<void>::failure(submitted.error);
    result = submitted.value->wait();
    if (result)
        rendered_ = true;
    return result;
}
} // namespace ursa::editor
