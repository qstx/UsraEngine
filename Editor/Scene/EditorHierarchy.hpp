#pragma once

#include <Scene/Scene.hpp>
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

	Result<void> Select(const PrefabAsset& asset, GameObject::Ptr node);

	void ClearSelection();

	Result<GameObject::Ptr> SelectedGameObject(const Scene& scene) const;

	Result<GameObject::Ptr> SelectedGameObject(const PrefabAsset& asset) const;

	ResourceId SelectedId() const
	{
		return selected;
	}

	// Returns true if a persistent scene property changed.
	Result<bool> Draw(Scene& scene, bool* open = nullptr);

	Result<bool> Draw(PrefabAsset& asset, bool* open = nullptr);

	void DrawEmpty(bool* open = nullptr);

private:
	Result<bool> DrawGameObject(Scene& scene, GameObject::Ptr object);

	ResourceId selected;
};
} // namespace Ursa::Editor
