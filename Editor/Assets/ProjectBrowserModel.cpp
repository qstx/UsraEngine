#include <Assets/ProjectBrowserModel.hpp>

#include <algorithm>
#include <cctype>

namespace Ursa::Editor
{
namespace
{
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

} // namespace

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
	auto navigated = NavigateTo(folderCandidate);
	while (!navigated && !folderCandidate.empty())
	{
		folderCandidate = ParentPath(folderCandidate);
		navigated = NavigateTo(folderCandidate);
	}
	if (!previousSelection.scheme.empty() && Find(previousSelection))
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

Result<void> ProjectBrowserModel::NavigateTo(const String& relativePath)
{
	if (relativePath.empty())
	{
		currentFolder.clear();
		return {};
	}
	const auto found = entries.find(relativePath);
	if (found == entries.end() || !found->second.folder)
	{
		return Result<void>::Failure("Project folder does not exist: " + relativePath);
	}
	currentFolder = relativePath;
	return {};
}

void ProjectBrowserModel::SelectAsset(const VirtualPath& path)
{
	selectedAsset = path;
}

void ProjectBrowserModel::ClearAssetSelection()
{
	selectedAsset = {};
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
	const String foldedSearch = FoldCase(search);
	if (!foldedSearch.empty())
	{
		for (const auto& item : entries)
		{
			if (item.second.folder)
			{
				continue;
			}
			const String foldedPath = FoldCase(item.first);
			if (foldedPath.find(foldedSearch) != String::npos)
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

void ProjectBrowserModel::AddChild(const String& parent, const ProjectBrowserEntry& entry)
{
	childrenByFolder[parent].push_back(entry.path.relative);
}

} // namespace Ursa::Editor
