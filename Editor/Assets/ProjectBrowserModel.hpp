#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <functional>
#include <optional>

namespace Ursa::Editor
{
Vector<ImportedArtifactRecord> AssetObjectChoices(const ProjectWorkspace& workspace);
Vector<ImportedArtifactRecord>
QueryAssetObjectChoices(const ProjectWorkspace& workspace,
                        const std::function<bool(const ImportedArtifactRecord&)>& predicate);
Result<Vector<AssetMoveRequest>> PlanProjectFolderDrop(const AssetDatabase& database, const EditorDragSession& session,
                                                       const String& destinationFolder, bool readOnly);
Result<Vector<VirtualPath>> PlanProjectSourceDelete(const AssetDatabase& database,
                                                    const Vector<EditorSelectionEntry>& selection, bool readOnly);
Result<Vector<VirtualPath>> PlanProjectSourceReimport(const AssetDatabase& database,
                                                      const Vector<EditorSelectionEntry>& selection, bool readOnly);

struct ProjectBrowserEntry
{
	VirtualPath path;
	ResourceId guid;
	String name;
	bool folder = false;
	std::optional<ResourceKind> kind;
	String importer;
	Vector<String> labels;
	Vector<ImportedArtifactRecord> importedObjects;
	Vector<ImportedArtifactRecord> VisibleChildren(const String& search = {}) const;
};

class ProjectBrowserModel final
{
public:
	Result<void> Refresh(const ProjectWorkspace& workspace);

	Result<void> NavigateTo(const String& relativePath, bool recordHistory = true);
	Result<void> NavigateBack();
	Result<void> NavigateForward();
	bool CanNavigateBack() const;
	bool CanNavigateForward() const;

	void SelectAsset(const VirtualPath& path);

	void ClearAssetSelection();

	const String& CurrentFolder() const
	{
		return currentFolder;
	}

	const VirtualPath& SelectedAsset() const
	{
		return selectedAsset;
	}

	Vector<ProjectBrowserEntry> ChildFolders(const String& folder) const;

	Vector<ProjectBrowserEntry> ChildAssets(const String& folder, const String& search = {}) const;

	const ProjectBrowserEntry* Find(const VirtualPath& path) const;
	const ProjectBrowserEntry* Find(const String& relativePath) const;
	const ProjectBrowserEntry* Find(ResourceId guid) const;
	void ReconcileSelection(EditorSelectionContext& selection);

	bool NeedsRefresh(const ProjectWorkspace& workspace) const;

private:
	void AddChild(const String& parent, const ProjectBrowserEntry& entry);

	Map<String, ProjectBrowserEntry> entries;
	Map<String, Vector<String>> childrenByFolder;
	String currentFolder;
	Vector<String> navigationHistory;
	Size navigationHistoryIndex = 0;
	VirtualPath selectedAsset;
	ResourceId selectedGuid;
	Ursa::UInt64 assetDatabaseRevision = 0;
};
} // namespace Ursa::Editor
