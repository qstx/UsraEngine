#pragma once

#include <renderer3d/Renderer3D.hpp>
#include <ui/ImGuiBridge.hpp>

namespace ursa::editor {
class MaterialPreview final {
public:
    static Result<std::unique_ptr<MaterialPreview>> create(rhi::DevicePtr device, ImGuiBridge &gui,
                                                            uint32_t size = 256);

    ~MaterialPreview();

    Result<void> render(const std::shared_ptr<MaterialInstance> &material);

    ImTextureID imageId() const {
        return imageId_;
    }

    const rhi::TexturePtr &colorTexture() const {
        return color_;
    }

private:
    MaterialPreview(rhi::DevicePtr device, ImGuiBridge &gui, std::unique_ptr<Renderer3D> renderer,
                    rhi::TexturePtr color, rhi::SamplerPtr sampler, ImTextureID imageId)
        : device_(std::move(device)), gui_(gui), renderer_(std::move(renderer)), color_(std::move(color)),
          sampler_(std::move(sampler)), imageId_(imageId) {
    }

    rhi::DevicePtr device_;
    ImGuiBridge &gui_;
    std::unique_ptr<Renderer3D> renderer_;
    rhi::TexturePtr color_;
    rhi::SamplerPtr sampler_;
    ImTextureID imageId_ = ImTextureID_Invalid;
    bool rendered_ = false;
};
} // namespace ursa::editor
