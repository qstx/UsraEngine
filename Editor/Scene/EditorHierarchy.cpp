#include <Core/Types.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <functional>

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

Result<bool> EditorHierarchy::Draw(PrefabAsset& asset)
{
	if (!asset.Root())
	{
		return Result<bool>::Failure("PrefabAsset has no root GameObject");
	}
	ImGui::Begin("Hierarchy");
	std::function<void(const GameObject::Ptr&)> draw = [&](const GameObject::Ptr& object)
	{
		const ResourceId id = object->Id();
		const String key = id.ToString();
		const auto name = object->Name();
		const auto children = object->Children();
		ImGui::PushID(key.c_str());
		const String label = name && !name.value.empty() ? name.value : String("GameObject ") + key.substr(0, 8);
		const bool hasContent = !children.empty();
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
	return Result<bool>::Success(false);
}

Result<bool> EditorHierarchy::DrawGameObject(Scene& scene, GameObject::Ptr node)
{
	auto id = scene.PersistentId(node);
	auto name = scene.GameObjectName(node);
	auto enabled = scene.IsEnabled(node);
	auto children = scene.ChildrenOf(node);
	if (!id || !name || !enabled || !children)
	{
		return Result<bool>::Failure("Cannot inspect hierarchy node");
	}
	const auto key = id.value.ToString();
	ImGui::PushID(key.c_str());
	String display = name.value.empty() ? String("Node ") + key.substr(0, 8) : name.value;
	const bool hasChildren = !children.value.empty();
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
		for (GameObject::Ptr child : children.value)
		{
			auto drawn = DrawGameObject(scene, child);
			if (!drawn)
			{
				ImGui::TreePop();
				ImGui::PopID();
				return drawn;
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
	return Result<bool>::Success(false);
}

Result<bool> EditorHierarchy::Draw(Scene& scene)
{
	ImGui::Begin("Hierarchy");
	for (GameObject::Ptr root : scene.RootGameObjects())
	{
		auto drawn = DrawGameObject(scene, root);
		if (!drawn)
		{
			ImGui::End();
			return drawn;
		}
	}
	ImGui::End();
	if (selected)
	{
		auto selectedObject = SelectedGameObject(scene);
		if (!selectedObject)
		{
			ClearSelection();
		}
	}
	return Result<bool>::Success(false);
}
} // namespace Ursa::Editor
