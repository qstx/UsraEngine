#include <Core/Types.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <algorithm>
#include <cstring>

namespace Ursa::Editor
{
Result<void> EditorHierarchy::Select(const Scene& scene, GameObject::Ptr node)
{
	auto id = scene.PersistentId(node);
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

Result<GameObject::Ptr> EditorHierarchy::SelectedGameObject(const Scene& scene) const
{
	if (!selected)
	{
		return Result<GameObject::Ptr>::Failure("No node selected");
	}
	return scene.GameObjectById(selected);
}

Result<GameObject::Ptr> EditorHierarchy::SelectedGameObject(const PrefabAsset& asset) const
{
	if (!selected || !asset.Root())
	{
		return Result<GameObject::Ptr>::Failure("No Prefab GameObject selected");
	}
	std::function<GameObject::Ptr(const GameObject::Ptr&)> find = [&](const GameObject::Ptr& object)
	{
		if (object->Id() == selected)
		{
			return object;
		}
		for (const auto& child : object->Children())
		{
			auto result = find(child);
			if (result)
			{
				return result;
			}
		}
		return GameObject::Ptr{};
	};
	auto object = find(asset.Root());
	return object ? Result<GameObject::Ptr>::Success(std::move(object))
	              : Result<GameObject::Ptr>::Failure("Selected Prefab GameObject no longer exists");
}

Result<bool> EditorHierarchy::Draw(PrefabAsset& asset, const ComponentRegistry& registry)
{
	if (!asset.Root())
	{
		return Result<bool>::Failure("PrefabAsset has no root GameObject");
	}
	bool changed = false;
	ImGui::Begin("Hierarchy");
	std::function<void(const GameObject::Ptr&)> draw = [&](const GameObject::Ptr& object)
	{
		const ResourceId id = object->Id();
		const String key = id.ToString();
		const auto name = object->Name();
		const auto children = object->Children();
		ImGui::PushID(key.c_str());
		bool enabled = object->IsEnabled().value;
		if (ImGui::Checkbox("##enabled", &enabled))
		{
			object->SetEnabled(enabled);
			changed = true;
		}
		ImGui::SameLine();
		const String label = name && !name.value.empty() ? name.value : String("GameObject ") + key.substr(0, 8);
		const bool hasContent = !children.empty() || !object->Components().empty();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasContent)
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (selected == id)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		const bool open = ImGui::TreeNodeEx("##gameObject", flags, "%s", label.c_str());
		if (ImGui::IsItemClicked())
		{
			selected = id;
		}
		if (open && hasContent)
		{
			for (Component* component : object->Components())
			{
				ImGui::PushID(component->PersistentId().ToString().c_str());
				bool componentEnabled = component->IsEnabled();
				if (ImGui::Checkbox("##componentEnabled", &componentEnabled))
				{
					component->SetEnabled(componentEnabled);
					changed = true;
				}
				ImGui::SameLine();
				auto codec = registry.FindFor(*component);
				ImGui::TextUnformatted(codec ? codec.value->type.c_str() : "Unregistered component");
				ImGui::PopID();
			}
			for (const auto& child : children)
			{
				draw(child);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	};
	draw(asset.Root());
	ImGui::End();
	if (selected && !SelectedGameObject(asset))
	{
		ClearSelection();
	}
	return Result<bool>::Success(changed);
}

Result<bool> EditorHierarchy::DrawGameObject(Scene& scene, GameObject::Ptr node, const ComponentRegistry& registry)
{
	auto id = scene.PersistentId(node);
	auto name = scene.GameObjectName(node);
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
		for (GameObject::Ptr child : children.value)
		{
			auto drawn = DrawGameObject(scene, child, registry);
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
	for (GameObject::Ptr root : scene.RootGameObjects())
	{
		auto drawn = DrawGameObject(scene, root, registry);
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
		auto selectedObject = SelectedGameObject(scene);
		if (!selectedObject)
		{
			ClearSelection();
		}
		else
		{
			auto label = scene.GameObjectName(selectedObject.value);
			if (!label)
			{
				return Result<bool>::Failure(label.error);
			}
			char buffer[257]{};
			std::memcpy(buffer, label.value.data(), std::min<Ursa::Size>(label.value.size(), sizeof(buffer) - 1));
			ImGui::Begin("Inspector");
			if (ImGui::InputText("Label", buffer, sizeof(buffer)))
			{
				auto set = scene.SetGameObjectName(selectedObject.value, String(buffer));
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
