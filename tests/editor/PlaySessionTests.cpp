#include <application/PlaySession.hpp>
#include <workspace/EditorDocuments.hpp>
#include <core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa;

Vector<String> events;

struct TestService {
    int value = 0;
};

class TestComponent final : public Component {
public:
    TestComponent(TestService &service, int value) : service_(service), value_(value) {
    }

    Result<void> onStart(const FrameContext &) override {
        events.push_back("start");
        return {};
    }

    Result<void> onUpdate(const FrameContext &) override {
        events.push_back("update");
        if (service_.value < 0)
            return Result<void>::failure("Simulated play failure");
        ++value_;
        ++service_.value;
        return {};
    }

    Result<void> onFixedUpdate(const FrameContext &) override {
        events.push_back("fixed");
        return {};
    }

    Result<void> onLateUpdate(const FrameContext &) override {
        events.push_back("late");
        return {};
    }

    void onRemove() override {
        events.push_back("remove");
    }

    int value() const {
        return value_;
    }

private:
    TestService &service_;
    int value_;
};

template <class T> T take(Result<T> result) {
    if (!result)
        throw std::runtime_error(toStdString(result.error));
    return std::move(result.value);
}

void done(Result<void> result) {
    if (!result)
        throw std::runtime_error(toStdString(result.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
                      ("play_session_" + toStdString(ResourceId::generate().toString()));
    try {
        std::filesystem::create_directories(root / "assets");
        auto workspace = take(editor::ProjectWorkspace::open(root));
        auto documents = take(editor::EditorDocuments::create(*workspace));
        ComponentRegistry registry;
        done(registerBuiltinComponents(registry));
        ComponentCodec codec;
        codec.type = "test.PlayComponent";
        codec.matches = [](const Component &component) {
            return dynamic_cast<const TestComponent *>(&component) != nullptr;
        };
        codec.save = [](const Component &component, const ComponentSaveContext &) {
            return Result<nlohmann::json>::success({{"value", static_cast<const TestComponent &>(component).value()}});
        };
        codec.create = [](Scene &scene, NodeId node, const nlohmann::json &data, ComponentLoadContext &context) {
            auto service = context.service<TestService>();
            if (!service)
                return Result<ComponentId>::failure(service.error);
            return scene.addComponent<TestComponent>(node, *service.value, data.at("value").get<int>());
        };
        done(registry.registerCodec(std::move(codec)));
        auto document = take(editor::newSceneDocument(take(VirtualPath::parse("prj:play.ursascene")),
                                                      ResourceKind::Scene, registry, documents->session()));
        auto draft = document->payload<editor::SceneDraft>();
        const auto rootNode = draft->scene().rootNodes().front();
        TestService editService;
        take(draft->scene().addComponent<TestComponent>(rootNode, editService, 12));
        document->markDirty();
        const auto documentId = document->id();
        done(documents->requestOpen(std::move(document)));
        editor::PlaySession play;
        FrameInput pressed;
        pressed.keys[static_cast<size_t>(Key::Space)] = true;
        expect(!editor::selectPlayInput(pressed, false, false).keyDown(Key::Space) &&
                   !editor::selectPlayInput(pressed, true, true).keyDown(Key::Space) &&
                   editor::selectPlayInput(pressed, true, false).keyDown(Key::Space),
               "Editor keyboard input leaked to or was blocked from the game viewport");
        auto missing = play.start(*draft, documentId, registry, documents->session());
        expect(!missing && !play.active() && documents->current()->dirty(),
               "Missing runtime service changed Edit mode");
        auto service = std::make_shared<TestService>();
        done(play.start(*draft, documentId, registry, documents->session(),
                        [service](ComponentLoadContext &context) -> Result<std::shared_ptr<void>> {
                            context.provide(*service);
                            return Result<std::shared_ptr<void>>::success(service);
                        }));
        documents->setSaveBlocked(true);
        expect(!documents->saveCurrent(), "Play mode allowed saving");
        events.clear();
        done(play.tick(1.f / 30.f, nullptr, {}));
        expect(events.size() == 5 && events[0] == "start" && events[1] == "update" &&
                   events[2] == "fixed" && events[3] == "fixed" && events[4] == "late",
               "Play frame lifecycle order changed");
        expect(service->value == 1 &&
                   draft->scene().findComponent<TestComponent>(rootNode)->value() == 12,
               "Play mutation reached the edit scene");
        play.stop();
        documents->setSaveBlocked(false);
        expect(events.back() == "remove" && !play.active() && documents->current()->dirty() &&
                   !std::filesystem::exists(root / "assets" / "play.ursascene"),
               "Stop did not restore the edit document state");
        auto failingService = std::make_shared<TestService>();
        failingService->value = -1;
        done(play.start(*draft, documentId, registry, documents->session(),
                        [failingService](ComponentLoadContext &context) -> Result<std::shared_ptr<void>> {
                            context.provide(*failingService);
                            return Result<std::shared_ptr<void>>::success(failingService);
                        }));
        expect(!play.tick(1.f / 60.f, nullptr, {}), "Simulated play failure was not reported");
        play.stop();
        expect(documents->current()->dirty() &&
                   draft->scene().findComponent<TestComponent>(rootNode)->value() == 12,
               "Failed play changed the pre-Run edit state");
        done(documents->saveCurrent());
        std::filesystem::remove_all(root);
        std::cout << "Editor Play session test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
