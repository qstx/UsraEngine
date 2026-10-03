#include <scene/EditorScenePreview.hpp>

namespace ursa::editor {
Result<std::unique_ptr<EditorScenePreview>> EditorScenePreview::create(rhi::DevicePtr device) {
    auto renderer = Renderer3D::create(std::move(device));
    if (!renderer)
        return Result<std::unique_ptr<EditorScenePreview>>::failure(renderer.error);
    return Result<std::unique_ptr<EditorScenePreview>>::success(
        std::unique_ptr<EditorScenePreview>(new EditorScenePreview(std::move(renderer.value))));
}

Result<void> EditorScenePreview::record(const Scene &scene, const rhi::CommandListPtr &command,
                                         const rhi::TexturePtr &color, rhi::ResourceState initialState,
                                         rhi::ResourceState finalState) {
    if (!color || !color->desc.width || !color->desc.height)
        return Result<void>::failure("Editor preview needs a nonzero color target");
    const float aspect = static_cast<float>(color->desc.width) / static_cast<float>(color->desc.height);
    auto frame = scene.collectPreviewRenderData(camera_, aspect);
    if (!frame)
        return Result<void>::failure(frame.error);
    return renderer_->recordOffscreenModels(command, color, initialState, finalState, frame.value.viewProjection,
                                            frame.value.boxes, frame.value.lighting, frame.value.cameraPosition);
}

Result<void> EditorScenePreview::recordGame(const Scene &scene, const rhi::CommandListPtr &command,
                                             const rhi::TexturePtr &color, rhi::ResourceState initialState,
                                             rhi::ResourceState finalState) {
    if (!color || !color->desc.width || !color->desc.height)
        return Result<void>::failure("Game preview needs a nonzero color target");
    const float aspect = static_cast<float>(color->desc.width) / static_cast<float>(color->desc.height);
    auto frame = scene.collectRenderData(aspect);
    if (!frame)
        return Result<void>::failure(frame.error);
    return renderer_->recordOffscreenModels(command, color, initialState, finalState, frame.value.viewProjection,
                                            frame.value.boxes, frame.value.lighting, frame.value.cameraPosition);
}
} // namespace ursa::editor
