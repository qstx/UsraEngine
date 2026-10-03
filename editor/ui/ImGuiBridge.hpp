#pragma once

#include <rhi/RHI.hpp>
#include <imgui.h>
#include <memory>
#include <unordered_map>

namespace ursa::editor {
// Owns an ImGui context. The caller must finish the GPU submission before starting another frame.
class ImGuiBridge final {
public:
    static Result<std::unique_ptr<ImGuiBridge>> create(rhi::DevicePtr device,
                                                       std::shared_ptr<Window> window = {});

    ~ImGuiBridge();

    ImGuiBridge(const ImGuiBridge &) = delete;

    ImGuiBridge &operator=(const ImGuiBridge &) = delete;

    Result<void> beginFrame(float deltaSeconds, uint32_t width, uint32_t height);

    Result<void> endFrame();

    // Called within an existing color rendering pass. The pass target must match colorFormat.
    Result<void> record(const rhi::CommandListPtr &command, rhi::Format colorFormat,
                        uint32_t framebufferWidth, uint32_t framebufferHeight);

    Result<ImTextureID> registerTexture(rhi::TexturePtr texture, rhi::SamplerPtr sampler);

    void unregisterTexture(ImTextureID id);

    ImGuiContext *context() const {
        return context_;
    }

private:
    explicit ImGuiBridge(rhi::DevicePtr device, std::shared_ptr<Window> window);

    Result<void> initialize();

    Result<void> updateAtlas();

    Result<void> uploadAtlas(ImTextureData *data);

    static const char *getClipboardText(ImGuiContext *context);

    static void setClipboardText(ImGuiContext *context, const char *text);

    rhi::DevicePtr device_;
    std::shared_ptr<Window> window_;
    uint64_t subscription_ = 0;
    ImGuiContext *context_ = nullptr;
    String clipboardText_;
    bool leftControl_ = false;
    bool rightControl_ = false;
    bool leftShift_ = false;
    bool rightShift_ = false;
    bool leftAlt_ = false;
    bool rightAlt_ = false;
    bool leftSuper_ = false;
    bool rightSuper_ = false;
    rhi::LayoutPtr layout_;
    rhi::ShaderPtr vertexShader_;
    rhi::ShaderPtr fragmentShader_;
    rhi::SamplerPtr atlasSampler_;
    rhi::BufferPtr vertexBuffer_;
    rhi::BufferPtr indexBuffer_;
    std::unordered_map<int, rhi::PipelinePtr> pipelines_;
    std::unordered_map<ImTextureID, rhi::BindGroupPtr> groups_;
    std::unordered_map<ImTextureData *, ImTextureID> atlasIds_;
    ImTextureID nextTextureId_ = 1;
};
} // namespace ursa::editor
