#pragma once

#include <workspace/EditorDocuments.hpp>
#include <serialization/PrefabResource.hpp>
#include <scene/EditorHierarchy.hpp>
#include <scene/EditorScenePreview.hpp>
#include <scene/EditorGizmo.hpp>
#include <scene/ComponentEditors.hpp>

namespace ursa::editor {
using EditContextSetup = std::function<Result<void>(ComponentLoadContext &)>;

class SceneDraft final {
public:
    static Result<std::shared_ptr<SceneDraft>> create(ResourceKind kind, ComponentRegistry &registry,
                                                       ResourceSession &resources, const EditContextSetup &setup = {});

    static Result<std::shared_ptr<SceneDraft>> load(const std::filesystem::path &path, ResourceKind kind,
                                                     ComponentRegistry &registry, ResourceSession &resources,
                                                     const EditContextSetup &setup = {});

    Result<void> save(const std::filesystem::path &path, ResourceId id);

    Scene &scene() {
        return linked_ ? *linked_->scene : *scene_;
    }

    const Scene &scene() const {
        return linked_ ? *linked_->scene : *scene_;
    }

    ResourceKind kind() const {
        return kind_;
    }

    bool linked() const {
        return static_cast<bool>(linked_);
    }

    LinkedSceneResource *linkedScene() {
        return linked_.get();
    }

    Result<void> assignMaterial(NodeId node, ResourceId materialId, ResourceSession &resources);

    Result<void> assignMaterial(ComponentId component, ResourceId materialId, ResourceSession &resources);

    Result<ResourceId> materialId(const std::shared_ptr<MaterialInstance> &material) const;

    Result<void> setInstanceProperty(ResourceId instanceId, ResourceId sourceObjectId,
                                      const String &pointer, const nlohmann::json &value);

    Result<void> setInstanceStructure(ResourceId instanceId, const String &operation,
                                       const nlohmann::json &value);

    Result<ResourceId> placePrefab(ResourceId prefabId, NodeId parent, const std::filesystem::path &scenePath,
                                   ResourceId sceneId);

private:
    SceneDraft(ResourceKind kind, ComponentRegistry &registry, ResourceSession &resources,
               EditContextSetup setup)
        : kind_(kind), registry_(registry), resources_(resources), setup_(std::move(setup)) {
    }

    ResourceKind kind_;
    ComponentRegistry &registry_;
    ResourceSession &resources_;
    EditContextSetup setup_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<LinkedSceneResource> linked_;
    Map<const MaterialInstance *, ResourceId> assignedMaterials_;
};

Result<std::unique_ptr<EditorDocument>> newSceneDocument(const VirtualPath &path, ResourceKind kind,
                                                          ComponentRegistry &registry, ResourceSession &resources,
                                                          const EditContextSetup &setup = {});

Result<std::unique_ptr<EditorDocument>> openSceneDocument(const VirtualPath &path, ResourceKind kind,
                                                           ComponentRegistry &registry, ResourceSession &resources,
                                                           const EditContextSetup &setup = {});

Result<bool> drawSceneInspector(EditorDocument &document, EditorHierarchy &hierarchy,
                                EditorDocuments &documents, const ProjectWorkspace &workspace,
                                ComponentRegistry &registry, ComponentEditors &componentEditors,
                                String &diagnostic);
} // namespace ursa::editor
