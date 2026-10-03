#include <scene/EditorHierarchy.hpp>
#include <algorithm>
#include <cstring>

namespace Ursa::Editor
{
Result<void> EditorHierarchy::Select(const Scene& scene, NodeId node)
{
	auto id = scene.NodePersistentId(node);
	if (!id)
	{
		return Result<void>::Failure(id.error);
	}
	selected = id.value;
	return {};
}

void EditorHierarchy::ClearSelection()
{
	selected = {};
}

Result<NodeId> EditorHierarchy::SelectedNode(const Scene& scene) const
{
	if (!selected)
	{
		return Result<NodeId>::Failure("No node selected");
	}
	return scene.NodeByPersistentId(selected);
}

Result<bool> EditorHierarchy::DrawNode(Scene& scene, NodeId node, const ComponentRegistry& registry)
{
	auto id = scene.NodePersistentId(node);
	auto name = scene.NodeLabel(node);
	auto enabled = scene.IsEnabled(node);
	auto children = scene.ChildrenOf(node);
	auto components = scene.ComponentsOf(node);
	if (!id || !name || !enabled || !children || !components)
	{
		return Result<bool>::Failure("Cannot inspect hierarchy node");
	}
	const auto key = id.value.ToString();
	ImGui::PushID(key.c_str());
	bool changed = false;
	bool active = enabled.value;
	if (ImGui::Checkbox("##enabled", &active))
	{
		auto set = scene.SetEnabled(node, active);
		if (!set)
		{
			ImGui::PopID();
			return Result<bool>::Failure(set.error);
		}
		changed = true;
	}
	ImGui::SameLine();
	String display = name.value.empty() ? String("Node ") + key.substr(0, 8) : name.value;
	const bool hasChildren = !children.value.empty() || !components.value.empty();
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (selected == id.value)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	const bool open = ImGui::TreeNodeEx("##node", flags, "%s", display.c_str());
	if (ImGui::IsItemClicked())
	{
		selected = id.value;
	}
	if (open && hasChildren)
	{
		for (ComponentId componentId : components.value)
		{
			const auto* object = scene.GetComponent(componentId);
			if (!object)
			{
				continue;
			}
			auto componentIdValue = scene.ComponentPersistentId(componentId);
			if (!componentIdValue)
			{
				continue;
			}
			const String componentKey = componentIdValue.value.ToString();
			ImGui::PushID(componentKey.c_str());
			auto componentEnabled = scene.IsComponentEnabled(componentId);
			if (componentEnabled)
			{
				bool value = componentEnabled.value;
				if (ImGui::Checkbox("##componentEnabled", &value))
				{
					auto set = scene.SetComponentEnabled(componentId, value);
					if (!set)
					{
						ImGui::PopID();
						ImGui::TreePop();
						ImGui::PopID();
						return Result<bool>::Failure(set.error);
					}
					changed = true;
				}
				ImGui::SameLine();
			}
			const auto codec = registry.FindFor(*object);
			ImGui::TextUnformatted(codec ? codec.value->type.c_str() : "Unregistered component");
			ImGui::PopID();
		}
		for (NodeId child : children.value)
		{
			auto drawn = DrawNode(scene, child, registry);
			if (!drawn)
			{
				ImGui::TreePop();
				ImGui::PopID();
				return drawn;
			}
			changed |= drawn.value;
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
	return Result<bool>::Success(changed);
}

Result<bool> EditorHierarchy::Draw(Scene& scene, const ComponentRegistry& registry)
{
	bool changed = false;
	ImGui::Begin("Hierarchy");
	for (NodeId root : scene.RootNodes())
	{
		auto drawn = DrawNode(scene, root, registry);
		if (!drawn)
		{
			ImGui::End();
			return drawn;
		}
		changed |= drawn.value;
	}
	ImGui::End();
	if (selected)
	{
		auto selected = SelectedNode(scene);
		if (!selected)
		{
			ClearSelection();
		}
		else
		{
			auto label = scene.NodeLabel(selected.value);
			if (!label)
			{
				return Result<bool>::Failure(label.error);
			}
			char buffer[257]{};
			std::memcpy(buffer, label.value.data(), std::min<size_t>(label.value.size(), sizeof(buffer) - 1));
			ImGui::Begin("Inspector");
			if (ImGui::InputText("Label", buffer, sizeof(buffer)))
			{
				auto set = scene.SetNodeLabel(selected.value, String(buffer));
				if (!set)
				{
					ImGui::End();
					return Result<bool>::Failure(set.error);
				}
				changed = true;
			}
			ImGui::End();
		}
	}
	return Result<bool>::Success(changed);
}
} // namespace Ursa::Editor
