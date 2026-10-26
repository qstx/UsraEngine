#include <Assets/ProjectBrowserModel.hpp>

#include <algorithm>
#include <cctype>

namespace Ursa::Editor
{
namespace
{
constexpr Size MaximumNavigationHistory = 64;

String ParentPath(const String& path)
{
	const Ursa::Size separator = path.rfind('/');
	return separator == String::npos ? String{} : path.substr(0, separator);
}

String FileName(const String& path)
{
	const Ursa::Size separator = path.rfind('/');
	return separator == String::npos ? path : path.substr(separator + 1);
}

String FoldCase(String value)
{
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return value;
}

struct ProjectSearchQuery
{
	Vector<String> terms;
	Vector<String> types;
	Vector<String> labels;
	bool HasFilter() const
	{
		return !terms.empty() || !types.empty() || !labels.empty();
	}
};

ProjectSearchQuery ParseProjectSearch(const String& search)
{
	ProjectSearchQuery query;
	String token;
	const auto finishToken = [&]
	{
		if (token.empty())
		{
			return;
		}
		const String folded = FoldCase(token);
		if (folded.size() > 2 && folded.compare(0, 2, "t:") == 0)
		{
			query.types.push_back(folded.substr(2));
		}
		else if (folded.size() > 2 && folded.compare(0, 2, "l:") == 0)
		{
			query.labels.push_back(folded.substr(2));
		}
		else
		{
			query.terms.push_back(folded);
		}
		token.clear();
	};
	for (const unsigned char character : search)
	{
		if (std::isspace(character))
		{
			finishToken();
		}
		else
		{
			token.push_back(static_cast<char>(character));
		}
	}
	finishToken();
	return query;
}

bool MatchesTerms(const String& value, const ProjectSearchQuery& query)
{
	const String folded = FoldCase(value);
	return std::all_of(query.terms.begin(), query.terms.end(),
	                   [&](const String& term) { return folded.find(term) != String::npos; });
}

bool MatchesTypeName(const String& kind, const String& filter)
{
	const String folded = FoldCase(kind);
	if (folded == filter)
	{
		return true;
	}
	if (filter == "texture2d" || filter == "texture3d")
	{
		return folded == "texture";
	}
	if (filter == "model")
	{
		return folded == "mesh" || folded == "model";
	}
	if (filter == "gameobject")
	{
		return folded == "prefab" || folded == "gameobject";
	}
	return false;
}

bool MatchesResourceKind(std::optional<ResourceKind> kind, const String& filter)
{
	if (!kind)
	{
		return false;
	}
	switch (*kind)
	{
		case ResourceKind::Texture:
			return MatchesTypeName("Texture", filter);
		case ResourceKind::Material:
			return MatchesTypeName("Material", filter);
		case ResourceKind::Scene:
			return MatchesTypeName("Scene", filter);
		case ResourceKind::Prefab:
			return MatchesTypeName("Prefab", filter);
		case ResourceKind::Shader:
			return MatchesTypeName("Shader", filter);
		case ResourceKind::Mesh:
			return MatchesTypeName("Mesh", filter);
	}
	return false;
}

bool MatchesImportedType(const ImportedArtifactRecord& object, const String& filter)
{
	return object.visibleInProject && MatchesTypeName(object.kind, filter);
}

bool MatchesEntryText(const ProjectBrowserEntry& entry, const ProjectSearchQuery& query)
{
	if (!query.terms.empty() && !MatchesTerms(entry.path.relative, query) &&
	    !std::any_of(entry.importedObjects.begin(), entry.importedObjects.end(), [&](const ImportedArtifactRecord& object)
	                 { return object.visibleInProject && !object.isMainObject && MatchesTerms(object.Label(), query); }))
	{
		return false;
	}
	if (!query.labels.empty() &&
	    !std::any_of(query.labels.begin(), query.labels.end(), [&](const String& filter)
	                 {
		                 return std::any_of(entry.labels.begin(), entry.labels.end(), [&](const String& label)
	                                    { return FoldCase(label) == filter; });
	                 }))
	{
		return false;
	}
	return true;
}

bool MatchesEntryType(const ProjectBrowserEntry& entry, const ProjectSearchQuery& query)
{
	if (query.types.empty())
	{
		return true;
	}
	return std::any_of(query.types.begin(), query.types.end(),
	                   [&](const String& type)
	                   {
		                   if (MatchesResourceKind(entry.kind, type))
		                   {
			                   return true;
		                   }
			                   const Size dot = entry.name.rfind('.');
			                   const String extension = dot == String::npos ? String{} : FoldCase(entry.name.substr(dot));
		                   if (type == "script" && extension == ".cs")
			                   return true;
		                   if (type == "folder" && entry.folder)
			                   return true;
		                   return std::any_of(entry.importedObjects.begin(), entry.importedObjects.end(),
			                                      [&](const ImportedArtifactRecord& object)
			                                      { return MatchesImportedType(object, type); });
	                   });
}

bool IsPathWithin(const String& path, const String& directory)
{
	return path.size() > directory.size() && path.compare(0, directory.size(), directory) == 0 &&
	       path[directory.size()] == '/';
}

} // namespace

Result<Vector<AssetMoveRequest>> PlanProjectFolderDrop(const AssetDatabase& database, const EditorDragSession& session,
                                                       const String& destinationFolder, bool readOnly)
{
	if (readOnly)
	{
		return Result<Vector<AssetMoveRequest>>::Failure(
		    "Project assets cannot be moved while the project is read-only");
	}
	if (session.sources.empty())
	{
		return Result<Vector<AssetMoveRequest>>::Failure("The drag contains no project asset sources");
	}
	if (!destinationFolder.empty())
	{
		auto folderPath = VirtualPath::Parse(database.SourceScheme() + ":" + destinationFolder);
		if (!folderPath)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(folderPath.error);
		}
		auto metadata = database.Metadata(folderPath.value);
		if (!metadata || !metadata.value.folder)
		{
			return Result<Vector<AssetMoveRequest>>::Failure("Move target is not an indexed project folder: " +
			                                                 destinationFolder);
		}
	}

	struct Source
	{
		VirtualPath path;
		ResourceId guid;
		bool folder = false;
	};

	Vector<Source> sources;
	Set<String> seenGuids;
	for (const EditorDragSource& dragSource : session.sources)
	{
		const EditorSelectionEntry& entry = dragSource.identity;
		if (entry.kind != EditorSelectionKind::Asset || entry.assetRole != EditorAssetSelectionRole::Source ||
		    entry.assetLocalFileId || !entry.assetGuid || entry.IsMissing() || dragSource.componentId)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(
			    "Project Move accepts only writable source assets and folders, not imported sub-assets");
		}
		if (!seenGuids.insert(entry.assetGuid.ToString()).second)
		{
			continue;
		}
		auto path = database.SourcePath(entry.assetGuid);
		if (!path || path.value != entry.assetPath || path.value.scheme != database.SourceScheme())
		{
			return Result<Vector<AssetMoveRequest>>::Failure("A dragged source changed or disappeared during the move");
		}
		auto metadata = database.Metadata(path.value);
		if (!metadata)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(metadata.error);
		}
		auto writable = database.Filesystem().CheckWritable(path.value);
		if (!writable)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(writable.error);
		}
		sources.push_back({path.value, entry.assetGuid, metadata.value.folder});
	}
	if (sources.empty())
	{
		return Result<Vector<AssetMoveRequest>>::Failure("The drag contains no writable project assets");
	}

	Vector<AssetMoveRequest> plan;
	Set<String> destinations;
	for (const Source& source : sources)
	{
		const bool nestedInSelectedFolder =
		    std::any_of(sources.begin(), sources.end(),
		                [&](const Source& ancestor)
		                {
			                return ancestor.folder && ancestor.path != source.path &&
			                       IsPathWithin(source.path.relative, ancestor.path.relative);
		                });
		if (nestedInSelectedFolder)
		{
			continue;
		}
		if (source.folder &&
		    (destinationFolder == source.path.relative || IsPathWithin(destinationFolder, source.path.relative)))
		{
			return Result<Vector<AssetMoveRequest>>::Failure(
			    "Cannot move a folder into itself or one of its descendants");
		}
		const Ursa::Size separator = source.path.relative.rfind('/');
		const String name = source.path.relative.substr(separator == String::npos ? 0 : separator + 1);
		const String relative = destinationFolder.empty() ? name : destinationFolder + "/" + name;
		if (relative == source.path.relative)
		{
			continue;
		}
		if (!destinations.insert(relative).second)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(
			    "Two dragged assets have the same name in the target folder: " + name);
		}
		auto destination = VirtualPath::Parse(database.SourceScheme() + ":" + relative);
		if (!destination)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(destination.error);
		}
		auto writable = database.Filesystem().CheckWritable(destination.value);
		if (!writable)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(writable.error);
		}
		auto destinationExists = database.Filesystem().Exists(destination.value);
		auto sidecar = VirtualPath::Parse(destination.value.ToString() + ".meta");
		auto sidecarExists =
		    sidecar ? database.Filesystem().Exists(sidecar.value) : Result<bool>::Failure(sidecar.error);
		if (!destinationExists || !sidecarExists)
		{
			return Result<Vector<AssetMoveRequest>>::Failure(!destinationExists ? destinationExists.error
			                                                                    : sidecarExists.error);
		}
		if (destinationExists.value || sidecarExists.value)
		{
			return Result<Vector<AssetMoveRequest>>::Failure("Move target already exists: " +
			                                                 destination.value.ToString());
		}
		plan.push_back({source.path, destination.value});
	}
	if (plan.empty())
	{
		return Result<Vector<AssetMoveRequest>>::Failure("The selected assets are already in the target folder");
	}
	return Result<Vector<AssetMoveRequest>>::Success(std::move(plan));
}

Result<Vector<VirtualPath>> PlanProjectSourceDelete(const AssetDatabase& database,
                                                    const Vector<EditorSelectionEntry>& selection, bool readOnly)
{
	if (readOnly)
	{
		return Result<Vector<VirtualPath>>::Failure("Project assets cannot be deleted while the project is read-only");
	}
	if (selection.empty())
	{
		return Result<Vector<VirtualPath>>::Failure("Select one or more Project source assets to delete");
	}

	struct Source
	{
		VirtualPath path;
		bool folder = false;
	};

	Vector<Source> sources;
	Set<String> seenGuids;
	for (const EditorSelectionEntry& entry : selection)
	{
		if (entry.kind != EditorSelectionKind::Asset || entry.assetRole != EditorAssetSelectionRole::Source ||
		    entry.assetLocalFileId || !entry.assetGuid || entry.IsMissing())
		{
			return Result<Vector<VirtualPath>>::Failure(
			    "Delete accepts only writable source assets and folders, not imported sub-assets");
		}
		if (!seenGuids.insert(entry.assetGuid.ToString()).second)
		{
			continue;
		}
		auto path = database.SourcePath(entry.assetGuid);
		if (!path || path.value != entry.assetPath || path.value.scheme != database.SourceScheme())
		{
			return Result<Vector<VirtualPath>>::Failure("A selected source changed or disappeared before deletion");
		}
		auto metadata = database.Metadata(path.value);
		if (!metadata)
		{
			return Result<Vector<VirtualPath>>::Failure(metadata.error);
		}
		auto writable = database.Filesystem().CheckWritable(path.value);
		if (!writable)
		{
			return Result<Vector<VirtualPath>>::Failure(writable.error);
		}
		sources.push_back({path.value, metadata.value.folder});
	}
	Vector<VirtualPath> plan;
	for (const Source& source : sources)
	{
		const bool nestedInSelectedFolder =
		    std::any_of(sources.begin(), sources.end(),
		                [&](const Source& ancestor)
		                {
			                return ancestor.folder && ancestor.path != source.path &&
			                       IsPathWithin(source.path.relative, ancestor.path.relative);
		                });
		if (!nestedInSelectedFolder)
		{
			plan.push_back(source.path);
		}
	}
	return plan.empty() ? Result<Vector<VirtualPath>>::Failure("The selected Project sources are no longer deletable")
	                    : Result<Vector<VirtualPath>>::Success(std::move(plan));
}

Result<Vector<VirtualPath>> PlanProjectSourceReimport(const AssetDatabase& database,
                                                      const Vector<EditorSelectionEntry>& selection, bool readOnly)
{
	if (readOnly)
	{
		return Result<Vector<VirtualPath>>::Failure(
		    "Project assets cannot be reimported while the project is read-only");
	}
	if (selection.empty())
	{
		return Result<Vector<VirtualPath>>::Failure("Select one or more Project source assets to reimport");
	}
	Vector<VirtualPath> roots;
	Set<String> seen;
	for (const auto& entry : selection)
	{
		if (entry.kind != EditorSelectionKind::Asset || entry.assetRole != EditorAssetSelectionRole::Source ||
		    entry.assetLocalFileId || !entry.assetGuid || entry.IsMissing())
		{
			return Result<Vector<VirtualPath>>::Failure(
			    "Reimport accepts source assets and folders, not imported sub-assets");
		}
		if (!seen.insert(entry.assetGuid.ToString()).second)
		{
			continue;
		}
		auto path = database.SourcePath(entry.assetGuid);
		if (!path || path.value != entry.assetPath || path.value.scheme != database.SourceScheme())
		{
			return Result<Vector<VirtualPath>>::Failure("A selected source changed or disappeared before reimport");
		}
		auto metadata = database.Metadata(path.value);
		if (!metadata)
		{
			return Result<Vector<VirtualPath>>::Failure(metadata.error);
		}
		auto writable = database.Filesystem().CheckWritable(path.value);
		if (!writable)
		{
			return Result<Vector<VirtualPath>>::Failure(writable.error);
		}
		roots.push_back(path.value);
	}
	Vector<VirtualPath> plan;
	Set<String> planned;
	for (const auto& asset : database.Assets())
	{
		if (asset.metadata.folder)
		{
			continue;
		}
		const bool included = std::any_of(roots.begin(), roots.end(),
		                                  [&](const VirtualPath& root)
		                                  {
			                                  return root == asset.path ||
			                                         (database.Metadata(root) && database.Metadata(root).value.folder &&
			                                          asset.path.relative.rfind(root.relative + "/", 0) == 0);
		                                  });
		if (!included)
		{
			continue;
		}
		if (!database.Filesystem().CheckWritable(asset.path))
		{
			return Result<Vector<VirtualPath>>::Failure("A selected folder contains a read-only source: " +
			                                            asset.path.ToString());
		}
		if (planned.insert(asset.path.ToString()).second)
		{
			plan.push_back(asset.path);
		}
	}
	if (plan.empty())
	{
		return Result<Vector<VirtualPath>>::Failure("The selection contains no source files to reimport");
	}
	return Result<Vector<VirtualPath>>::Success(std::move(plan));
}

Vector<ImportedArtifactRecord> AssetObjectChoices(const ProjectWorkspace& workspace)
{
	return QueryAssetObjectChoices(workspace, {});
}

Vector<ImportedArtifactRecord>
QueryAssetObjectChoices(const ProjectWorkspace& workspace,
                        const std::function<bool(const ImportedArtifactRecord&)>& predicate)
{
	Vector<ImportedArtifactRecord> result;
	Set<String> mainGuids;
	for (const auto* database : {workspace.Database(), workspace.BuiltinDatabase()})
	{
		if (!database)
		{
			continue;
		}
		for (auto object : database->VisibleImportedObjects())
		{
			if (object.isMainObject)
			{
				mainGuids.insert(object.guid.ToString());
			}
			result.push_back(std::move(object));
		}
	}
	for (const auto& entry : workspace.Resources())
	{
		if (!mainGuids.insert(entry.id.ToString()).second)
		{
			continue;
		}
		String kind;
		switch (entry.location.kind)
		{
			case ResourceKind::Mesh:
				kind = "Mesh";
				break;
			case ResourceKind::Material:
				kind = "Material";
				break;
			case ResourceKind::Texture:
				kind = "Texture";
				break;
			case ResourceKind::Prefab:
				kind = "Prefab";
				break;
			case ResourceKind::Scene:
				kind = "Scene";
				break;
			case ResourceKind::Shader:
				kind = "Shader";
				break;
		}
		result.push_back({entry.id,
		                  0,
		                  kind,
		                  entry.location.path,
		                  true,
		                  entry.location.path.FileName(),
		                  true,
		                  entry.location.path,
		                  {}});
	}
	if (predicate)
	{
		result.erase(std::remove_if(result.begin(), result.end(),
		                            [&](const ImportedArtifactRecord& object) { return !predicate(object); }),
		             result.end());
	}
	return result;
}

Vector<ImportedArtifactRecord> ProjectBrowserEntry::VisibleChildren(const String& search) const
{
	Vector<ImportedArtifactRecord> children;
	const ProjectSearchQuery query = ParseProjectSearch(search);
	for (const auto& object : importedObjects)
	{
		const bool matchesType = query.types.empty() ||
		                         std::any_of(query.types.begin(), query.types.end(),
		                                     [&](const String& type) { return MatchesImportedType(object, type); });
		const bool matchesText = MatchesTerms(object.Label(), query);
		if (!object.isMainObject && object.visibleInProject && matchesType && matchesText)
		{
			children.push_back(object);
		}
	}
	std::sort(children.begin(), children.end(),
	          [](const auto& left, const auto& right)
	          {
		          if (left.displayName != right.displayName)
		          {
			          return left.displayName < right.displayName;
		          }
		          return left.localFileId < right.localFileId;
	          });
	return children;
}

Result<void> ProjectBrowserModel::Refresh(const ProjectWorkspace& workspace)
{
	const String previousFolder = currentFolder;
	const VirtualPath previousSelection = selectedAsset;
	Map<String, ResourceKind> kindsByPath;
	Map<String, ResourceKind> importedKinds;
	Map<String, Vector<ImportedArtifactRecord>> importedObjects;
	if (const auto* database = workspace.Database())
	{
		for (const auto& artifact : database->ImportedArtifacts())
		{
			importedObjects[artifact.guid.ToString()].push_back(artifact);
			if (artifact.isMainObject)
			{
				auto kind = ResourceKindForPath(artifact.path);
				if (kind)
				{
					importedKinds[artifact.guid.ToString()] = kind.value;
				}
			}
		}
	}
	for (const ResourceEntry& resource : workspace.Resources())
	{
		if (resource.location.path.scheme == projectResourceScheme)
		{
			kindsByPath.emplace(resource.location.path.relative, resource.location.kind);
		}
	}
	entries.clear();
	childrenByFolder.clear();
	for (const AssetRecord& asset : workspace.Assets())
	{
		ProjectBrowserEntry entry;
		entry.path = asset.path;
		entry.guid = asset.metadata.guid;
		entry.name = FileName(asset.path.relative);
		entry.folder = asset.metadata.folder;
		entry.importer = asset.metadata.importer;
		entry.labels = asset.metadata.labels;
		const auto objects = importedObjects.find(entry.guid.ToString());
		if (objects != importedObjects.end())
		{
			entry.importedObjects = objects->second;
		}
		const auto kind = kindsByPath.find(asset.path.relative);
		if (kind != kindsByPath.end())
		{
			entry.kind = kind->second;
		}
		else if (!entry.folder)
		{
			const auto imported = importedKinds.find(entry.guid.ToString());
			auto inferred = ResourceKindForPath(asset.path);
			if (imported != importedKinds.end())
			{
				entry.kind = imported->second;
			}
			else if (inferred)
			{
				entry.kind = inferred.value;
			}
		}
		entries.emplace(asset.path.relative, std::move(entry));
	}
	for (const auto& item : entries)
	{
		AddChild(ParentPath(item.first), item.second);
	}
	for (auto& children : childrenByFolder)
	{
		std::sort(children.second.begin(), children.second.end(),
		          [&](const String& left, const String& right)
		          {
			          const auto& leftEntry = entries.at(left);
			          const auto& rightEntry = entries.at(right);
			          if (leftEntry.folder != rightEntry.folder)
			          {
				          return leftEntry.folder;
			          }
			          return FoldCase(leftEntry.name) < FoldCase(rightEntry.name);
		          });
	}
	String folderCandidate = previousFolder;
	auto navigated = NavigateTo(folderCandidate, false);
	while (!navigated && !folderCandidate.empty())
	{
		folderCandidate = ParentPath(folderCandidate);
		navigated = NavigateTo(folderCandidate, false);
	}
	navigationHistory.erase(std::remove_if(navigationHistory.begin(), navigationHistory.end(),
	                                       [&](const String& path)
	                                       {
		                                       const auto found = entries.find(path);
		                                       return !path.empty() &&
		                                              (found == entries.end() || !found->second.folder);
	                                       }),
	                        navigationHistory.end());
	if (navigationHistory.empty())
	{
		navigationHistory.push_back(currentFolder);
	}
	const auto currentHistory = std::find(navigationHistory.rbegin(), navigationHistory.rend(), currentFolder);
	if (currentHistory == navigationHistory.rend())
	{
		navigationHistory.push_back(currentFolder);
		navigationHistoryIndex = navigationHistory.size() - 1;
	}
	else
	{
		navigationHistoryIndex =
		    navigationHistory.size() - 1 - static_cast<Size>(std::distance(navigationHistory.rbegin(), currentHistory));
	}
	if (selectedGuid && Find(selectedGuid))
	{
		selectedAsset = Find(selectedGuid)->path;
	}
	else if (!previousSelection.scheme.empty() && Find(previousSelection))
	{
		selectedAsset = previousSelection;
	}
	else
	{
		selectedAsset = {};
	}
	assetDatabaseRevision = workspace.Database() ? workspace.Database()->Revision() : 0;
	return {};
}

Result<void> ProjectBrowserModel::NavigateTo(const String& relativePath, bool recordHistory)
{
	if (!relativePath.empty())
	{
		const auto found = entries.find(relativePath);
		if (found == entries.end() || !found->second.folder)
		{
			return Result<void>::Failure("Project folder does not exist: " + relativePath);
		}
	}
	if (!recordHistory)
	{
		currentFolder = relativePath;
		return {};
	}
	if (navigationHistory.empty())
	{
		navigationHistory.push_back(currentFolder);
		navigationHistoryIndex = 0;
	}
	if (relativePath == currentFolder)
	{
		return {};
	}
	if (navigationHistoryIndex + 1 < navigationHistory.size())
	{
		navigationHistory.erase(navigationHistory.begin() + static_cast<std::ptrdiff_t>(navigationHistoryIndex + 1),
		                        navigationHistory.end());
	}
	currentFolder = relativePath;
	navigationHistory.push_back(currentFolder);
	if (navigationHistory.size() > MaximumNavigationHistory)
	{
		navigationHistory.erase(navigationHistory.begin());
	}
	navigationHistoryIndex = navigationHistory.size() - 1;
	return {};
}

Result<void> ProjectBrowserModel::NavigateBack()
{
	if (!CanNavigateBack())
	{
		return Result<void>::Failure("Project navigation history has no previous folder");
	}
	--navigationHistoryIndex;
	currentFolder = navigationHistory[navigationHistoryIndex];
	return {};
}

Result<void> ProjectBrowserModel::NavigateForward()
{
	if (!CanNavigateForward())
	{
		return Result<void>::Failure("Project navigation history has no next folder");
	}
	++navigationHistoryIndex;
	currentFolder = navigationHistory[navigationHistoryIndex];
	return {};
}

bool ProjectBrowserModel::CanNavigateBack() const
{
	return !navigationHistory.empty() && navigationHistoryIndex > 0;
}

bool ProjectBrowserModel::CanNavigateForward() const
{
	return !navigationHistory.empty() && navigationHistoryIndex + 1 < navigationHistory.size();
}

void ProjectBrowserModel::SelectAsset(const VirtualPath& path)
{
	selectedAsset = path;
	const auto* entry = Find(path);
	selectedGuid = entry ? entry->guid : ResourceId{};
}

void ProjectBrowserModel::ClearAssetSelection()
{
	selectedAsset = {};
	selectedGuid = {};
}

Vector<ProjectBrowserEntry> ProjectBrowserModel::ChildFolders(const String& folder) const
{
	Vector<ProjectBrowserEntry> result;
	const auto found = childrenByFolder.find(folder);
	if (found == childrenByFolder.end())
	{
		return result;
	}
	for (const String& child : found->second)
	{
		const auto entry = entries.find(child);
		if (entry != entries.end() && entry->second.folder)
		{
			result.push_back(entry->second);
		}
	}
	return result;
}

Vector<ProjectBrowserEntry> ProjectBrowserModel::ChildAssets(const String& folder, const String& search) const
{
	Vector<ProjectBrowserEntry> result;
	const ProjectSearchQuery query = ParseProjectSearch(search);
	if (query.HasFilter())
	{
		for (const auto& item : entries)
		{
			if (item.second.folder)
			{
				continue;
			}
			if (MatchesEntryType(item.second, query) && MatchesEntryText(item.second, query))
			{
				result.push_back(item.second);
			}
		}
		std::sort(result.begin(), result.end(), [](const ProjectBrowserEntry& left, const ProjectBrowserEntry& right)
		          { return FoldCase(left.path.relative) < FoldCase(right.path.relative); });
		return result;
	}
	const auto found = childrenByFolder.find(folder);
	if (found == childrenByFolder.end())
	{
		return result;
	}
	for (const String& child : found->second)
	{
		const auto entry = entries.find(child);
		if (entry != entries.end() && !entry->second.folder)
		{
			result.push_back(entry->second);
		}
	}
	return result;
}

const ProjectBrowserEntry* ProjectBrowserModel::Find(const VirtualPath& path) const
{
	if (path.scheme != projectResourceScheme)
	{
		return nullptr;
	}
	const auto found = entries.find(path.relative);
	return found == entries.end() ? nullptr : &found->second;
}

bool ProjectBrowserModel::NeedsRefresh(const ProjectWorkspace& workspace) const
{
	return workspace.Database() && assetDatabaseRevision != workspace.Database()->Revision();
}

const ProjectBrowserEntry* ProjectBrowserModel::Find(const String& relativePath) const
{
	const auto found = entries.find(relativePath);
	return found == entries.end() ? nullptr : &found->second;
}

const ProjectBrowserEntry* ProjectBrowserModel::Find(ResourceId guid) const
{
	for (const auto& entry : entries)
	{
		if (entry.second.guid == guid)
		{
			return &entry.second;
		}
	}
	return nullptr;
}

void ProjectBrowserModel::ReconcileSelection(EditorSelectionContext& selection)
{
	if (selection.entries.empty())
	{
		return;
	}
	auto reconciled = selection.entries;
	for (auto& selected : reconciled)
	{
		if (selected.kind != EditorSelectionKind::Asset)
		{
			continue;
		}
		const auto* entry = selected.assetGuid ? Find(selected.assetGuid) : Find(selected.assetPath);
		selected.assetMissing = !entry;
		selected.missing = selected.assetMissing;
		if (!entry)
		{
			continue;
		}
		selected.assetGuid = entry->guid;
		selected.assetPath = entry->path;
		if (selected.assetLocalFileId)
		{
			const auto object =
			    std::find_if(entry->importedObjects.begin(), entry->importedObjects.end(),
			                 [&](const auto& item) { return item.localFileId == selected.assetLocalFileId; });
			selected.assetMissing = object == entry->importedObjects.end();
			selected.missing = selected.assetMissing;
			if (!selected.assetMissing)
			{
				selected.assetKind = object->kind;
			}
		}
	}
	selection.ReconcileEntries(std::move(reconciled));
	const auto* primary = selection.PrimaryEntry();
	if (primary && primary->kind == EditorSelectionKind::Asset)
	{
		selectedAsset = primary->assetPath;
		selectedGuid = primary->assetGuid;
	}
	else
	{
		selectedAsset = {};
		selectedGuid = {};
	}
}

void ProjectBrowserModel::AddChild(const String& parent, const ProjectBrowserEntry& entry)
{
	childrenByFolder[parent].push_back(entry.path.relative);
}

} // namespace Ursa::Editor
