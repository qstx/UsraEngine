#pragma once

#include <Assets/ProjectBrowserModel.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Workspace/EditorCommands.hpp>
#include <Workspace/EditorDragSession.hpp>
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
	std::function<Result<void>(const Vector<AssetMoveRequest>&)> moveAssets;
	std::function<Result<void>(const Vector<VirtualPath>&)> deleteAssets;
	std::function<Result<void>(const Vector<VirtualPath>&)> reimportAssets;
	std::function<Result<void>(const ProjectBrowserEntry&)> openAsset;
	std::function<Result<void>(const EditorDragSession&, const String&)> exportScenePrefabs;
};

class ProjectBrowserPanel final
{
public:
	void Reset();

	void RequestRenameSelected();
	void RequestDeleteSelected(const EditorSelectionContext& selection);

	Result<void> Draw(ProjectWorkspace& workspace, ProjectBrowserModel& model, EditorSelectionContext& selection,
	                  EditorHierarchy& hierarchy, bool& open, bool readOnly, String& diagnostic,
	                  const ProjectBrowserActions& actions);

private:
	Result<void> RenameSelected(const ProjectBrowserModel& model, EditorSelectionContext& selection,
	                            const ProjectBrowserActions& actions);
	void CaptureMenuContext(const String& key, const EditorSelectionContext& selection);
	Result<void> ExecuteMenuCommand(const String& key, const String& commandId, const String& label,
	                                const EditorSelectionContext& selection,
	                                const EditorCommandAvailability& availability,
	                                const EditorCommandAction& action) const;
	Result<void> CanExecuteMenuCommand(const String& key, const String& commandId, const String& label,
	                                   const EditorSelectionContext& selection,
	                                   const EditorCommandAvailability& availability) const;

	Map<String, bool> expandedAssets;
	Ursa::Array<char, 256> searchText{};
	Ursa::Array<char, 256> destinationPath{};
	Ursa::Array<char, 128> folderName{};
	bool createFolderRequested = false;
	bool renameRequested = false;
	bool deleteRequested = false;
	Vector<EditorSelectionEntry> deleteTargets;
	String deleteDiagnostic;
	String backgroundMenuFolder;
	Map<String, EditorCommandContext> contextMenuTargets;
};
} // namespace Ursa::Editor
