#include <scene/EditorHierarchy.hpp>
#include <serialization/SceneResource.hpp>
#include <serialization/PrefabResource.hpp>
#include <core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ursa;

template <class T> T take(Result<T> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
    return std::move(value.value);
}

void done(Result<void> value) {
    if (!value)
        throw std::runtime_error(toStdString(value.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        ComponentRegistry registry;
        done(registerBuiltinComponents(registry));
        Scene scene;
        const auto first = take(scene.createNode());
        const auto child = take(scene.createNode(first));
        const auto second = take(scene.createNode());
        done(scene.reorderNode(second, 0));
        done(scene.setNodeLabel(first, "Root"));
        done(scene.setNodeLabel(child, "Child"));
        done(scene.setEnabled(child, false));
        take(scene.addComponent<BoxRenderComponent>(first));
        const auto firstId = take(scene.nodePersistentId(first));
        const auto childId = take(scene.nodePersistentId(child));
        const auto secondId = take(scene.nodePersistentId(second));
        editor::EditorHierarchy hierarchy;
        done(hierarchy.select(scene, child));

        const auto path = std::filesystem::current_path() / "hierarchy.ursascene";
        const auto document = take(encodeSceneDocument(path, ResourceId::generate(), scene, registry));
        const auto resources = take(ResourceSession::open({std::filesystem::current_path()}));
        ComponentLoadContext context(*resources);
        auto loaded = take(decodeSceneDocument(path, document, registry, context));
        expect(loaded.scene->rootNodes().size() == 2, "Root count changed after save/reopen");
        expect(take(loaded.scene->nodePersistentId(loaded.scene->rootNodes()[0])) == secondId,
               "Root order changed after save/reopen");
        expect(take(loaded.scene->nodePersistentId(loaded.scene->rootNodes()[1])) == firstId,
               "Root ID changed after save/reopen");
        const auto reopenedChild = take(loaded.scene->nodeByPersistentId(childId));
        expect(take(loaded.scene->nodeLabel(reopenedChild)) == "Child", "Child label was not restored");
        expect(!take(loaded.scene->isEnabled(reopenedChild)), "Child enabled state changed");
        expect(take(loaded.scene->nodePersistentId(take(hierarchy.selectedNode(*loaded.scene)))) == childId,
               "Hierarchy selection did not survive scene replacement");

        auto legacy = document;
        for (auto &node : legacy["nodes"])
            node.erase("label");
        auto oldScene = take(decodeSceneDocument(path, legacy, registry, context));
        expect(take(oldScene.scene->nodeLabel(take(oldScene.scene->nodeByPersistentId(childId)))).empty(),
               "Old scene without labels did not load");
        const auto resaved = take(encodeSceneDocument(path, ResourceId::generate(), *oldScene.scene, registry));
        expect(resaved["nodes"][1].contains("label"), "Re-saved old scene is missing a label field");

        const auto prefabPath = std::filesystem::current_path() / "hierarchy.ursaprefab";
        done(savePrefabResource(prefabPath, ResourceId::generate(), scene, first, registry));
        auto prefab = take(loadPrefabResource(prefabPath, registry, context));
        expect(take(prefab.scene->nodeLabel(take(prefab.scene->nodeByPersistentId(firstId)))) == "Root",
               "Prefab root label was not restored");
        expect(take(prefab.scene->nodeLabel(take(prefab.scene->nodeByPersistentId(childId)))) == "Child",
               "Prefab child label was not restored");

        auto *imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(imgui);
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetIO().DisplaySize = ImVec2(640, 480);
        ImGui::NewFrame();
        const auto displayed = hierarchy.draw(*loaded.scene, registry);
        expect(displayed && !displayed.value, "Hierarchy display changed scene data unexpectedly");
        ImGui::Render();
        ImGui::DestroyContext(imgui);
        std::cout << "Editor hierarchy persistence test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
