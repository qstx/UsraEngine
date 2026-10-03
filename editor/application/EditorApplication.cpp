#include <application/EditorApplication.hpp>
#include <ui/ImGuiBridge.hpp>
#include <assets/TextureDraft.hpp>
#include <assets/MaterialDraft.hpp>
#include <assets/SceneDraft.hpp>
#include <core/StringInterop.hpp>
#include <core/Log.hpp>
#include <vfs/AssetRoots.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace ursa::editor {
namespace {
Result<VirtualPath> projectAssetPath(const char *input) {
    const String name = toUrsaString(std::string(input));
    if (name.empty())
        return Result<VirtualPath>::failure("Enter an asset path such as snake/level.ursascene");
    return VirtualPath::parse(String(projectResourceScheme) + ":" + name);
}
} // namespace

Result<void> EditorApplication::openProject(const std::filesystem::path &root) {
    if (play_.active())
        return Result<void>::failure("Stop Play mode before changing projects");
    if (documents_ && documents_->current() && documents_->current()->dirty())
        return Result<void>::failure("Close or save the dirty document before changing projects");
    Vector<ResourceMount> engineMounts;
    auto engineRoots = resolveEngineAssetRoots();
    if (!engineRoots)
        return Result<void>::failure(engineRoots.error);
    for (const std::filesystem::path &root : engineRoots.value)
        engineMounts.push_back(ResourceMount{engineResourceScheme, root, MountAccess::ReadOnly});
    auto opened = ProjectWorkspace::open(root, engineMounts);
    if (!opened)
        return Result<void>::failure(opened.error);
    auto documents = EditorDocuments::create(*opened.value);
    if (!documents)
        return Result<void>::failure(documents.error);
    documents_ = std::move(documents.value);
    workspace_ = std::move(opened.value);
    workspaceError_.clear();
    URSA_LOG_INFO("Editor", "Opened project " + pathToUrsaString(workspace_->projectRoot()));
    return {};
}

Result<void> EditorApplication::onGui() {
    if (ImGui::BeginMainMenuBar()) {
        ImGui::TextUnformatted("Ursa Editor");
        ImGui::Separator();
        const auto *current = documents_ ? documents_->current() : nullptr;
        const bool canRun = current && current->kind() == ResourceKind::Scene && !documents_->promptPending();
        ImGui::BeginDisabled(!play_.active() && !canRun);
        if (ImGui::Button(play_.active() ? "Stop" : "Run")) {
            if (play_.active()) {
                play_.stop();
                documents_->setSaveBlocked(false);
                viewportFocused_ = false;
            } else {
                auto draft = current->payload<SceneDraft>();
                if (draft) {
                    auto started = play_.start(*draft, current->id(), project_->components,
                                               documents_->session(), project_->playContextSetup);
                    inspectorError_ = started ? String{} : started.error;
                    if (started)
                        documents_->setSaveBlocked(true);
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (workspace_) {
            const auto name = pathToUrsaString(workspace_->projectRoot().filename());
            ImGui::Text("Project: %s", name.c_str());
        }
        ImGui::EndMainMenuBar();
    }
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
    if (!dockLayoutInitialized_) {
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
        ImGuiID projectDock = 0;
        ImGuiID upperDock = 0;
        ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.25f, &projectDock, &upperDock);
        ImGuiID hierarchyDock = 0;
        ImGuiID workspaceDock = 0;
        ImGui::DockBuilderSplitNode(upperDock, ImGuiDir_Left, 0.20f, &hierarchyDock, &workspaceDock);
        ImGuiID inspectorDock = 0;
        ImGuiID viewportDock = 0;
        ImGui::DockBuilderSplitNode(workspaceDock, ImGuiDir_Right, 0.30f, &inspectorDock, &viewportDock);
        ImGui::DockBuilderDockWindow("Project", projectDock);
        ImGui::DockBuilderDockWindow("Hierarchy", hierarchyDock);
        ImGui::DockBuilderDockWindow("Viewport", viewportDock);
        ImGui::DockBuilderDockWindow("Material Preview", viewportDock);
        ImGui::DockBuilderDockWindow("Inspector", inspectorDock);
        ImGui::DockBuilderFinish(dockspace);
        dockLayoutInitialized_ = true;
    }
    ImGui::Begin("Project");
    ImGui::BeginDisabled(play_.active());
    if (workspace_) {
        const auto root = pathToUrsaString(workspace_->projectRoot());
        ImGui::TextWrapped("Assets: %s", root.c_str());
    }
    if (!workspaceError_.empty())
        ImGui::TextWrapped("%s", workspaceError_.c_str());
    if (workspace_) {
        if (ImGui::Button("Refresh assets")) {
            auto refreshed = workspace_->refresh();
            workspaceError_ = refreshed ? String{} : refreshed.error;
        }
        if (documents_) {
            ImGui::InputText("New asset", assetNameInput_.data(), assetNameInput_.size());
            if (ImGui::Button("New texture")) {
                auto target = projectAssetPath(assetNameInput_.data());
                auto created = target ? newTextureDocument(target.value)
                                      : Result<std::unique_ptr<EditorDocument>>::failure(target.error);
                if (created) {
                    auto opened = documents_->requestOpen(std::move(created.value));
                    workspaceError_ = opened ? String{} : opened.error;
                } else
                    workspaceError_ = created.error;
            }
            ImGui::SameLine();
            if (ImGui::Button("New Unlit")) {
                auto target = projectAssetPath(assetNameInput_.data());
                auto created = target ? newMaterialDocument(target.value, MaterialType::Unlit, *workspace_)
                                      : Result<std::unique_ptr<EditorDocument>>::failure(target.error);
                if (created) {
                    auto opened = documents_->requestOpen(std::move(created.value));
                    workspaceError_ = opened ? String{} : opened.error;
                } else
                    workspaceError_ = created.error;
            }
            ImGui::SameLine();
            if (ImGui::Button("New PBR")) {
                auto target = projectAssetPath(assetNameInput_.data());
                auto created = target ? newMaterialDocument(target.value, MaterialType::PbrMetallicRoughness,
                                                            *workspace_)
                                      : Result<std::unique_ptr<EditorDocument>>::failure(target.error);
                if (created) {
                    auto opened = documents_->requestOpen(std::move(created.value));
                    workspaceError_ = opened ? String{} : opened.error;
                } else
                    workspaceError_ = created.error;
            }
            ImGui::SameLine();
            const bool newScene = ImGui::Button("New Scene");
            ImGui::SameLine();
            const bool newPrefab = ImGui::Button("New Prefab");
            if (newScene || newPrefab) {
                const ResourceKind kind = newScene ? ResourceKind::Scene : ResourceKind::Prefab;
                auto target = projectAssetPath(assetNameInput_.data());
                auto created = target ? newSceneDocument(target.value, kind, project_->components,
                                                          documents_->session(), project_->editContextSetup)
                                      : Result<std::unique_ptr<EditorDocument>>::failure(target.error);
                if (created) {
                    auto opened = documents_->requestOpen(std::move(created.value));
                    workspaceError_ = opened ? String{} : opened.error;
                } else
                    workspaceError_ = created.error;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save") && documents_->current()) {
                auto saved = documents_->saveCurrent();
                workspaceError_ = saved ? String{} : saved.error;
            }
            ImGui::SameLine();
            if (ImGui::Button("Close document")) {
                auto closed = documents_->requestClose();
                workspaceError_ = closed ? String{} : closed.error;
            }
            if (documents_->current())
                ImGui::Text("Open: %s%s", documents_->current()->path().toString().c_str(),
                            documents_->current()->dirty() ? " *" : "");
        }
        for (const auto &entry : workspace_->resources()) {
            const bool writable = static_cast<bool>(workspace_->writablePath(entry.location.path));
            ImGui::PushID(entry.id.toString().c_str());
            ImGui::TextUnformatted(writable ? "[Project]" : "[Engine]");
            ImGui::SameLine();
            const auto name = entry.location.path.toString();
            if (ImGui::Selectable(name.c_str()) && writable && documents_) {
                if (entry.location.kind == ResourceKind::Texture) {
                    auto opened = openTextureDocument(entry.location.path, workspace_->index());
                    if (opened) {
                        auto switched = documents_->requestOpen(std::move(opened.value));
                        workspaceError_ = switched ? String{} : switched.error;
                    } else
                        workspaceError_ = opened.error;
                } else if (entry.location.kind == ResourceKind::Material) {
                    auto opened = openMaterialDocument(entry.location.path, documents_->session(), *workspace_);
                    if (opened) {
                        auto switched = documents_->requestOpen(std::move(opened.value));
                        workspaceError_ = switched ? String{} : switched.error;
                    } else
                        workspaceError_ = opened.error;
                } else if (entry.location.kind == ResourceKind::Scene ||
                           entry.location.kind == ResourceKind::Prefab) {
                    auto opened = openSceneDocument(entry.location.path, entry.location.kind, project_->components,
                                                     documents_->session(), project_->editContextSetup);
                    if (opened) {
                        auto switched = documents_->requestOpen(std::move(opened.value));
                        workspaceError_ = switched ? String{} : switched.error;
                    } else
                        workspaceError_ = opened.error;
                } else
                    workspaceError_ = "This asset type is not yet editable";
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", entry.id.toString().c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();
    ImGui::End();
    if (documents_ && documents_->current() && documents_->current()->kind() == ResourceKind::Texture) {
        auto inspected = drawTextureInspector(*documents_->current(), inspectorError_);
        if (!inspected)
            workspaceError_ = inspected.error;
    }
    if (documents_ && documents_->current() && documents_->current()->kind() == ResourceKind::Material) {
        auto inspected = drawMaterialInspector(*documents_->current(), *documents_, *workspace_, inspectorError_);
        if (!inspected)
            workspaceError_ = inspected.error;
        auto draft = documents_->current()->payload<MaterialDraft>();
        if (draft && materialPreview_) {
            auto rendered = materialPreview_->render(draft->materialPtr());
            if (!rendered)
                inspectorError_ = rendered.error;
            ImGui::Begin("Material Preview");
            if (rendered)
                ImGui::Image(materialPreview_->imageId(), ImVec2(256, 256));
            ImGui::End();
        }
    }
    if (documents_ && documents_->current() &&
        (documents_->current()->kind() == ResourceKind::Scene ||
         documents_->current()->kind() == ResourceKind::Prefab)) {
        if (displayedSceneId_ != documents_->current()->id()) {
            hierarchy_.clearSelection();
            displayedSceneId_ = documents_->current()->id();
        }
        auto draft = documents_->current()->payload<SceneDraft>();
        if (draft) {
            ImGui::Begin("Viewport");
            if (ImGui::Button("Translate"))
                gizmoOperation_ = GizmoOperation::Translate;
            ImGui::SameLine();
            if (ImGui::Button("Rotate"))
                gizmoOperation_ = GizmoOperation::Rotate;
            ImGui::SameLine();
            if (ImGui::Button("Scale"))
                gizmoOperation_ = GizmoOperation::Scale;
            ImGui::End();
            if (play_.active()) {
                ImGui::BeginDisabled();
                auto hierarchyResult = hierarchy_.draw(*play_.scene(), project_->components);
                ImGui::EndDisabled();
                if (!hierarchyResult)
                    inspectorError_ = hierarchyResult.error;
                ImGui::Begin("Inspector");
                ImGui::TextUnformatted("Play mode: scene changes are temporary");
                ImGui::End();
            } else {
                auto inspected = drawSceneInspector(*documents_->current(), hierarchy_, *documents_, *workspace_,
                                                     project_->components, project_->componentEditors, inspectorError_);
                if (!inspected)
                    workspaceError_ = inspected.error;
            }
            auto command = device_->createCommandList();
            Result<void> previewResult = command ? command.value->begin()
                                                  : Result<void>::failure(command.error);
            if (previewResult) {
                const auto initial = scenePreviewRendered_ ? rhi::ResourceState::ShaderRead
                                                            : rhi::ResourceState::Undefined;
                previewResult = play_.active()
                                    ? scenePreview_->recordGame(*play_.scene(), command.value, scenePreviewColor_,
                                                                initial, rhi::ResourceState::ShaderRead)
                                    : scenePreview_->record(draft->scene(), command.value, scenePreviewColor_,
                                                            initial, rhi::ResourceState::ShaderRead);
            }
            if (previewResult)
                previewResult = command.value->end();
            if (previewResult) {
                auto submitted = device_->submit(command.value);
                previewResult = submitted ? submitted.value->wait() : Result<void>::failure(submitted.error);
            }
            if (previewResult)
                scenePreviewRendered_ = true;
            else
                inspectorError_ = previewResult.error;
            ImGui::Begin("Viewport");
            if (previewResult) {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float imageSize = std::max(1.f, std::min(available.x, available.y));
                const ImVec2 position = ImGui::GetCursorScreenPos();
                ImGui::Image(scenePreviewId_, ImVec2(imageSize, imageSize));
                viewportFocused_ = play_.active() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                                   ImGui::IsItemHovered();
                auto selected = hierarchy_.selectedNode(draft->scene());
                if (selected && !play_.active()) {
                    auto manipulated = drawGizmo(draft->scene(), selected.value, scenePreview_->camera(),
                                                   {position.x, position.y, imageSize, imageSize}, gizmoOperation_);
                    if (manipulated && manipulated.value)
                        documents_->current()->markDirty();
                    else if (!manipulated)
                        inspectorError_ = manipulated.error;
                }
            }
            ImGui::End();
        }
    } else
        displayedSceneId_ = {};
    const bool sceneOpen = documents_ && documents_->current() &&
                           (documents_->current()->kind() == ResourceKind::Scene ||
                            documents_->current()->kind() == ResourceKind::Prefab);
    if (!sceneOpen) {
        ImGui::Begin("Hierarchy");
        ImGui::TextDisabled("Open a scene or Prefab from Project.");
        ImGui::End();
        ImGui::Begin("Viewport");
        ImGui::TextDisabled("Open a scene to view and edit it.");
        ImGui::End();
    }
    const bool assetInspectorOpen = documents_ && documents_->current() &&
                                    (documents_->current()->kind() == ResourceKind::Material ||
                                     documents_->current()->kind() == ResourceKind::Texture);
    if (!sceneOpen && !assetInspectorOpen) {
        ImGui::Begin("Inspector");
        ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
        ImGui::End();
    }
    if (documents_)
        documents_->drawUnsavedPrompt();
    return {};
}

Result<void> EditorApplication::run(uint32_t width, uint32_t height, const String &title, bool visible,
                                    uint32_t frameLimit) {
    if (!assetNameInput_[0])
        std::snprintf(assetNameInput_.data(), assetNameInput_.size(), "%s", "texture.ursatex");
    auto window = Window::create(width, height, title, visible);
    if (!window)
        return Result<void>::failure(window.error);
    window_ = std::move(window.value);

    rhi::DeviceDesc deviceDesc;
    deviceDesc.window = window_;
#ifndef NDEBUG
    deviceDesc.validation = true;
#endif
    auto device = rhi::createDevice(deviceDesc);
    if (!device) {
        window_.reset();
        return Result<void>::failure(device.error);
    }
    device_ = std::move(device.value);

    auto swapchain = device_->createSwapchain({});
    if (!swapchain) {
        device_.reset();
        window_.reset();
        return Result<void>::failure(swapchain.error);
    }
    swapchain_ = std::move(swapchain.value);

    auto gui = ImGuiBridge::create(device_, window_);
    if (!gui) {
        swapchain_.reset();
        device_.reset();
        window_.reset();
        return Result<void>::failure(gui.error);
    }
    gui_ = std::move(gui.value);
    dockLayoutInitialized_ = false;
    auto preview = MaterialPreview::create(device_, *gui_);
    if (!preview) {
        gui_.reset();
        swapchain_.reset();
        device_.reset();
        window_.reset();
        return Result<void>::failure(preview.error);
    }
    materialPreview_ = std::move(preview.value);
    auto scenePreview = EditorScenePreview::create(device_);
    if (!scenePreview) {
        materialPreview_.reset();
        gui_.reset();
        swapchain_.reset();
        device_.reset();
        window_.reset();
        return Result<void>::failure(scenePreview.error);
    }
    scenePreview_ = std::move(scenePreview.value);
    auto sceneColor = device_->createTexture({512, 512, rhi::Format::RGBA8Unorm,
                                              rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled});
    auto sceneSampler = device_->createSampler({true, false});
    if (!sceneColor || !sceneSampler) {
        const String error = !sceneColor ? sceneColor.error : sceneSampler.error;
        scenePreview_.reset();
        materialPreview_.reset();
        gui_.reset();
        swapchain_.reset();
        device_.reset();
        window_.reset();
        return Result<void>::failure(error);
    }
    scenePreviewColor_ = std::move(sceneColor.value);
    scenePreviewSampler_ = std::move(sceneSampler.value);
    auto sceneImage = gui_->registerTexture(scenePreviewColor_, scenePreviewSampler_);
    if (!sceneImage) {
        const String error = sceneImage.error;
        scenePreviewColor_.reset();
        scenePreviewSampler_.reset();
        scenePreview_.reset();
        materialPreview_.reset();
        gui_.reset();
        swapchain_.reset();
        device_.reset();
        window_.reset();
        return Result<void>::failure(error);
    }
    scenePreviewId_ = sceneImage.value;

    project_ = std::make_unique<EditorProject>();
    auto result = registerBuiltinComponents(project_->components);
    if (result && projectSetup_)
        result = projectSetup_(*project_);
    if (result)
        result = onStart();
    auto previous = std::chrono::steady_clock::now();
    uint32_t frames = 0;
    while (result && !window_->shouldClose()) {
        window_->pollEvents();
        if (window_->shouldClose() && documents_ && documents_->current() && documents_->current()->dirty() &&
            !exitRequested_) {
            exitRequested_ = true;
            auto closing = documents_->requestClose();
            if (!closing) {
                result = closing;
                break;
            }
            window_->cancelClose();
        }
        if (exitRequested_ && documents_ && !documents_->promptPending()) {
            if (documents_->current())
                exitRequested_ = false;
            else
                window_->requestClose();
        }
        if (window_->shouldClose())
            break;
        if (window_->framebufferWidth() == 0 || window_->framebufferHeight() == 0) {
            window_->waitEvents();
            previous = std::chrono::steady_clock::now();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - previous).count();
        previous = now;
        result = gui_->beginFrame(deltaSeconds, window_->framebufferWidth(), window_->framebufferHeight());
        if (result)
            result = onFrame(deltaSeconds);
        if (result && play_.active()) {
            const FrameInput input = selectPlayInput(FrameInput::capture(*window_), viewportFocused_,
                                                     ImGui::GetIO().WantTextInput);
            auto ticked = play_.tick(deltaSeconds, window_.get(), input);
            if (!ticked) {
                inspectorError_ = ticked.error;
                play_.stop();
                documents_->setSaveBlocked(false);
                viewportFocused_ = false;
            }
        }
        if (result)
            result = onGui();
        if (result)
            result = gui_->endFrame();
        if (!result)
            break;

        auto acquired = swapchain_->acquire();
        if (!acquired) {
            result = Result<void>::failure(acquired.error);
            break;
        }
        if (!acquired.value.texture)
            continue;

        auto command = device_->createCommandList();
        if (!command) {
            result = Result<void>::failure(command.error);
            break;
        }
        result = command.value->begin();
        if (result)
            result = command.value->barrier(acquired.value.texture, acquired.value.initialState,
                                            rhi::ResourceState::ColorAttachment);
        rhi::RenderTarget target;
        target.color = acquired.value.texture;
        if (result)
            result = command.value->beginRendering(target);
        if (result)
            result = gui_->record(command.value, swapchain_->format(), acquired.value.texture->desc.width,
                                  acquired.value.texture->desc.height);
        if (result)
            result = command.value->endRendering();
        if (result)
            result = command.value->barrier(acquired.value.texture, rhi::ResourceState::ColorAttachment,
                                            rhi::ResourceState::Present);
        if (result)
            result = command.value->end();
        rhi::FencePtr submittedFence;
        if (result) {
            auto submitted = device_->submit(command.value, swapchain_);
            if (!submitted)
                result = Result<void>::failure(submitted.error);
            else
                submittedFence = std::move(submitted.value);
        }
        if (result)
            result = swapchain_->present();
        if (result)
            result = submittedFence->wait();
        if (frameLimit && ++frames >= frameLimit)
            break;
    }

    const auto idle = device_->waitIdle();
    play_.stop();
    if (documents_)
        documents_->setSaveBlocked(false);
    onStop();
    documents_.reset();
    workspace_.reset();
    if (scenePreviewId_ != ImTextureID_Invalid)
        gui_->unregisterTexture(scenePreviewId_);
    scenePreviewId_ = ImTextureID_Invalid;
    scenePreviewColor_.reset();
    scenePreviewSampler_.reset();
    scenePreview_.reset();
    materialPreview_.reset();
    gui_.reset();
    project_.reset();
    swapchain_.reset();
    device_.reset();
    window_.reset();
    return result ? idle : result;
}
} // namespace ursa::editor
