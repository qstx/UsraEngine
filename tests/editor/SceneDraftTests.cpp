#include <assets/SceneDraft.hpp>
#include <serialization/MaterialResource.hpp>
#include <serialization/JsonResource.hpp>
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

void expect(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
                      ("scene_draft_" + toStdString(ResourceId::generate().toString()));
    try {
        std::filesystem::create_directories(root / "assets");
        auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
        done(material->setBaseColor({0.7f, 0.2f, 0.1f, 1.f}));
        const auto materialId = ResourceId::generate();
        done(saveMaterialResource(root / "assets" / "box.ursamat", materialId, *material, {}));
        auto workspace = take(editor::ProjectWorkspace::open(root));
        auto documents = take(editor::EditorDocuments::create(*workspace));
        ComponentRegistry registry;
        done(registerBuiltinComponents(registry));
        const auto scenePath = root / "assets" / "level.ursascene";
        auto sceneDocument = take(editor::newSceneDocument(take(VirtualPath::parse("prj:level.ursascene")),
                                                           ResourceKind::Scene, registry, documents->session()));
        const auto sceneId = sceneDocument->id();
        auto draft = sceneDocument->payload<editor::SceneDraft>();
        Scene &scene = draft->scene();
        const auto first = scene.rootNodes().front();
        const auto child = take(scene.createNode(first));
        const auto last = take(scene.createNode());
        done(scene.setNodeLabel(child, "Nested box"));
        done(scene.setNodeLabel(last, "Last"));
        done(scene.setEnabled(child, false));
        LocalTransform pose;
        pose.position = {1, 2, 3};
        done(scene.setLocalTransform(child, pose));
        take(scene.addComponent<BoxRenderComponent>(child));
        take(scene.addComponent<DirectionalLightComponent>(last));
        done(draft->assignMaterial(child, materialId, documents->session()));
        const auto childId = take(scene.nodePersistentId(child));
        const auto lastId = take(scene.nodePersistentId(last));
        done(documents->requestOpen(std::move(sceneDocument)));
        done(documents->saveCurrent());
        auto reopened = take(editor::openSceneDocument(take(VirtualPath::parse("prj:level.ursascene")),
                                                       ResourceKind::Scene, registry, documents->session()));
        expect(reopened->id() == sceneId, "Scene resource ID changed after reopen");
        auto loaded = reopened->payload<editor::SceneDraft>();
        const auto loadedChild = take(loaded->scene().nodeByPersistentId(childId));
        expect(take(loaded->scene().nodeLabel(loadedChild)) == "Nested box" &&
                   !take(loaded->scene().isEnabled(loadedChild)) &&
                   take(loaded->scene().localTransform(loadedChild)).position.x == 1.f,
               "Scene hierarchy or transform did not round-trip");
        expect(take(loaded->scene().nodePersistentId(loaded->scene().rootNodes().back())) == lastId,
               "Scene root order or ID changed");
        const auto *box = loaded->scene().findComponent<BoxRenderComponent>(loadedChild);
        expect(box && box->material && box->material->parameters().baseColor.red > 0.6f,
               "Scene material reference did not round-trip");

        const auto prefabPath = root / "assets" / "tile.ursaprefab";
        auto prefabDocument = take(editor::newSceneDocument(take(VirtualPath::parse("prj:tile.ursaprefab")),
                                                            ResourceKind::Prefab, registry, documents->session()));
        const auto prefabId = prefabDocument->id();
        auto prefabDraft = prefabDocument->payload<editor::SceneDraft>();
        const auto prefabRoot = prefabDraft->scene().rootNodes().front();
        const auto prefabChild = take(prefabDraft->scene().createNode(prefabRoot));
        done(prefabDraft->scene().setNodeLabel(prefabChild, "Prefab child"));
        take(prefabDraft->scene().addComponent<BoxRenderComponent>(prefabChild));
        done(prefabDraft->assignMaterial(prefabChild, materialId, documents->session()));
        const auto prefabChildId = take(prefabDraft->scene().nodePersistentId(prefabChild));
        done(documents->requestOpen(std::move(prefabDocument)));
        done(documents->saveCurrent());
        auto reopenedPrefab = take(editor::openSceneDocument(take(VirtualPath::parse("prj:tile.ursaprefab")),
                                                             ResourceKind::Prefab, registry,
                                                             documents->session()));
        expect(reopenedPrefab->id() == prefabId, "Prefab resource ID changed after reopen");
        auto prefabLoaded = reopenedPrefab->payload<editor::SceneDraft>();
        const auto loadedPrefabChild = take(prefabLoaded->scene().nodeByPersistentId(prefabChildId));
        expect(take(prefabLoaded->scene().nodeLabel(loadedPrefabChild)) == "Prefab child" &&
                   prefabLoaded->scene().findComponent<BoxRenderComponent>(loadedPrefabChild)->material,
               "Prefab subtree or material reference did not round-trip");
        auto linkedDocument = take(editor::openSceneDocument(take(VirtualPath::parse("prj:level.ursascene")),
                                                             ResourceKind::Scene, registry,
                                                             documents->session()));
        auto linkedDraft = linkedDocument->payload<editor::SceneDraft>();
        const auto parentNode = take(linkedDraft->scene().nodeByPersistentId(childId));
        const auto firstInstance = take(linkedDraft->placePrefab(prefabId, parentNode, scenePath, sceneId));
        const auto secondInstance = take(linkedDraft->placePrefab(prefabId, parentNode, scenePath, sceneId));
        const auto firstScoped = take(ResourceId::parse(linkedDraft->linkedScene()->scopedIds.at(
            firstInstance.toString()).at(prefabChildId.toString())));
        const auto secondScoped = take(ResourceId::parse(linkedDraft->linkedScene()->scopedIds.at(
            secondInstance.toString()).at(prefabChildId.toString())));
        const auto firstNode = take(linkedDraft->scene().nodeByPersistentId(firstScoped));
        const auto secondNode = take(linkedDraft->scene().nodeByPersistentId(secondScoped));
        done(linkedDraft->scene().setNodeLabel(firstNode, "Instance override"));
        take(linkedDraft->scene().createNode(firstNode));
        done(linkedDraft->setInstanceProperty(firstInstance, prefabChildId, "/label", "Instance override"));
        expect(take(linkedDraft->scene().nodeLabel(secondNode)) == "Prefab child",
               "Editing first placement changed the second placement");
        const auto sourceBefore = take(readJsonResource(prefabPath, ResourceKind::Prefab));
        linkedDocument->markDirty();
        done(documents->requestOpen(std::move(linkedDocument)));
        done(documents->current()->dirty() ? Result<void>{} : Result<void>::failure("Expected dirty scene"));
        done(documents->saveCurrent());
        auto linkedReopened = take(editor::openSceneDocument(take(VirtualPath::parse("prj:level.ursascene")),
                                                             ResourceKind::Scene, registry,
                                                             documents->session()));
        auto reloadedDraft = linkedReopened->payload<editor::SceneDraft>();
        const auto reloadedFirst = take(reloadedDraft->scene().nodeByPersistentId(firstScoped));
        const auto reloadedSecond = take(reloadedDraft->scene().nodeByPersistentId(secondScoped));
        expect(take(reloadedDraft->scene().nodeLabel(reloadedFirst)) == "Instance override" &&
                   take(reloadedDraft->scene().nodeLabel(reloadedSecond)) == "Prefab child" &&
                   take(reloadedDraft->scene().childrenOf(reloadedFirst)).size() == 1,
               "Prefab property or structure overrides did not round-trip");
        expect(take(readJsonResource(prefabPath, ResourceKind::Prefab)) == sourceBefore,
               "Saving an instance modified the Prefab source");
        std::filesystem::remove_all(root);
        std::cout << "Editor scene and Prefab document test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
