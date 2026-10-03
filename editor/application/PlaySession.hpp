#pragma once

#include <application/FrameScheduler.hpp>
#include <assets/SceneDraft.hpp>
#include <memory>

namespace ursa::editor {
using PlayContextSetup = std::function<Result<std::shared_ptr<void>>(ComponentLoadContext &)>;

FrameInput selectPlayInput(FrameInput polled, bool viewportFocused, bool editorTextActive);

class PlaySession final {
public:
    Result<void> start(const SceneDraft &draft, ResourceId documentId, const ComponentRegistry &registry,
                       ResourceSession &resources, const PlayContextSetup &setup = {});

    Result<void> tick(float deltaSeconds, Window *window, FrameInput input);

    void stop();

    bool active() const {
        return static_cast<bool>(scene_);
    }

    Scene *scene() {
        return scene_.get();
    }

    const Scene *scene() const {
        return scene_.get();
    }

    ~PlaySession() {
        stop();
    }

private:
    std::shared_ptr<void> services_;
    std::unique_ptr<Scene> scene_;
    FrameScheduler scheduler_;
    uint64_t frameIndex_ = 0;
    double elapsedSeconds_ = 0.0;
};
} // namespace ursa::editor
