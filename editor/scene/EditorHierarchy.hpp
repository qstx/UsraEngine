#pragma once

#include <serialization/ComponentRegistry.hpp>
#include <imgui.h>

namespace Ursa::Editor
{
class EditorHierarchy final
{
public:
	Result<void> Select(const Scene& scene, NodeId node);

	void ClearSelection();

	Result<NodeId> SelectedNode(const Scene& scene) const;

	ResourceId SelectedId() const
	{
		return selected;
	}

	// Returns true if a persistent scene property changed.
	Result<bool> Draw(Scene& scene, const ComponentRegistry& registry);

private:
	Result<bool> DrawNode(Scene& scene, NodeId node, const ComponentRegistry& registry);

	ResourceId selected;
};
} // namespace Ursa::Editor
