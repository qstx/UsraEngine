#include <application/PlaySession.hpp>
#include <serialization/SceneResource.hpp>

namespace ursa::editor {
FrameInput selectPlayInput(FrameInput polled, bool viewportFocused, bool editorTextActive) {
    return viewportFocused && !editorTextActive ? polled : FrameInput{};
}

Result<void> PlaySession::start(const SceneDraft &draft, ResourceId documentId, const ComponentRegistry &registry,
                                ResourceSession &resources, const PlayContextSetup &setup) {
    if (active())
        return Result<void>::failure("Play mode is already active");
    if (draft.kind() != ResourceKind::Scene)
        return Result<void>::failure("Play mode requires a scene document");
    const MaterialIdResolver resolveMaterial = [&draft](const std::shared_ptr<MaterialInstance> &material) {
        return draft.materialId(material);
    };
    const std::filesystem::path virtualPath = "play-clone.ursascene";
    auto encoded = encodeSceneDocument(virtualPath, documentId, draft.scene(), registry, resolveMaterial);
    if (!encoded)
        return Result<void>::failure("Cannot clone edit scene: " + encoded.error);
    ComponentLoadContext context(resources);
    std::shared_ptr<void> services;
    if (setup) {
        auto prepared = setup(context);
        if (!prepared)
            return Result<void>::failure("Cannot create play services: " + prepared.error);
        services = std::move(prepared.value);
    }
    auto decoded = decodeSceneDocument(virtualPath, encoded.value, registry, context);
    if (!decoded)
        return Result<void>::failure("Cannot clone edit scene: " + decoded.error);
    services_ = std::move(services);
    scene_ = std::move(decoded.value.scene);
    scheduler_.reset();
    frameIndex_ = 0;
    elapsedSeconds_ = 0.0;
    return {};
}

Result<void> PlaySession::tick(float deltaSeconds, Window *window, FrameInput input) {
    if (!scene_)
        return Result<void>::failure("Play mode is not active");
    const auto schedule = scheduler_.advance(deltaSeconds);
    elapsedSeconds_ += schedule.deltaSeconds;
    FrameContext context;
    context.deltaSeconds = schedule.deltaSeconds;
    context.fixedDeltaSeconds = scheduler_.fixedSeconds();
    context.elapsedSeconds = elapsedSeconds_;
    context.frameIndex = ++frameIndex_;
    context.window = window;
    context.input = input;
    scene_->beginFrame();
    auto result = scene_->update(context);
    for (uint32_t step = 0; result && step < schedule.fixedSteps; ++step)
        result = scene_->fixedUpdate(context);
    if (result)
        result = scene_->lateUpdate(context);
    return result;
}

void PlaySession::stop() {
    if (scene_)
        scene_->clear();
    scene_.reset();
    services_.reset();
    scheduler_.reset();
    frameIndex_ = 0;
    elapsedSeconds_ = 0.0;
}
} // namespace ursa::editor
