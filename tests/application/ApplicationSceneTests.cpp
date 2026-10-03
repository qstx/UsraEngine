#include <core/Containers.hpp>
#include <application/Application.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
class Probe final : public ursa::Component {
public:
    Probe(int &updates, int &removals, bool fail) : updates_(updates), removals_(removals), fail_(fail) {
    }

    ursa::Result<void> onUpdate(const ursa::FrameContext &context) override {
        ++updates_;
        if (!context.window || context.frameIndex != static_cast<uint64_t>(updates_))
            return ursa::Result<void>::failure("Frame context was not populated");
        if (updates_ == 2 && !fail_) {
            context.window->minimize();
            context.window->restore();
        }
        if (fail_)
            return ursa::Result<void>::failure("Intentional component failure");
        return {};
    }

    void onRemove() override {
        ++removals_;
    }

private:
    int &updates_;
    int &removals_;
    bool fail_;
};

class SceneApp final : public ursa::Application {
public:
    explicit SceneApp(bool fail) : fail_(fail) {
    }

    int updates = 0;
    int removals = 0;
    int legacyFrames = 0;

private:
    ursa::Result<void> onStart() override {
        scene_ = std::make_shared<ursa::Scene>();
        auto node = scene_->createNode();
        if (!node)
            return ursa::Result<void>::failure(node.error);
        auto component = scene_->addComponent<Probe>(node.value, updates, removals, fail_);
        if (!component)
            return ursa::Result<void>::failure(component.error);
        auto cameraNode = scene_->createNode();
        if (!cameraNode)
            return ursa::Result<void>::failure(cameraNode.error);
        ursa::LocalTransform cameraTransform;
        cameraTransform.position = {0, 0, 4};
        auto transformed = scene_->setLocalTransform(cameraNode.value, cameraTransform);
        if (!transformed)
            return transformed;
        auto camera = scene_->addComponent<ursa::CameraComponent>(cameraNode.value);
        if (!camera)
            return ursa::Result<void>::failure(camera.error);
        auto selected = scene_->setActiveCamera(camera.value);
        if (!selected)
            return selected;
        return {};
    }

    ursa::Result<void> onFrame(float) override {
        ++legacyFrames;
        return {};
    }

    bool fail_;
};

class LegacyApp final : public ursa::Application {
public:
    int frames = 0;

private:
    ursa::Result<void> onStart() override {
        return {};
    }

    ursa::Result<void> onFrame(float) override {
        ++frames;
        return {};
    }
};

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        SceneApp normal(false);
        const auto normalResult = normal.run(640, 480, "Scene application test", false, 4);
        expect(static_cast<bool>(normalResult), "Scene application failed");
        expect(normal.updates == 4 && normal.removals == 1 && normal.legacyFrames == 0,
               "Normal scene lifecycle or legacy callback path was wrong");
        SceneApp failing(true);
        const auto failed = failing.run(640, 480, "Scene failure test", false, 4);
        expect(!failed && failed.error.find("Intentional component failure") != ursa::String::npos,
               "Component failure was not returned by Application");
        expect(failing.updates == 1 && failing.removals == 1, "Failure cleanup did not run once");
        LegacyApp legacy;
        const auto legacyResult = legacy.run(640, 480, "Legacy application test", false, 3);
        expect(static_cast<bool>(legacyResult) && legacy.frames == 3,
               "Application without a scene did not use the legacy frame callback");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
