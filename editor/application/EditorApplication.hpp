#pragma once

#include <core/Result.hpp>
#include <rhi/RHI.hpp>
#include <serialization/ComponentRegistry.hpp>
#include <window/Window.hpp>
#include <ui/ImGuiBridge.hpp>
#include <workspace/EditorDocuments.hpp>
#include <assets/MaterialPreview.hpp>
#include <scene/EditorHierarchy.hpp>
#include <scene/EditorScenePreview.hpp>
#include <scene/EditorGizmo.hpp>
#include <scene/ComponentEditors.hpp>
#include <application/PlaySession.hpp>
#include <array>
#include <functional>

namespace ursa::editor {
struct EditorProject {
    ComponentRegistry components;
    ComponentEditors componentEditors;
    std::function<Result<void>(ComponentLoadContext &)> editContextSetup;
    PlayContextSetup playContextSetup;
};

using EditorProjectSetup = std::function<Result<void>(EditorProject &)>;

class EditorApplication {
public:
    explicit EditorApplication(EditorProjectSetup setup = {}) : projectSetup_(std::move(setup)) {
    }

    virtual ~EditorApplication() = default;

    Result<void> run(uint32_t width = 1280, uint32_t height = 720, const String &title = "Ursa Editor",
                     bool visible = true, uint32_t frameLimit = 0);

    Result<void> openProject(const std::filesystem::path &root);

protected:
    virtual Result<void> onStart() {
        return {};
    }

    virtual Result<void> onFrame(float deltaSeconds) {
        (void)deltaSeconds;
        return {};
    }

    virtual Result<void> onGui();

    virtual void onStop() {
    }

    std::shared_ptr<Window> window_;
    rhi::DevicePtr device_;
    rhi::SwapchainPtr swapchain_;

    ImGuiBridge &gui() {
        return *gui_;
    }

    EditorProject &project() {
        return *project_;
    }

    ProjectWorkspace *workspace() const {
        return workspace_.get();
    }

    EditorDocuments *documents() const {
        return documents_.get();
    }

private:
    EditorProjectSetup projectSetup_;
    std::unique_ptr<EditorProject> project_;
    std::unique_ptr<ImGuiBridge> gui_;
    std::unique_ptr<MaterialPreview> materialPreview_;
    std::unique_ptr<EditorScenePreview> scenePreview_;
    EditorHierarchy hierarchy_;
    rhi::TexturePtr scenePreviewColor_;
    rhi::SamplerPtr scenePreviewSampler_;
    ImTextureID scenePreviewId_ = ImTextureID_Invalid;
    bool scenePreviewRendered_ = false;
    ResourceId displayedSceneId_;
    GizmoOperation gizmoOperation_ = GizmoOperation::Translate;
    PlaySession play_;
    bool viewportFocused_ = false;
    std::unique_ptr<ProjectWorkspace> workspace_;
    std::unique_ptr<EditorDocuments> documents_;
    std::array<char, 256> assetNameInput_{};
    String workspaceError_;
    String inspectorError_;
    bool exitRequested_ = false;
    bool dockLayoutInitialized_ = false;
};
} // namespace ursa::editor
