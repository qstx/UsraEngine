#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <functional>

namespace Ursa::Editor
{
struct ProjectBrowserActions
{
	std::function<Result<void>()> refresh;
	std::function<Result<void>(const String&)> createFolder;
	std::function<Result<void>(ResourceKind, MaterialType, const String&)> createAsset;
	const Vector<ScriptTypeDescriptor>* scriptableAssetTypes = nullptr;
	std::function<Result<void>(const ScriptTypeDescriptor&, const String&)> createScriptableAsset;
	std::function<Result<void>(const VirtualPath&, const VirtualPath&)> renameAsset;
	std::function<Result<void>(const ProjectBrowserEntry&)> openAsset;
};

class ProjectBrowserPanel final
{
public:
	void Reset()
	{
		expandedAssets.clear();
		searchText.fill(0);
		destinationPath.fill(0);
		folderName.fill(0);
		createFolderRequested = false;
		renameRequested = false;
	}

	void RequestRenameSelected()
	{
		renameRequested = true;
	}

	Result<void> Draw(ProjectWorkspace& workspace, ProjectBrowserModel& model, EditorSelectionContext& selection,
	                  EditorHierarchy& hierarchy, bool& open, bool readOnly, String& diagnostic,
	                  const ProjectBrowserActions& actions);

private:
	Result<void> RenameSelected(const ProjectBrowserModel& model, EditorSelectionContext& selection,
	                            const ProjectBrowserActions& actions);

	Map<String, bool> expandedAssets;
	Ursa::Array<char, 256> searchText{};
	Ursa::Array<char, 256> destinationPath{};
	Ursa::Array<char, 128> folderName{};
	bool createFolderRequested = false;
	bool renameRequested = false;
};
} // namespace Ursa::Editor
