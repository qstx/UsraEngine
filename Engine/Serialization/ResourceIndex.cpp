#include <Core/StringInterop.hpp>
#include <Serialization/ResourceIndex.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Ursa
{
Result<void> ResourceIndex::Mount(const ResourceMount& mount)
{
	auto added = mount.provider ? filesystem.MountProvider(mount.scheme, mount.provider, mount.access)
	                            : filesystem.Mount(mount.scheme, mount.root, mount.access);
	if (!added)
	{
		return added;
	}
	if (mount.provider)
	{
		mounts.push_back(mount);
		return {};
	}
	std::error_code error;
	const auto canonical = std::filesystem::canonical(mount.root, error);
	if (error)
	{
		return Result<void>::Failure("Resource root is not a readable directory: " + PathToUrsaString(mount.root));
	}
	const bool known = std::any_of(mounts.begin(), mounts.end(), [&](const ResourceMount& candidate)
	                               { return candidate.scheme == mount.scheme && candidate.root == canonical; });
	if (!known)
	{
		mounts.push_back(ResourceMount{mount.scheme, canonical, mount.access});
	}
	return {};
}

Result<void> ResourceIndex::Rebuild()
{
	Map<String, ResourceLocation> candidate;
	Map<String, VirtualPath> candidateCatalog;
	Map<String, UInt64> candidateMainObjects;
	Set<String> catalogGuids;
	bool catalogRead = false;
	for (const String& scheme : filesystem.Schemes())
	{
		if (scheme == userDataScheme || scheme == editorLibraryScheme || scheme == projectSettingsScheme)
		{
			continue;
		}
		auto listed = filesystem.List(scheme);
		if (!listed)
		{
			return Result<void>::Failure(listed.error);
		}
		for (const VirtualFileEntry& entry : listed.value)
		{
			if ((scheme == projectScheme || scheme == engineScheme) &&
			    entry.path.relative == "__UrsaCooked/AssetCatalog.ursacatalog")
			{
				if (catalogRead)
				{
					return Result<void>::Failure("Duplicate runtime asset catalog in project mounts");
				}
				auto text = filesystem.ReadText(entry.path);
				if (!text)
				{
					return Result<void>::Failure(text.error);
				}
				try
				{
					const nlohmann::json document = nlohmann::json::parse(ToStdString(text.value));
					if (!document.is_object() || document.value("format", std::string{}) != "UrsaAssetCatalog" ||
					    document.value("version", UInt32{0}) != 2 || !document.contains("assets") ||
					    !document["assets"].is_array())
					{
						return Result<void>::Failure("Runtime asset catalog has an unsupported schema at " +
						                             entry.path.ToString());
					}
					for (const nlohmann::json& asset : document["assets"])
					{
						if (!asset.is_object() || !asset.contains("guid") || !asset["guid"].is_string() ||
						    !asset.contains("localFileId") || !asset["localFileId"].is_number_unsigned() ||
						    !asset.contains("isMainObject") || !asset["isMainObject"].is_boolean() ||
						    !asset.contains("kind") || !asset["kind"].is_string() || !asset.contains("path") ||
						    !asset["path"].is_string())
						{
							return Result<void>::Failure("Runtime asset catalog contains a malformed entry at " +
							                             entry.path.ToString());
						}
						auto guid = ResourceId::Parse(ToUrsaString(asset["guid"].get<std::string>()));
						auto path = VirtualPath::Parse(ToUrsaString(asset["path"].get<std::string>()));
						if (!guid || !path || (path.value.scheme != projectScheme && path.value.scheme != engineScheme))
						{
							return Result<void>::Failure("Runtime asset catalog contains an invalid reference at " +
							                             entry.path.ToString());
						}
						auto exists = filesystem.Exists(path.value);
						if (!exists || !exists.value)
						{
							return Result<void>::Failure("Runtime asset catalog target is missing: " +
							                             path.value.ToString());
						}
						const auto localId = asset["localFileId"].get<UInt64>();
						const auto guidKey = guid.value.ToString();
						catalogGuids.insert(guidKey);
						if (asset["isMainObject"].get<bool>())
						{
							if (!candidateMainObjects.emplace(guidKey, localId).second)
							{
								return Result<void>::Failure("Multiple main objects for asset " + guidKey);
							}
						}
						else if (localId == 0)
						{
							return Result<void>::Failure("Zero local file ID is reserved for the main object: " +
							                             guidKey);
						}
						const String key = guidKey + ":" + ToUrsaString(std::to_string(localId));
						if (!candidateCatalog.emplace(key, std::move(path.value)).second)
						{
							return Result<void>::Failure(
							    "Runtime asset catalog contains duplicate GUID/local-file-ID entry " + key);
						}
					}
					catalogRead = true;
				}
				catch (const std::exception& error)
				{
					return Result<void>::Failure("Cannot parse runtime asset catalog: " + ToUrsaString(error.what()));
				}
				continue;
			}
			if (entry.path.relative.size() < 5 || entry.path.relative.substr(entry.path.relative.size() - 5) != ".meta")
			{
				continue;
			}
			const String assetRelative = entry.path.relative.substr(0, entry.path.relative.size() - 5);
			auto assetPath = VirtualPath::Parse(scheme + ":" + assetRelative);
			if (!assetPath)
			{
				return Result<void>::Failure(assetPath.error);
			}
			auto kind = ResourceKindForPath(assetPath.value);
			if (!kind)
			{
				continue;
			}
			auto exists = filesystem.Exists(assetPath.value);
			if (!exists || !exists.value)
			{
				return Result<void>::Failure("Resource metadata has no source resource at " +
				                             assetPath.value.ToString());
			}
			auto metadata = filesystem.ReadText(entry.path);
			if (!metadata)
			{
				return Result<void>::Failure(metadata.error);
			}
			try
			{
				const nlohmann::json document = nlohmann::json::parse(ToStdString(metadata.value));
				if (!document.is_object() || !document.contains("guid") || !document["guid"].is_string() ||
				    (document.contains("folder") &&
				     (!document["folder"].is_boolean() || document["folder"].get<bool>())))
				{
					return Result<void>::Failure("Resource metadata has an invalid GUID or folder flag at " +
					                             entry.path.ToString());
				}
				auto guid = ResourceId::Parse(ToUrsaString(document["guid"].get<std::string>()));
				if (!guid)
				{
					return Result<void>::Failure(guid.error + " at " + entry.path.ToString());
				}
				const auto key = guid.value.ToString();
				const auto found = candidate.find(key);
				if (found != candidate.end())
				{
					return Result<void>::Failure("Duplicate asset GUID " + key + " in " +
					                             found->second.path.ToString() + " and " + assetPath.value.ToString());
				}
				candidate.emplace(key, ResourceLocation{kind.value, std::move(assetPath.value)});
			}
			catch (const std::exception& error)
			{
				return Result<void>::Failure("Cannot parse resource metadata at " + entry.path.ToString() + ": " +
				                             ToUrsaString(error.what()));
			}
		}
	}
	for (const auto& guid : catalogGuids)
	{
		const auto main = candidateMainObjects.find(guid);
		if (main == candidateMainObjects.end())
		{
			return Result<void>::Failure("Missing main object for asset " + guid);
		}
		if (main->second != 0)
		{
			const auto path = candidateCatalog.find(guid + ":" + ToUrsaString(std::to_string(main->second)));
			if (path == candidateCatalog.end() || !candidateCatalog.emplace(guid + ":0", path->second).second)
			{
				return Result<void>::Failure("Invalid main object alias for asset " + guid);
			}
		}
	}
	locations = std::move(candidate);
	catalogAssets = std::move(candidateCatalog);
	return {};
}

Result<ResourceLocation> ResourceIndex::Resolve(ResourceId id, const String& referencedBy) const
{
	const auto key = id.ToString();
	const auto found = locations.find(key);
	if (found == locations.end())
	{
		return Result<ResourceLocation>::Failure("Missing resource ID " + key +
		                                         (referencedBy.empty() ? String{} : " referenced by " + referencedBy));
	}
	return Result<ResourceLocation>::Success(found->second);
}

Result<VirtualPath> ResourceIndex::ResolveAsset(ResourceId guid, Ursa::UInt64 localFileId) const
{
	if (!guid)
	{
		return Result<VirtualPath>::Failure("Cannot resolve an empty asset GUID");
	}
	const String key = guid.ToString() + ":" + ToUrsaString(std::to_string(localFileId));
	const auto imported = catalogAssets.find(key);
	if (imported != catalogAssets.end())
	{
		return Result<VirtualPath>::Success(imported->second);
	}
	if (localFileId != 0)
	{
		return Result<VirtualPath>::Failure("Missing asset sub-resource GUID/local-file-ID " + key);
	}
	auto resource = Resolve(guid);
	return resource ? Result<VirtualPath>::Success(resource.value.path) : Result<VirtualPath>::Failure(resource.error);
}

Vector<ResourceEntry> ResourceIndex::Entries() const
{
	Vector<ResourceEntry> result;
	result.reserve(locations.size());
	for (const auto& entry : locations)
	{
		auto id = ResourceId::Parse(entry.first);
		if (id)
		{
			result.push_back({id.value, entry.second});
		}
	}
	std::sort(result.begin(), result.end(), [](const ResourceEntry& left, const ResourceEntry& right)
	          { return left.location.path.ToString() < right.location.path.ToString(); });
	return result;
}
} // namespace Ursa
