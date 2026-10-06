#pragma once

#include <Serialization/ComponentRegistry.hpp>
#include <imgui.h>

namespace Ursa
{
class PrefabAsset;
}

namespace Ursa::Editor
{
class EditorHierarchy final
{
public:
	Result<void> Select(const Scene& scene, GameObject::Ptr node);

	void ClearSelection();

	Result<GameObject::Ptr> SelectedGameObject(const Scene& scene) const;

	Result<GameObject::Ptr> SelectedGameObject(const PrefabAsset& asset) const;

	ResourceId SelectedId() const
	{
		return selected;
	}

	// Returns true if a persistent scene property changed.
	Result<bool> Draw(Scene& scene, const ComponentRegistry& registry);

	Result<bool> Draw(PrefabAsset& asset, const ComponentRegistry& registry);

private:
	Result<bool> DrawGameObject(Scene& scene, GameObject::Ptr object, const ComponentRegistry& registry);

	ResourceId selected;
};
} // namespace Ursa::Editor
