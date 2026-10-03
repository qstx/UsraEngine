#pragma once

#include <renderer3d/Renderer3D.hpp>
#include <scene/Scene.hpp>

namespace ursa::editor {
class EditorScenePreview final {
public:
    static Result<std::unique_ptr<EditorScenePreview>> create(rhi::DevicePtr device);

    Camera &camera() {
        return camera_;
    }

    const Camera &camera() const {
        return camera_;
    }

    Result<void> record(const Scene &scene, const rhi::CommandListPtr &command, const rhi::TexturePtr &color,
                        rhi::ResourceState initialState, rhi::ResourceState finalState);

    Result<void> recordGame(const Scene &scene, const rhi::CommandListPtr &command, const rhi::TexturePtr &color,
                            rhi::ResourceState initialState, rhi::ResourceState finalState);

private:
    explicit EditorScenePreview(std::unique_ptr<Renderer3D> renderer) : renderer_(std::move(renderer)) {
    }

    std::unique_ptr<Renderer3D> renderer_;
    Camera camera_;
};
} // namespace ursa::editor
