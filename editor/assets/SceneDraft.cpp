#include <assets/SceneDraft.hpp>
#include <core/StringInterop.hpp>
#include <serialization/JsonResource.hpp>
#include <serialization/PrefabCapture.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace ursa::editor {
Result<std::shared_ptr<SceneDraft>> SceneDraft::create(ResourceKind kind, ComponentRegistry &registry,
                                                        ResourceSession &resources, const EditContextSetup &setup) {
    if (kind != ResourceKind::Scene && kind != ResourceKind::Prefab)
        return Result<std::shared_ptr<SceneDraft>>::failure("Scene draft kind must be Scene or Prefab");
    auto draft = std::shared_ptr<SceneDraft>(new SceneDraft(kind, registry, resources, setup));
    draft->scene_ = std::make_unique<Scene>();
    auto root = draft->scene_->createNode();
    if (!root)
        return Result<std::shared_ptr<SceneDraft>>::failure(root.error);
    auto labeled = draft->scene_->setNodeLabel(root.value, kind == ResourceKind::Scene ? "Root" : "Prefab");
    if (!labeled)
        return Result<std::shared_ptr<SceneDraft>>::failure(labeled.error);
    return Result<std::shared_ptr<SceneDraft>>::success(std::move(draft));
}

Result<std::shared_ptr<SceneDraft>> SceneDraft::load(const std::filesystem::path &path, ResourceKind kind,
                                                      ComponentRegistry &registry, ResourceSession &resources,
                                                      const EditContextSetup &setup) {
    if (kind != ResourceKind::Scene && kind != ResourceKind::Prefab)
        return Result<std::shared_ptr<SceneDraft>>::failure("Scene draft kind must be Scene or Prefab");
    ComponentLoadContext context(resources);
    if (setup) {
        auto configured = setup(context);
        if (!configured)
            return Result<std::shared_ptr<SceneDraft>>::failure(configured.error);
    }
    auto draft = std::shared_ptr<SceneDraft>(new SceneDraft(kind, registry, resources, setup));
    if (kind == ResourceKind::Prefab) {
        auto loaded = loadPrefabResource(path, registry, context);
        if (!loaded)
            return Result<std::shared_ptr<SceneDraft>>::failure(loaded.error);
        draft->scene_ = std::move(loaded.value.scene);
    } else {
        auto json = readJsonResource(path, ResourceKind::Scene);
        if (!json)
            return Result<std::shared_ptr<SceneDraft>>::failure(json.error);
        if (json.value.contains("instances")) {
            auto loaded = loadLinkedSceneResource(path, registry, context);
            if (!loaded)
                return Result<std::shared_ptr<SceneDraft>>::failure(loaded.error);
            draft->linked_ = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
        } else {
            auto loaded = loadSceneResource(path, registry, context);
            if (!loaded)
                return Result<std::shared_ptr<SceneDraft>>::failure(loaded.error);
            draft->scene_ = std::move(loaded.value.scene);
        }
    }
    return Result<std::shared_ptr<SceneDraft>>::success(std::move(draft));
}

Result<ResourceId> SceneDraft::materialId(const std::shared_ptr<MaterialInstance> &material) const {
    if (!material)
        return Result<ResourceId>::failure("No material assigned");
    auto assigned = assignedMaterials_.find(material.get());
    if (assigned != assignedMaterials_.end())
        return Result<ResourceId>::success(assigned->second);
    return resources_.identifyMaterial(material);
}

Result<void> SceneDraft::save(const std::filesystem::path &path, ResourceId id) {
    const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance> &material) {
        return materialId(material);
    };
    if (kind_ == ResourceKind::Prefab) {
        const auto roots = scene().rootNodes();
        if (roots.size() != 1)
            return Result<void>::failure("Prefab document must contain exactly one root node");
        return savePrefabResource(path, id, scene(), roots.front(), registry_, resolver);
    }
    if (linked_)
        return saveLinkedSceneResource(path, *linked_, registry_, resolver);
    return saveSceneResource(path, id, scene(), registry_, resolver);
}

Result<void> SceneDraft::assignMaterial(NodeId node, ResourceId materialId, ResourceSession &resources) {
    auto *box = scene().findComponent<BoxRenderComponent>(node);
    if (!box)
        return Result<void>::failure("Selected node has no box renderer component");
    auto components = scene().componentsOf(node);
    if (!components)
        return Result<void>::failure(components.error);
    for (ComponentId id : components.value)
        if (scene().component(id) == box)
            return assignMaterial(id, materialId, resources);
    return Result<void>::failure("Box renderer component is missing");
}

Result<void> SceneDraft::assignMaterial(ComponentId component, ResourceId materialId, ResourceSession &resources) {
    auto *box = dynamic_cast<BoxRenderComponent *>(scene().component(component));
    if (!box)
        return Result<void>::failure("Selected component is not a box renderer");
    if (!materialId) {
        box->material.reset();
        return {};
    }
    auto material = resources.loadMaterial(materialId);
    if (!material)
        return Result<void>::failure(material.error);
    box->material = material.value;
    assignedMaterials_[material.value.get()] = materialId;
    return {};
}

Result<void> SceneDraft::setInstanceProperty(ResourceId instanceId, ResourceId sourceObjectId,
                                               const String &pointer, const nlohmann::json &value) {
    if (!linked_)
        return Result<void>::failure("Scene has no linked Prefab instances");
    return setPrefabPropertyOverride(*linked_, instanceId, sourceObjectId, pointer, value);
}

Result<void> SceneDraft::setInstanceStructure(ResourceId instanceId, const String &operation,
                                                const nlohmann::json &value) {
    if (!linked_)
        return Result<void>::failure("Scene has no linked Prefab instances");
    return setPrefabStructureOverride(*linked_, instanceId, operation, value);
}

Result<ResourceId> SceneDraft::placePrefab(ResourceId prefabId, NodeId parent,
                                           const std::filesystem::path &scenePath, ResourceId sceneId) {
    if (kind_ != ResourceKind::Scene)
        return Result<ResourceId>::failure("Prefab instances can only be placed in scenes");
    ResourceId parentId;
    if (parent) {
        auto resolved = scene().nodePersistentId(parent);
        if (!resolved)
            return Result<ResourceId>::failure(resolved.error);
        parentId = resolved.value;
    }
    const MaterialIdResolver resolver = [this](const std::shared_ptr<MaterialInstance> &material) {
        return materialId(material);
    };
    nlohmann::json document;
    if (linked_) {
        const auto previousDocument = linked_->document;
        const auto previousSnapshot = linked_->snapshot;
        const auto previousScopes = linked_->scopedIds;
        auto captured = captureLinkedSceneChanges(scenePath, *linked_, registry_, resolver);
        if (!captured)
            return Result<ResourceId>::failure(captured.error);
        document = linked_->document;
        linked_->document = previousDocument;
        linked_->snapshot = previousSnapshot;
        linked_->scopedIds = previousScopes;
    } else {
        auto encoded = encodeSceneDocument(scenePath, sceneId, *scene_, registry_, resolver);
        if (!encoded)
            return Result<ResourceId>::failure(encoded.error);
        document = std::move(encoded.value);
    }
    const ResourceId instanceId = ResourceId::generate();
    auto placed = addPrefabInstance(document, prefabId, instanceId, parentId);
    if (!placed)
        return Result<ResourceId>::failure(placed.error);
    ComponentLoadContext context(resources_);
    if (setup_) {
        auto prepared = setup_(context);
        if (!prepared)
            return Result<ResourceId>::failure(prepared.error);
    }
    auto loaded = decodeLinkedSceneDocument(scenePath, std::move(document), registry_, context);
    if (!loaded)
        return Result<ResourceId>::failure(loaded.error);
    linked_ = std::make_unique<LinkedSceneResource>(std::move(loaded.value));
    scene_.reset();
    return Result<ResourceId>::success(instanceId);
}

Result<std::unique_ptr<EditorDocument>> newSceneDocument(const VirtualPath &path, ResourceKind kind,
                                                           ComponentRegistry &registry, ResourceSession &resources,
                                                           const EditContextSetup &setup) {
    auto draft = SceneDraft::create(kind, registry, resources, setup);
    if (!draft)
        return Result<std::unique_ptr<EditorDocument>>::failure(draft.error);
    auto document = EditorDocument::create(kind, ResourceId::generate(), path,
                                            [draft = draft.value](const std::filesystem::path &target, ResourceId id) {
                                                return draft->save(target, id);
                                            });
    if (document) {
        document.value->setPayload(draft.value);
        document.value->markDirty();
    }
    return document;
}

Result<std::unique_ptr<EditorDocument>> openSceneDocument(const VirtualPath &path, ResourceKind kind,
                                                            ComponentRegistry &registry, ResourceSession &resources,
                                                            const EditContextSetup &setup) {
    auto host = resources.index().locate(ResourceLocation{kind, path});
    if (!host)
        return Result<std::unique_ptr<EditorDocument>>::failure(host.error);
    auto header = readResourceHeader(host.value);
    if (!header)
        return Result<std::unique_ptr<EditorDocument>>::failure(header.error);
    if (header.value.kind != kind)
        return Result<std::unique_ptr<EditorDocument>>::failure("Scene document kind does not match file");
    auto draft = SceneDraft::load(host.value, kind, registry, resources, setup);
    if (!draft)
        return Result<std::unique_ptr<EditorDocument>>::failure(draft.error);
    auto document = EditorDocument::create(kind, header.value.id, path,
                                            [draft = draft.value](const std::filesystem::path &target, ResourceId id) {
                                                return draft->save(target, id);
                                            });
    if (document)
        document.value->setPayload(draft.value);
    return document;
}

Result<bool> drawSceneInspector(EditorDocument &document, EditorHierarchy &hierarchy,
                                 EditorDocuments &documents, const ProjectWorkspace &workspace,
                                 ComponentRegistry &registry, ComponentEditors &componentEditors,
                                 String &diagnostic) {
    auto draft = document.payload<SceneDraft>();
    if (!draft)
        return Result<bool>::failure("Scene document has no scene draft");
    Scene &scene = draft->scene();
    auto hierarchyChanged = hierarchy.draw(scene, registry);
    if (!hierarchyChanged)
        return hierarchyChanged;
    bool changed = hierarchyChanged.value;
    ResourceId prefabToPlace;
    NodeId placementParent;
    ImGui::Begin("Inspector");
    auto selected = hierarchy.selectedNode(scene);
    if (!selected)
        ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
    if (draft->kind() == ResourceKind::Scene && ImGui::BeginCombo("Place Prefab", "Select source")) {
        for (const auto &entry : workspace.resources())
            if (entry.location.kind == ResourceKind::Prefab) {
                ImGui::PushID(entry.id.toString().c_str());
                const auto name = entry.location.path.fileName();
                if (ImGui::Selectable(name.c_str())) {
                    prefabToPlace = entry.id;
                    placementParent = selected ? selected.value : NodeId{};
                }
                ImGui::PopID();
            }
        ImGui::EndCombo();
    }
    if (selected) {
        NodeId node = selected.value;
        if (draft->linked()) {
            auto selectedId = scene.nodePersistentId(node);
            if (selectedId)
                for (const auto &instance : draft->linkedScene()->scopedIds)
                    for (const auto &mapping : instance.second)
                        if (mapping.second == selectedId.value.toString()) {
                            ImGui::Text("Prefab instance: %s", instance.first.c_str());
                            if (ImGui::Button("Pin label override")) {
                                auto instanceId = ResourceId::parse(instance.first);
                                auto sourceId = ResourceId::parse(mapping.first);
                                auto label = scene.nodeLabel(node);
                                if (instanceId && sourceId && label) {
                                    auto pinned = draft->setInstanceProperty(instanceId.value, sourceId.value,
                                                                             "/label", toStdString(label.value));
                                    diagnostic = pinned ? String{} : pinned.error;
                                    changed |= static_cast<bool>(pinned);
                                }
                            }
                            break;
                        }
        }
        if (ImGui::Button("Add child")) {
            auto added = scene.createNode(node);
            diagnostic = added ? String{} : added.error;
            changed |= static_cast<bool>(added);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete node")) {
            auto removed = scene.destroyNode(node);
            diagnostic = removed ? String{} : removed.error;
            if (removed) {
                hierarchy.clearSelection();
                changed = true;
            }
        }
        if (scene.contains(node)) {
            auto parent = scene.parentOf(node);
            if (parent) {
                Vector<NodeId> siblings;
                if (parent.value) {
                    auto children = scene.childrenOf(parent.value);
                    if (children)
                        siblings = std::move(children.value);
                } else
                    siblings = scene.rootNodes();
                const auto position = std::find(siblings.begin(), siblings.end(), node);
                if (position != siblings.end()) {
                    const size_t index = static_cast<size_t>(position - siblings.begin());
                    ImGui::BeginDisabled(index == 0);
                    if (ImGui::Button("Move up")) {
                        auto moved = scene.reorderNode(node, index - 1);
                        diagnostic = moved ? String{} : moved.error;
                        changed |= static_cast<bool>(moved);
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(index + 1 >= siblings.size());
                    if (ImGui::Button("Move down")) {
                        auto moved = scene.reorderNode(node, index + 1);
                        diagnostic = moved ? String{} : moved.error;
                        changed |= static_cast<bool>(moved);
                    }
                    ImGui::EndDisabled();
                }
                if (ImGui::BeginCombo("Parent", parent.value ? "Node" : "Scene root")) {
                    if (ImGui::Selectable("Scene root", !parent.value)) {
                        auto moved = scene.setParent(node, {});
                        diagnostic = moved ? String{} : moved.error;
                        changed |= static_cast<bool>(moved);
                    }
                    std::function<void(NodeId)> offer = [&](NodeId candidate) {
                        if (candidate != node) {
                            auto label = scene.nodeLabel(candidate);
                            auto id = scene.nodePersistentId(candidate);
                            if (label && id) {
                                ImGui::PushID(id.value.toString().c_str());
                                if (ImGui::Selectable(label.value.c_str(), parent.value == candidate)) {
                                    auto moved = scene.setParent(node, candidate);
                                    diagnostic = moved ? String{} : moved.error;
                                    changed |= static_cast<bool>(moved);
                                }
                                ImGui::PopID();
                            }
                        }
                        auto children = scene.childrenOf(candidate);
                        if (children)
                            for (NodeId child : children.value)
                                offer(child);
                    };
                    for (NodeId root : scene.rootNodes())
                        offer(root);
                    ImGui::EndCombo();
                }
            }
            const auto local = scene.localTransform(node);
            if (local) {
                LocalTransform value = local.value;
                float position[3]{value.position.x, value.position.y, value.position.z};
                float scale[3]{value.scale.x, value.scale.y, value.scale.z};
                bool transformChanged = ImGui::DragFloat3("Position", position, 0.05f);
                transformChanged |= ImGui::DragFloat3("Scale", scale, 0.05f, 0.001f, 100.f);
                if (transformChanged) {
                    value.position = {position[0], position[1], position[2]};
                    value.scale = {scale[0], scale[1], scale[2]};
                    try {
                        auto transformParent = scene.parentOf(node);
                        const Mat4 parentWorld = transformParent && transformParent.value ? scene.worldTransform(transformParent.value).value
                                                                         : Mat4::identity();
                        auto applied = applyGizmoWorldTransform(scene, node, parentWorld * value.matrix());
                        diagnostic = applied ? String{} : applied.error;
                        changed |= static_cast<bool>(applied);
                    } catch (const std::exception &error) {
                        diagnostic = error.what();
                    }
                }
            }
            if (ImGui::Button("Add Box")) {
                auto added = scene.addComponent<BoxRenderComponent>(node);
                diagnostic = added ? String{} : added.error;
                changed |= static_cast<bool>(added);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Camera")) {
                auto added = scene.addComponent<CameraComponent>(node);
                diagnostic = added ? String{} : added.error;
                changed |= static_cast<bool>(added);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Light")) {
                auto added = scene.addComponent<DirectionalLightComponent>(node);
                diagnostic = added ? String{} : added.error;
                changed |= static_cast<bool>(added);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Point Light")) {
                auto added = scene.addComponent<PointLightComponent>(node);
                diagnostic = added ? String{} : added.error;
                changed |= static_cast<bool>(added);
            }
            auto components = scene.componentsOf(node);
            if (!components) {
                ImGui::End();
                return Result<bool>::failure(components.error);
            }
            for (ComponentId componentId : components.value) {
                Component *object = scene.component(componentId);
                if (!object)
                    continue;
                auto id = scene.componentPersistentId(componentId);
                if (!id) {
                    ImGui::End();
                    return Result<bool>::failure(id.error);
                }
                ImGui::PushID(id.value.toString().c_str());
                auto codec = registry.findFor(*object);
                if (!codec) {
                    diagnostic = codec.error;
                    ImGui::PopID();
                    continue;
                }
                ImGui::SeparatorText(codec.value->type.c_str());
                auto enabled = scene.isComponentEnabled(componentId);
                if (enabled) {
                    bool value = enabled.value;
                    if (ImGui::Checkbox("Enabled", &value)) {
                        auto set = scene.setComponentEnabled(componentId, value);
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                }
                ImGui::SameLine();
                const bool remove = ImGui::Button("Remove component");
                if (remove) {
                    auto removed = scene.removeComponent(componentId);
                    diagnostic = removed ? String{} : removed.error;
                    changed |= static_cast<bool>(removed);
                    ImGui::PopID();
                    continue;
                }
                if (auto *box = dynamic_cast<BoxRenderComponent *>(object)) {
                    float color[4]{box->color.red, box->color.green, box->color.blue, box->color.alpha};
                    if (ImGui::ColorEdit4("Color", color)) {
                        box->color = {color[0], color[1], color[2], color[3]};
                        changed = true;
                    }
                    const auto selectedMaterial = draft->materialId(box->material);
                    String materialName = "None";
                    if (selectedMaterial)
                        for (const auto &entry : workspace.resources())
                            if (entry.id == selectedMaterial.value)
                                materialName = entry.location.path.fileName();
                    if (ImGui::BeginCombo("Material", materialName.c_str())) {
                        if (ImGui::Selectable("None", !box->material)) {
                            auto set = draft->assignMaterial(componentId, {}, documents.session());
                            diagnostic = set ? String{} : set.error;
                            changed |= static_cast<bool>(set);
                        }
                        for (const auto &entry : workspace.resources())
                            if (entry.location.kind == ResourceKind::Material) {
                                ImGui::PushID(entry.id.toString().c_str());
                                const auto name = entry.location.path.fileName();
                                if (ImGui::Selectable(name.c_str(), selectedMaterial && selectedMaterial.value == entry.id)) {
                                    auto set = draft->assignMaterial(componentId, entry.id, documents.session());
                                    diagnostic = set ? String{} : set.error;
                                    changed |= static_cast<bool>(set);
                                }
                                ImGui::PopID();
                            }
                        ImGui::EndCombo();
                    }
                } else if (auto *camera = dynamic_cast<CameraComponent *>(object)) {
                    float fov = camera->verticalFovRadians * 57.2957795f;
                    float nearPlane = camera->nearPlane;
                    float farPlane = camera->farPlane;
                    bool edited = ImGui::DragFloat("FOV degrees", &fov, 0.5f, 1.f, 179.f);
                    edited |= ImGui::DragFloat("Near", &nearPlane, 0.01f, 0.001f, 10000.f);
                    edited |= ImGui::DragFloat("Far", &farPlane, 0.1f, 0.002f, 100000.f);
                    if (edited) {
                        if (std::isfinite(fov) && std::isfinite(nearPlane) && std::isfinite(farPlane) &&
                            fov > 0.f && fov < 180.f && nearPlane > 0.f && farPlane > nearPlane) {
                            camera->verticalFovRadians = fov * 0.0174532925f;
                            camera->nearPlane = nearPlane;
                            camera->farPlane = farPlane;
                            changed = true;
                            diagnostic.clear();
                        } else
                            diagnostic = "Camera requires 0 < FOV < 180 and 0 < Near < Far";
                    }
                    if (ImGui::Button("Use as game camera")) {
                        auto set = scene.setActiveCamera(componentId);
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                } else if (auto *directional = dynamic_cast<DirectionalLightComponent *>(object)) {
                    Color value = directional->color();
                    float color[4]{value.red, value.green, value.blue, value.alpha};
                    if (ImGui::ColorEdit4("Color", color)) {
                        auto set = directional->setColor({color[0], color[1], color[2], color[3]});
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                    float intensity = directional->intensity();
                    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f)) {
                        auto set = directional->setIntensity(intensity);
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                } else if (auto *point = dynamic_cast<PointLightComponent *>(object)) {
                    Color value = point->color();
                    float color[4]{value.red, value.green, value.blue, value.alpha};
                    if (ImGui::ColorEdit4("Color", color)) {
                        auto set = point->setColor({color[0], color[1], color[2], color[3]});
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                    float intensity = point->intensity();
                    float range = point->range();
                    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.f, 10000.f)) {
                        auto set = point->setIntensity(intensity);
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                    if (ImGui::DragFloat("Range", &range, 0.05f, 0.001f, 10000.f)) {
                        auto set = point->setRange(range);
                        diagnostic = set ? String{} : set.error;
                        changed |= static_cast<bool>(set);
                    }
                } else if (const auto *editor = componentEditors.find(codec.value->type)) {
                    auto edited = (*editor)(*object, scene, node);
                    diagnostic = edited ? String{} : edited.error;
                    changed |= edited && edited.value;
                } else
                    ImGui::TextDisabled("No property editor registered; serialized data is preserved");
                ImGui::PopID();
            }
        }
    }
    if (!diagnostic.empty())
        ImGui::TextWrapped("%s", diagnostic.c_str());
    ImGui::End();
    if (prefabToPlace) {
        auto sceneHost = workspace.index().locate(ResourceLocation{document.kind(), document.path()});
        if (!sceneHost) {
            diagnostic = sceneHost.error;
        } else {
            auto placed = draft->placePrefab(prefabToPlace, placementParent, sceneHost.value, document.id());
            diagnostic = placed ? String{} : placed.error;
            changed |= static_cast<bool>(placed);
        }
    }
    if (changed)
        document.markDirty();
    return Result<bool>::success(changed);
}
} // namespace ursa::editor
