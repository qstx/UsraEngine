#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <functional>

namespace Ursa::Editor
{
class AssetInspectorPanel final
{
public:
	using OpenAsset = std::function<Result<void>(const ProjectBrowserEntry&)>;

	void Draw(const ProjectWorkspace& workspace, const ProjectBrowserModel& model,
	          const EditorSelectionContext& selection, bool& open, const OpenAsset& openAsset);
};
} // namespace Ursa::Editor
