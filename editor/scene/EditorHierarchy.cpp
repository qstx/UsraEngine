#include <scene/EditorHierarchy.hpp>
#include <algorithm>
#include <cstring>

namespace ursa::editor {
Result<void> EditorHierarchy::select(const Scene &scene, NodeId node) {
    auto id = scene.nodePersistentId(node);
    if (!id)
        return Result<void>::failure(id.error);
    selected_ = id.value;
    return {};
}

void EditorHierarchy::clearSelection() {
    selected_ = {};
}

Result<NodeId> EditorHierarchy::selectedNode(const Scene &scene) const {
    if (!selected_)
        return Result<NodeId>::failure("No node selected");
    return scene.nodeByPersistentId(selected_);
}

Result<bool> EditorHierarchy::drawNode(Scene &scene, NodeId node, const ComponentRegistry &registry) {
    auto id = scene.nodePersistentId(node);
    auto name = scene.nodeLabel(node);
    auto enabled = scene.isEnabled(node);
    auto children = scene.childrenOf(node);
    auto components = scene.componentsOf(node);
    if (!id || !name || !enabled || !children || !components)
        return Result<bool>::failure("Cannot inspect hierarchy node");
    const auto key = id.value.toString();
    ImGui::PushID(key.c_str());
    bool changed = false;
    bool active = enabled.value;
    if (ImGui::Checkbox("##enabled", &active)) {
        auto set = scene.setEnabled(node, active);
        if (!set) {
            ImGui::PopID();
            return Result<bool>::failure(set.error);
        }
        changed = true;
    }
    ImGui::SameLine();
    String display = name.value.empty() ? String("Node ") + key.substr(0, 8) : name.value;
    const bool hasChildren = !children.value.empty() || !components.value.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected_ == id.value)
        flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", display.c_str());
    if (ImGui::IsItemClicked())
        selected_ = id.value;
    if (open && hasChildren) {
        for (ComponentId componentId : components.value) {
            const auto *object = scene.component(componentId);
            if (!object)
                continue;
            auto componentIdValue = scene.componentPersistentId(componentId);
            if (!componentIdValue)
                continue;
            const String componentKey = componentIdValue.value.toString();
            ImGui::PushID(componentKey.c_str());
            auto componentEnabled = scene.isComponentEnabled(componentId);
            if (componentEnabled) {
                bool value = componentEnabled.value;
                if (ImGui::Checkbox("##componentEnabled", &value)) {
                    auto set = scene.setComponentEnabled(componentId, value);
                    if (!set) {
                        ImGui::PopID();
                        ImGui::TreePop();
                        ImGui::PopID();
                        return Result<bool>::failure(set.error);
                    }
                    changed = true;
                }
                ImGui::SameLine();
            }
            const auto codec = registry.findFor(*object);
            ImGui::TextUnformatted(codec ? codec.value->type.c_str() : "Unregistered component");
            ImGui::PopID();
        }
        for (NodeId child : children.value) {
            auto drawn = drawNode(scene, child, registry);
            if (!drawn) {
                ImGui::TreePop();
                ImGui::PopID();
                return drawn;
            }
            changed |= drawn.value;
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
    return Result<bool>::success(changed);
}

Result<bool> EditorHierarchy::draw(Scene &scene, const ComponentRegistry &registry) {
    bool changed = false;
    ImGui::Begin("Hierarchy");
    for (NodeId root : scene.rootNodes()) {
        auto drawn = drawNode(scene, root, registry);
        if (!drawn) {
            ImGui::End();
            return drawn;
        }
        changed |= drawn.value;
    }
    ImGui::End();
    if (selected_) {
        auto selected = selectedNode(scene);
        if (!selected)
            clearSelection();
        else {
            auto label = scene.nodeLabel(selected.value);
            if (!label)
                return Result<bool>::failure(label.error);
            char buffer[257]{};
            std::memcpy(buffer, label.value.data(), std::min<size_t>(label.value.size(), sizeof(buffer) - 1));
            ImGui::Begin("Inspector");
            if (ImGui::InputText("Label", buffer, sizeof(buffer))) {
                auto set = scene.setNodeLabel(selected.value, String(buffer));
                if (!set) {
                    ImGui::End();
                    return Result<bool>::failure(set.error);
                }
                changed = true;
            }
            ImGui::End();
        }
    }
    return Result<bool>::success(changed);
}
} // namespace ursa::editor
