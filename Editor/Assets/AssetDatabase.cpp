#include <Assets/AssetDatabase.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/ResourceFile.hpp>
#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace Ursa::Editor
{
Vector<ImportedArtifactRecord> AssetDatabase::ImportedArtifacts() const
{
	Vector<ImportedArtifactRecord> records;
	for (const auto& entry : assetsByPath)
	{
		if (const auto* cached = FindImport(entry.second.metadata.guid.ToString()))
		{
			for (const auto& artifact : cached->artifacts)
			{
				records.push_back({entry.second.metadata.guid, artifact.localFileId, artifact.kind, artifact.path});
			}
		}
	}
	return records;
}

namespace
{
using Json = nlohmann::json;
constexpr Ursa::UInt32 importCacheVersion = 2;

Vector<Ursa::UInt8> BytesOf(const std::string& text)
{
	Vector<Ursa::UInt8> bytes;
	bytes.reserve(text.size());
	for (unsigned char character : text)
	{
		bytes.push_back(character);
	}
	return bytes;
}

bool EndsWith(const String& value, const char* suffix)
{
	const String ending = suffix;
	return value.size() >= ending.size() && value.compare(value.size() - ending.size(), ending.size(), ending) == 0;
}

Result<std::optional<ResourceId>> EmbeddedGuid(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	if (EndsWith(path.relative, ".ursashader"))
	{
		return Result<std::optional<ResourceId>>::Success(std::nullopt);
	}
	auto kind = ResourceKindForPath(path);
	if (!kind)
	{
		return Result<std::optional<ResourceId>>::Success(std::nullopt);
	}
	auto header = ReadResourceHeader(filesystem, path);
	return header ? Result<std::optional<ResourceId>>::Success(header.value.id)
	              : Result<std::optional<ResourceId>>::Failure(header.error);
}

String WithoutMetaSuffix(const String& path)
{
	return path.substr(0, path.size() - 5);
}

Result<AssetMetadata> DecodeMetadata(const String& text, const VirtualPath& sidecar)
{
	try
	{
		const Json document = Json::parse(ToStdString(text));
		if (!document.is_object() || !document.contains("version") || !document["version"].is_number_unsigned())
		{
			return Result<AssetMetadata>::Failure("Asset metadata has no valid version at " + sidecar.ToString());
		}
		const auto version = document["version"].get<Ursa::UInt32>();
		if (version != assetMetadataVersion)
		{
			return Result<AssetMetadata>::Failure("Unsupported asset metadata version " +
			                                      ToUrsaString(std::to_string(version)) + " at " + sidecar.ToString());
		}
		if (!document.contains("guid") || !document["guid"].is_string())
		{
			return Result<AssetMetadata>::Failure("Asset metadata has no GUID at " + sidecar.ToString());
		}
		auto guid = ResourceId::Parse(ToUrsaString(document["guid"].get<std::string>()));
		if (!guid)
		{
			return Result<AssetMetadata>::Failure(guid.error + " at " + sidecar.ToString());
		}
		AssetMetadata metadata;
		metadata.guid = guid.value;
		if (document.contains("folder"))
		{
			if (!document["folder"].is_boolean())
			{
				return Result<AssetMetadata>::Failure("Asset metadata folder flag is invalid at " + sidecar.ToString());
			}
			metadata.folder = document["folder"].get<bool>();
		}
		if (document.contains("importer"))
		{
			if (!document["importer"].is_string())
			{
				return Result<AssetMetadata>::Failure("Asset metadata importer is invalid at " + sidecar.ToString());
			}
			metadata.importer = ToUrsaString(document["importer"].get<std::string>());
		}
		if (document.contains("importerVersion"))
		{
			if (!document["importerVersion"].is_number_unsigned())
			{
				return Result<AssetMetadata>::Failure("Asset metadata importer version is invalid at " +
				                                      sidecar.ToString());
			}
			metadata.importerVersion = document["importerVersion"].get<Ursa::UInt32>();
		}
		if (document.contains("settings"))
		{
			if (!document["settings"].is_object())
			{
				return Result<AssetMetadata>::Failure("Asset metadata settings must be an object at " +
				                                      sidecar.ToString());
			}
			metadata.importSettings = document["settings"];
		}
		return Result<AssetMetadata>::Success(std::move(metadata));
	}
	catch (const std::exception& error)
	{
		return Result<AssetMetadata>::Failure("Cannot parse asset metadata at " + sidecar.ToString() + ": " +
		                                      ToUrsaString(error.what()));
	}
}

Result<String> EncodeMetadata(const AssetMetadata& metadata)
{
	try
	{
		Json document = {{"version", assetMetadataVersion},
		                 {"guid", ToStdString(metadata.guid.ToString())},
		                 {"folder", metadata.folder},
		                 {"importer", ToStdString(metadata.importer)},
		                 {"importerVersion", metadata.importerVersion},
		                 {"settings", metadata.importSettings}};
		return Result<String>::Success(ToUrsaString(document.dump(2)));
	}
	catch (const std::exception& error)
	{
		return Result<String>::Failure("Cannot encode asset metadata: " + ToUrsaString(error.what()));
	}
}

bool IsPathWithin(const String& path, const String& directory)
{
	return path.size() > directory.size() && path.compare(0, directory.size(), directory) == 0 &&
	       path[directory.size()] == '/';
}

String ParentRelativePath(const String& path)
{
	const auto separator = path.rfind('/');
	return separator == String::npos ? String{} : path.substr(0, separator);
}

class FingerprintBuilder final
{
public:
	void Add(const String& text)
	{
		const Ursa::UInt64 length = text.size();
		AddBytes(reinterpret_cast<const Ursa::UInt8*>(&length), sizeof(length));
		AddBytes(reinterpret_cast<const Ursa::UInt8*>(text.data()), text.size());
	}

	void Add(const Vector<Ursa::UInt8>& bytes)
	{
		const Ursa::UInt64 length = bytes.size();
		AddBytes(reinterpret_cast<const Ursa::UInt8*>(&length), sizeof(length));
		AddBytes(bytes.data(), bytes.size());
	}

	String ToString() const
	{
		std::ostringstream output;
		output << std::hex << std::setfill('0') << std::setw(16) << first << std::setw(16) << second;
		return ToUrsaString(output.str());
	}

private:
	void AddBytes(const Ursa::UInt8* bytes, Ursa::Size size)
	{
		for (Ursa::Size index = 0; index < size; ++index)
		{
			first = (first ^ bytes[index]) * 1099511628211ull;
			second ^= static_cast<Ursa::UInt64>(bytes[index]) + 0x9e3779b97f4a7c15ull + (second << 6) + (second >> 2);
			second *= 0xbf58476d1ce4e5b9ull;
		}
	}

	Ursa::UInt64 first = 14695981039346656037ull;
	Ursa::UInt64 second = 0x6a09e667f3bcc909ull;
};

VirtualPath CacheManifestPath(const String& scheme)
{
	auto path = VirtualPath::Parse(String(editorLibraryScheme) + ":AssetDatabase/" + scheme + ".json");
	return path ? std::move(path.value) : VirtualPath{};
}
} // namespace

Result<std::unique_ptr<AssetDatabase>> AssetDatabase::Open(VirtualFileSystem& selectedFilesystem, String selectedScheme)
{
	if (selectedScheme.empty() || !selectedFilesystem.IsMounted(selectedScheme))
	{
		return Result<std::unique_ptr<AssetDatabase>>::Failure("Asset database source scheme is not mounted: " +
		                                                       selectedScheme);
	}
	return Result<std::unique_ptr<AssetDatabase>>::Success(
	    std::unique_ptr<AssetDatabase>(new AssetDatabase(selectedFilesystem, std::move(selectedScheme))));
}

Result<VirtualPath> AssetDatabase::SidecarPath(const VirtualPath& assetPath) const
{
	if (assetPath.scheme != sourceScheme)
	{
		return Result<VirtualPath>::Failure("Asset metadata is only writable for scheme " + sourceScheme + ": " +
		                                    assetPath.ToString());
	}
	return VirtualPath::Parse(assetPath.ToString() + ".meta");
}

Result<AssetMetadata> AssetDatabase::ReadMetadata(const VirtualPath& assetPath) const
{
	auto sidecar = SidecarPath(assetPath);
	if (!sidecar)
	{
		return Result<AssetMetadata>::Failure(sidecar.error);
	}
	auto text = filesystem.ReadText(sidecar.value);
	if (!text)
	{
		return Result<AssetMetadata>::Failure(text.error);
	}
	return DecodeMetadata(text.value, sidecar.value);
}

Result<void> AssetDatabase::WriteMetadata(const VirtualPath& assetPath, const AssetMetadata& metadata)
{
	if (!metadata.guid)
	{
		return Result<void>::Failure("Cannot write asset metadata with an empty GUID for " + assetPath.ToString());
	}
	auto sidecar = SidecarPath(assetPath);
	if (!sidecar)
	{
		return Result<void>::Failure(sidecar.error);
	}
	auto encoded = EncodeMetadata(metadata);
	if (!encoded)
	{
		return Result<void>::Failure(encoded.error);
	}
	return filesystem.WriteAtomic(sidecar.value, BytesOf(ToStdString(encoded.value)));
}

Result<void> AssetDatabase::LoadCache()
{
	cacheLoaded = true;
	importsByKey.clear();
	selectedImports.clear();
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return {};
	}
	const VirtualPath cachePath = CacheManifestPath(sourceScheme);
	auto exists = filesystem.Exists(cachePath);
	if (!exists)
	{
		return Result<void>::Failure(exists.error);
	}
	if (!exists.value)
	{
		return {};
	}
	auto text = filesystem.ReadText(cachePath);
	if (!text)
	{
		return Result<void>::Failure(text.error);
	}
	try
	{
		const Json document = Json::parse(ToStdString(text.value));
		if (!document.is_object() || !document.contains("version") ||
		    document["version"].get<Ursa::UInt32>() != importCacheVersion ||
		    document.value("sourceScheme", std::string{}) != ToStdString(sourceScheme) ||
		    !document.contains("imports") || !document["imports"].is_array())
		{
			return {};
		}
		for (const Json& entry : document["imports"])
		{
			if (!entry.is_object() || !entry.contains("guid") || !entry["guid"].is_string() ||
			    !entry.contains("importer") || !entry["importer"].is_string() || !entry.contains("importerVersion") ||
			    !entry["importerVersion"].is_number_unsigned() || !entry.contains("target") ||
			    !entry["target"].is_string() || !entry.contains("settings") || !entry["settings"].is_object() ||
			    !entry.contains("fingerprint") || !entry["fingerprint"].is_string() ||
			    !entry.contains("dependencies") || !entry["dependencies"].is_array() || !entry.contains("artifacts") ||
			    !entry["artifacts"].is_array())
			{
				continue;
			}
			auto guid = ResourceId::Parse(ToUrsaString(entry["guid"].get<std::string>()));
			if (!guid)
			{
				continue;
			}
			CachedImport cached;
			cached.importer = ToUrsaString(entry["importer"].get<std::string>());
			cached.importerVersion = entry["importerVersion"].get<Ursa::UInt32>();
			cached.target = ToUrsaString(entry["target"].get<std::string>());
			cached.settings = entry["settings"];
			cached.fingerprint = ToUrsaString(entry["fingerprint"].get<std::string>());
			bool valid = true;
			for (const Json& dependency : entry["dependencies"])
			{
				if (!dependency.is_string())
				{
					valid = false;
					break;
				}
				auto path = VirtualPath::Parse(ToUrsaString(dependency.get<std::string>()));
				if (!path)
				{
					valid = false;
					break;
				}
				cached.dependencies.push_back(std::move(path.value));
			}
			for (const Json& artifact : entry["artifacts"])
			{
				if (!artifact.is_object() || !artifact.contains("localFileId") ||
				    !artifact["localFileId"].is_number_unsigned() || !artifact.contains("kind") ||
				    !artifact["kind"].is_string() || !artifact.contains("path") || !artifact["path"].is_string())
				{
					valid = false;
					break;
				}
				auto path = VirtualPath::Parse(ToUrsaString(artifact["path"].get<std::string>()));
				if (!path || path.value.scheme != editorLibraryScheme)
				{
					valid = false;
					break;
				}
				cached.artifacts.push_back({artifact["localFileId"].get<Ursa::UInt64>(),
				                            ToUrsaString(artifact["kind"].get<std::string>()), std::move(path.value),
				                            ToUrsaString(artifact.value("contentFingerprint", std::string{})),
				                            artifact.value("size", Ursa::UInt64{0})});
			}
			if (valid && !cached.artifacts.empty())
			{
				const String key = guid.value.ToString() + ":" + cached.target;
				if (entry.value("selected", false))
				{
					selectedImports[guid.value.ToString()] = key;
				}
				importsByKey.emplace(key, std::move(cached));
			}
		}
	}
	catch (const std::exception&)
	{
		importsByKey.clear();
		selectedImports.clear();
	}
	return {};
}

Result<void> AssetDatabase::SaveCache(const Map<String, CachedImport>& imports) const
{
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return Result<void>::Failure("Editor import cache scheme is not mounted: " + String(editorLibraryScheme));
	}
	try
	{
		Json document;
		document["version"] = importCacheVersion;
		document["sourceScheme"] = ToStdString(sourceScheme);
		document["imports"] = Json::array();
		for (const auto& item : imports)
		{
			Json entry;
			const String guid = item.first.substr(0, item.first.find(':'));
			entry["guid"] = ToStdString(guid);
			const auto selected = selectedImports.find(guid);
			entry["selected"] = selected != selectedImports.end() && selected->second == item.first;
			entry["importer"] = ToStdString(item.second.importer);
			entry["importerVersion"] = item.second.importerVersion;
			entry["target"] = ToStdString(item.second.target);
			entry["settings"] = item.second.settings;
			entry["fingerprint"] = ToStdString(item.second.fingerprint);
			entry["dependencies"] = Json::array();
			for (const VirtualPath& dependency : item.second.dependencies)
			{
				entry["dependencies"].push_back(ToStdString(dependency.ToString()));
			}
			entry["artifacts"] = Json::array();
			for (const ImportedArtifactLocation& artifact : item.second.artifacts)
			{
				entry["artifacts"].push_back({{"localFileId", artifact.localFileId},
				                              {"kind", ToStdString(artifact.kind)},
				                              {"path", ToStdString(artifact.path.ToString())},
				                              {"contentFingerprint", ToStdString(artifact.contentFingerprint)},
				                              {"size", artifact.size}});
			}
			document["imports"].push_back(std::move(entry));
		}
		return filesystem.WriteAtomic(CacheManifestPath(sourceScheme), BytesOf(document.dump(2)));
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Cannot encode Editor import cache: " + ToUrsaString(error.what()));
	}
}

Result<void> AssetDatabase::Refresh()
{
	if (!cacheLoaded)
	{
		auto loaded = LoadCache();
		if (!loaded)
		{
			return loaded;
		}
	}
	auto listed = filesystem.List(sourceScheme);
	if (!listed)
	{
		return Result<void>::Failure(listed.error);
	}

	Map<String, bool> candidates;
	Map<String, AssetMetadata> existingMetadata;
	Map<String, bool> listedFiles;
	for (const VirtualFileEntry& entry : listed.value)
	{
		listedFiles.emplace(entry.path.relative, true);
		if (EndsWith(entry.path.relative, ".meta"))
		{
			const String assetRelative = WithoutMetaSuffix(entry.path.relative);
			auto text = filesystem.ReadText(entry.path);
			if (!text)
			{
				return Result<void>::Failure(text.error);
			}
			auto metadata = DecodeMetadata(text.value, entry.path);
			if (!metadata)
			{
				return Result<void>::Failure(metadata.error);
			}
			existingMetadata.emplace(assetRelative, std::move(metadata.value));
		}
		else
		{
			candidates[entry.path.relative] = false;
			String::size_type separator = entry.path.relative.find('/');
			while (separator != String::npos)
			{
				candidates[entry.path.relative.substr(0, separator)] = true;
				separator = entry.path.relative.find('/', separator + 1);
			}
		}
	}

	for (const auto& sidecar : existingMetadata)
	{
		if (sidecar.second.folder)
		{
			candidates[sidecar.first] = true;
		}
		else if (listedFiles.find(sidecar.first) == listedFiles.end())
		{
			return Result<void>::Failure("Orphan asset metadata has no source file at " + sourceScheme + ":" +
			                             sidecar.first + ".meta");
		}
	}

	Map<String, AssetRecord> candidateByPath;
	Map<String, String> candidatePathsByGuid;
	for (const auto& candidate : candidates)
	{
		auto path = VirtualPath::Parse(sourceScheme + ":" + candidate.first);
		if (!path)
		{
			return Result<void>::Failure(path.error);
		}
		AssetMetadata metadata;
		const auto existing = existingMetadata.find(candidate.first);
		if (existing == existingMetadata.end())
		{
			if (!filesystem.CheckWritable(VirtualPath::Parse(path.value.ToString() + ".meta").value))
			{
				return Result<void>::Failure("Read-only source requires published metadata: " + path.value.ToString());
			}
			metadata.folder = candidate.second;
			if (!metadata.folder)
			{
				auto embedded = EmbeddedGuid(filesystem, path.value);
				if (!embedded)
				{
					return Result<void>::Failure(embedded.error);
				}
				metadata.guid = embedded.value ? *embedded.value : ResourceId::Generate();
			}
			else
			{
				metadata.guid = ResourceId::Generate();
			}
			auto written = WriteMetadata(path.value, metadata);
			if (!written)
			{
				return Result<void>::Failure("Cannot create asset metadata for " + path.value.ToString() + ": " +
				                             written.error);
			}
		}
		else
		{
			metadata = existing->second;
			if (metadata.folder != candidate.second)
			{
				return Result<void>::Failure("Asset metadata kind does not match its source path at " +
				                             path.value.ToString() + ".meta");
			}
		}

		const String guid = metadata.guid.ToString();
		if (candidatePathsByGuid.find(guid) != candidatePathsByGuid.end())
		{
			metadata.guid = ResourceId::Generate();
			auto written = WriteMetadata(path.value, metadata);
			if (!written)
			{
				return Result<void>::Failure("Cannot repair duplicate GUID at " + path.value.ToString() + ": " +
				                             written.error);
			}
		}
		const String repairedGuid = metadata.guid.ToString();
		candidatePathsByGuid.emplace(repairedGuid, path.value.ToString());
		candidateByPath.emplace(candidate.first, AssetRecord{std::move(path.value), std::move(metadata)});
	}

	assetsByPath = std::move(candidateByPath);
	pathsByGuid = std::move(candidatePathsByGuid);
	++revision;
	return {};
}

Result<void> AssetDatabase::CreateFolder(const VirtualPath& path)
{
	if (path.scheme != sourceScheme || path.relative.empty())
	{
		return Result<void>::Failure("Project folder path is invalid: " + path.ToString());
	}
	if (assetsByPath.find(path.relative) != assetsByPath.end())
	{
		return Result<void>::Failure("Project asset already exists: " + path.ToString());
	}
	const String parentPath = ParentRelativePath(path.relative);
	const auto parent = assetsByPath.find(parentPath);
	if (!parentPath.empty() && (parent == assetsByPath.end() || !parent->second.metadata.folder))
	{
		return Result<void>::Failure("Project folder parent does not exist: " + sourceScheme + ":" + parentPath);
	}
	AssetMetadata metadata;
	metadata.guid = ResourceId::Generate();
	metadata.folder = true;
	auto written = WriteMetadata(path, metadata);
	if (!written)
	{
		return written;
	}
	auto created = filesystem.CreateDirectory(path);
	if (!created)
	{
		auto sidecar = SidecarPath(path);
		if (sidecar)
		{
			filesystem.Remove(sidecar.value);
		}
		return created;
	}
	return Refresh();
}

Result<AssetMetadata> AssetDatabase::Metadata(const VirtualPath& path) const
{
	const auto found = assetsByPath.find(path.relative);
	if (path.scheme != sourceScheme || found == assetsByPath.end())
	{
		return Result<AssetMetadata>::Failure("Asset is not indexed: " + path.ToString());
	}
	return Result<AssetMetadata>::Success(found->second.metadata);
}

Result<void> AssetDatabase::SetImporterSettings(const VirtualPath& path, const String& importer,
                                                Ursa::UInt32 importerVersion, const nlohmann::json& settings)
{
	if (importer.empty() || !settings.is_object())
	{
		return Result<void>::Failure("Importer identity and settings object are required");
	}
	auto existing = Metadata(path);
	if (!existing)
	{
		return Result<void>::Failure(existing.error);
	}
	if (existing.value.folder)
	{
		return Result<void>::Failure("Folder assets do not have importer settings: " + path.ToString());
	}
	auto sidecar = SidecarPath(path);
	if (!sidecar)
	{
		return Result<void>::Failure(sidecar.error);
	}
	auto original = filesystem.Read(sidecar.value);
	if (!original)
	{
		return Result<void>::Failure(original.error);
	}
	existing.value.importer = importer;
	existing.value.importerVersion = importerVersion;
	existing.value.importSettings = settings;
	auto written = WriteMetadata(path, existing.value);
	if (!written)
	{
		return written;
	}
	auto refreshed = Refresh();
	if (!refreshed)
	{
		filesystem.WriteAtomic(sidecar.value, original.value);
		Refresh();
		return Result<void>::Failure(refreshed.error);
	}
	return {};
}

Result<VirtualPath> AssetDatabase::Resolve(ResourceId guid, Ursa::UInt64 localFileId) const
{
	const String key = guid.ToString();
	const auto source = pathsByGuid.find(key);
	if (source == pathsByGuid.end())
	{
		return Result<VirtualPath>::Failure("Missing asset GUID " + key);
	}
	if (localFileId == 0)
	{
		const auto* imported = FindImport(key);
		if (imported)
		{
			const auto rootArtifact =
			    std::find_if(imported->artifacts.begin(), imported->artifacts.end(),
			                 [](const ImportedArtifactLocation& item) { return item.localFileId == 0; });
			if (rootArtifact != imported->artifacts.end())
			{
				return Result<VirtualPath>::Success(rootArtifact->path);
			}
		}
		return VirtualPath::Parse(source->second);
	}
	const auto* imported = FindImport(key);
	if (!imported)
	{
		return Result<VirtualPath>::Failure("Asset GUID " + key + " has no imported sub-assets");
	}
	const auto artifact =
	    std::find_if(imported->artifacts.begin(), imported->artifacts.end(),
	                 [&](const ImportedArtifactLocation& item) { return item.localFileId == localFileId; });
	if (artifact == imported->artifacts.end())
	{
		return Result<VirtualPath>::Failure("Asset GUID " + key + " has no local file ID " +
		                                    ToUrsaString(std::to_string(localFileId)));
	}
	return Result<VirtualPath>::Success(artifact->path);
}

Result<void> AssetDatabase::ReloadImports()
{
	cacheLoaded = false;
	return Refresh();
}

bool AssetDatabase::Contains(ResourceId guid) const
{
	return pathsByGuid.find(guid.ToString()) != pathsByGuid.end();
}

const AssetDatabase::CachedImport* AssetDatabase::FindImport(const String& guid) const
{
	const auto selected = selectedImports.find(guid);
	if (selected == selectedImports.end())
	{
		return nullptr;
	}
	const auto found = importsByKey.find(selected->second);
	return found == importsByKey.end() ? nullptr : &found->second;
}

Result<void> AssetDatabase::ApplyGuidMigration(const Vector<AssetGuidAssignment>& assignments)
{
	Map<String, ResourceId> assignedByPath;
	for (const AssetGuidAssignment& assignment : assignments)
	{
		if (assignment.path.scheme != sourceScheme || !assignment.guid ||
		    assetsByPath.find(assignment.path.relative) == assetsByPath.end())
		{
			return Result<void>::Failure("GUID migration contains an invalid asset path or GUID: " +
			                             assignment.path.ToString());
		}
		if (!assignedByPath.emplace(assignment.path.relative, assignment.guid).second)
		{
			return Result<void>::Failure("GUID migration assigns an asset more than once: " +
			                             assignment.path.ToString());
		}
	}

	Map<String, String> resultingPaths;
	for (const auto& asset : assetsByPath)
	{
		const auto assignment = assignedByPath.find(asset.first);
		const ResourceId guid = assignment == assignedByPath.end() ? asset.second.metadata.guid : assignment->second;
		const String key = guid.ToString();
		const auto existing = resultingPaths.find(key);
		if (existing != resultingPaths.end())
		{
			return Result<void>::Failure("GUID migration would duplicate " + key + " between " + existing->second +
			                             " and " + asset.second.path.ToString());
		}
		resultingPaths.emplace(key, asset.second.path.ToString());
	}

	struct MetadataBackup
	{
		VirtualPath assetPath;
		Vector<Ursa::UInt8> bytes;
	};

	Vector<MetadataBackup> backups;
	backups.reserve(assignedByPath.size());
	for (const auto& assignment : assignedByPath)
	{
		const auto record = assetsByPath.find(assignment.first);
		auto sidecar = SidecarPath(record->second.path);
		if (!sidecar)
		{
			return Result<void>::Failure(sidecar.error);
		}
		auto writable = filesystem.CheckWritable(sidecar.value);
		auto bytes = filesystem.Read(sidecar.value);
		if (!writable || !bytes)
		{
			return Result<void>::Failure(!writable ? writable.error : bytes.error);
		}
		backups.push_back({record->second.path, std::move(bytes.value)});
	}

	for (const auto& assignment : assignedByPath)
	{
		const auto record = assetsByPath.find(assignment.first);
		AssetMetadata metadata = record->second.metadata;
		metadata.guid = assignment.second;
		auto written = WriteMetadata(record->second.path, metadata);
		if (!written)
		{
			for (const MetadataBackup& backup : backups)
			{
				auto sidecar = SidecarPath(backup.assetPath);
				if (sidecar)
				{
					filesystem.WriteAtomic(sidecar.value, backup.bytes);
				}
			}
			return written;
		}
	}

	auto refreshed = Refresh();
	if (!refreshed)
	{
		for (const MetadataBackup& backup : backups)
		{
			auto sidecar = SidecarPath(backup.assetPath);
			if (sidecar)
			{
				filesystem.WriteAtomic(sidecar.value, backup.bytes);
			}
		}
		Refresh();
		return Result<void>::Failure("GUID migration was rolled back: " + refreshed.error);
	}
	return {};
}

Result<String> AssetDatabase::Fingerprint(const VirtualPath& source, const String& importer,
                                          Ursa::UInt32 importerVersion, const String& target,
                                          const nlohmann::json& settings, const Vector<VirtualPath>& dependencies) const
{
	if (source.scheme != sourceScheme || importer.empty() || target.empty() || !settings.is_object())
	{
		return Result<String>::Failure("Import request has an invalid source, importer, or settings object");
	}
	auto sourceBytes = filesystem.Read(source);
	if (!sourceBytes)
	{
		return Result<String>::Failure(sourceBytes.error);
	}
	FingerprintBuilder fingerprint;
	fingerprint.Add("Ursa.Import.2");
	fingerprint.Add(source.ToString());
	fingerprint.Add(importer);
	fingerprint.Add(ToUrsaString(std::to_string(importerVersion)));
	fingerprint.Add(target);
	try
	{
		fingerprint.Add(ToUrsaString(settings.dump()));
	}
	catch (const std::exception& error)
	{
		return Result<String>::Failure("Cannot fingerprint importer settings: " + ToUrsaString(error.what()));
	}
	fingerprint.Add(sourceBytes.value);
	auto sidecarBytes = filesystem.Read(VirtualPath::Parse(source.ToString() + ".meta").value);
	if (!sidecarBytes)
	{
		return Result<String>::Failure(sidecarBytes.error);
	}
	fingerprint.Add(sidecarBytes.value);

	Vector<VirtualPath> sortedDependencies = dependencies;
	std::sort(sortedDependencies.begin(), sortedDependencies.end(),
	          [](const VirtualPath& left, const VirtualPath& right) { return left.ToString() < right.ToString(); });
	for (Ursa::Size index = 0; index < sortedDependencies.size(); ++index)
	{
		if (sortedDependencies[index] == source)
		{
			continue;
		}
		if (index != 0 && sortedDependencies[index] == sortedDependencies[index - 1])
		{
			continue;
		}
		fingerprint.Add(sortedDependencies[index].ToString());
		auto bytes = filesystem.Read(sortedDependencies[index]);
		if (!bytes)
		{
			return Result<String>::Failure("Cannot fingerprint import dependency: " + bytes.error);
		}
		fingerprint.Add(bytes.value);
	}
	return Result<String>::Success(fingerprint.ToString());
}

Result<ImportCacheStatus> AssetDatabase::CheckImport(const VirtualPath& source, const String& importer,
                                                     Ursa::UInt32 importerVersion, const String& target,
                                                     const nlohmann::json& settings,
                                                     const Vector<VirtualPath>& dependencies)
{
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return Result<ImportCacheStatus>::Failure("Editor import cache scheme is not mounted: " +
		                                          String(editorLibraryScheme));
	}
	const auto asset = assetsByPath.find(source.relative);
	if (source.scheme != sourceScheme || asset == assetsByPath.end() || asset->second.metadata.folder)
	{
		return Result<ImportCacheStatus>::Failure("Import source is not an indexed file asset: " + source.ToString());
	}
	auto fingerprint = Fingerprint(source, importer, importerVersion, target, settings, dependencies);
	if (!fingerprint)
	{
		return Result<ImportCacheStatus>::Failure(fingerprint.error);
	}
	ImportCacheStatus status;
	status.fingerprint = fingerprint.value;
	status.settings = settings;
	const String guid = asset->second.metadata.guid.ToString();
	const String key = guid + ":" + target;
	const auto cached = importsByKey.find(key);
	if (cached == importsByKey.end())
	{
		return Result<ImportCacheStatus>::Success(std::move(status));
	}
	status.dependencies = cached->second.dependencies;
	if (cached->second.fingerprint != fingerprint.value)
	{
		return Result<ImportCacheStatus>::Success(std::move(status));
	}
	for (const ImportedArtifactLocation& artifact : cached->second.artifacts)
	{
		auto bytes = filesystem.Read(artifact.path);
		FingerprintBuilder content;
		if (bytes)
		{
			content.Add(bytes.value);
		}
		if (!bytes || bytes.value.size() != artifact.size || artifact.contentFingerprint.empty() ||
		    content.ToString() != artifact.contentFingerprint)
		{
			return Result<ImportCacheStatus>::Success(std::move(status));
		}
	}
	status.upToDate = true;
	status.artifacts = cached->second.artifacts;
	selectedImports[guid] = key;
	return Result<ImportCacheStatus>::Success(std::move(status));
}

Result<Vector<VirtualPath>> AssetDatabase::ImportDependencies(const VirtualPath& source, const String& importer) const
{
	const auto asset = assetsByPath.find(source.relative);
	if (source.scheme != sourceScheme || asset == assetsByPath.end())
	{
		return Result<Vector<VirtualPath>>::Failure("Import source is not indexed: " + source.ToString());
	}
	const auto* cached = FindImport(asset->second.metadata.guid.ToString());
	if (!cached || cached->importer != importer)
	{
		return Result<Vector<VirtualPath>>::Success({});
	}
	return Result<Vector<VirtualPath>>::Success(cached->dependencies);
}

Result<ImportCacheStatus> AssetDatabase::CurrentImport(const VirtualPath& source)
{
	auto metadata = Metadata(source);
	if (!metadata)
	{
		return Result<ImportCacheStatus>::Failure(metadata.error);
	}
	const auto* cached = FindImport(metadata.value.guid.ToString());
	if (!cached)
	{
		return Result<ImportCacheStatus>::Success({});
	}
	return CheckImport(source, cached->importer, cached->importerVersion, cached->target, cached->settings,
	                   cached->dependencies);
}

Result<Vector<ImportedArtifactRecord>> AssetDatabase::CurrentImportedArtifacts()
{
	Vector<ImportedArtifactRecord> result;
	for (const auto& assetEntry : assetsByPath)
	{
		const AssetRecord& asset = assetEntry.second;
		if (asset.metadata.folder)
		{
			continue;
		}
		const String guidKey = asset.metadata.guid.ToString();
		const auto* cached = FindImport(guidKey);
		if (!cached && asset.metadata.importer.empty())
		{
			continue;
		}
		if (!cached ||
		    (!asset.metadata.importer.empty() && (cached->importer != asset.metadata.importer ||
		                                          cached->importerVersion != asset.metadata.importerVersion)))
		{
			return Result<Vector<ImportedArtifactRecord>>::Failure("Asset import cache is missing or out of date for " +
			                                                       asset.path.ToString());
		}
		auto current = CheckImport(asset.path, cached->importer, cached->importerVersion, cached->target,
		                           cached->settings, cached->dependencies);
		if (!current)
		{
			return Result<Vector<ImportedArtifactRecord>>::Failure(current.error);
		}
		if (!current.value.upToDate)
		{
			return Result<Vector<ImportedArtifactRecord>>::Failure(
			    "Asset import is stale and must be reimported before packaging: " + asset.path.ToString());
		}
		for (const ImportedArtifactLocation& artifact : current.value.artifacts)
		{
			result.push_back({asset.metadata.guid, artifact.localFileId, artifact.kind, artifact.path});
		}
	}
	std::sort(result.begin(), result.end(),
	          [](const ImportedArtifactRecord& left, const ImportedArtifactRecord& right)
	          {
		          if (left.guid != right.guid)
		          {
			          return left.guid.ToString() < right.guid.ToString();
		          }
		          return left.localFileId < right.localFileId;
	          });
	return Result<Vector<ImportedArtifactRecord>>::Success(std::move(result));
}

Result<void> AssetDatabase::PublishImport(const VirtualPath& source, const String& importer,
                                          Ursa::UInt32 importerVersion, const String& target,
                                          const nlohmann::json& settings, const Vector<VirtualPath>& dependencies,
                                          const Vector<ImportedArtifact>& artifacts)
{
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return Result<void>::Failure("Editor import cache scheme is not mounted: " + String(editorLibraryScheme));
	}
	const auto asset = assetsByPath.find(source.relative);
	if (source.scheme != sourceScheme || asset == assetsByPath.end() || asset->second.metadata.folder)
	{
		return Result<void>::Failure("Import source is not an indexed file asset: " + source.ToString());
	}
	if (artifacts.empty())
	{
		return Result<void>::Failure("Importer produced no artifacts for " + source.ToString());
	}
	auto status = CheckImport(source, importer, importerVersion, target, settings, dependencies);
	if (!status)
	{
		return Result<void>::Failure(status.error);
	}
	if (status.value.upToDate)
	{
		return {};
	}
	Map<String, bool> localFileIds;
	for (const ImportedArtifact& artifact : artifacts)
	{
		if (artifact.kind.empty())
		{
			return Result<void>::Failure("Imported artifacts need a kind at " + source.ToString());
		}
		const String key = ToUrsaString(std::to_string(artifact.localFileId));
		if (!localFileIds.emplace(key, true).second)
		{
			return Result<void>::Failure("Importer produced a duplicate local file ID " + key + " at " +
			                             source.ToString());
		}
	}

	CachedImport candidate;
	candidate.importer = importer;
	candidate.importerVersion = importerVersion;
	candidate.target = target;
	candidate.settings = settings;
	candidate.fingerprint = status.value.fingerprint;
	candidate.dependencies = dependencies;
	Vector<VirtualPath> newlyWritten;
	for (const ImportedArtifact& artifact : artifacts)
	{
		const char* extension = ".bin";
		if (artifact.kind == "Mesh")
		{
			extension = ".ursamesh";
		}
		else if (artifact.kind == "Texture")
		{
			extension = ".ursatex";
		}
		else if (artifact.localFileId == 0 && artifact.kind == "Shader")
		{
			extension = ".ursashaderasset";
		}
		else if (artifact.localFileId == 0 && artifact.kind == "Model")
		{
			extension = ".ursamodel";
		}
		else if (artifact.kind == "Material")
		{
			extension = ".ursamat";
		}
		else if (artifact.kind == "Scene")
		{
			extension = ".ursascene";
		}
		else if (artifact.kind == "Prefab")
		{
			extension = ".ursaprefab";
		}
		const String relative = String("Artifacts/") + candidate.fingerprint + "/" +
		                        ToUrsaString(std::to_string(artifact.localFileId)) + extension;
		auto artifactPath = VirtualPath::Parse(String(editorLibraryScheme) + ":" + relative);
		if (!artifactPath)
		{
			return Result<void>::Failure(artifactPath.error);
		}
		auto exists = filesystem.Exists(artifactPath.value);
		if (!exists)
		{
			return Result<void>::Failure(exists.error);
		}
		if (exists.value)
		{
			auto oldBytes = filesystem.Read(artifactPath.value);
			if (!oldBytes || oldBytes.value != artifact.bytes)
			{
				auto repaired = filesystem.WriteAtomic(artifactPath.value, artifact.bytes);
				if (!repaired)
				{
					return repaired;
				}
			}
		}
		else
		{
			auto written = filesystem.WriteAtomic(artifactPath.value, artifact.bytes);
			if (!written)
			{
				for (const VirtualPath& path : newlyWritten)
				{
					filesystem.Remove(path);
				}
				return Result<void>::Failure("Cannot stage imported artifact " + artifactPath.value.ToString() + ": " +
				                             written.error);
			}
			newlyWritten.push_back(artifactPath.value);
		}
		FingerprintBuilder content;
		content.Add(artifact.bytes);
		candidate.artifacts.push_back({artifact.localFileId, artifact.kind, std::move(artifactPath.value),
		                               content.ToString(), static_cast<Ursa::UInt64>(artifact.bytes.size())});
	}

	const String guid = asset->second.metadata.guid.ToString();
	const String key = guid + ":" + target;
	Map<String, CachedImport> nextImports = importsByKey;
	nextImports[key] = std::move(candidate);
	const auto oldSelections = selectedImports;
	selectedImports[guid] = key;
	auto saved = SaveCache(nextImports);
	if (!saved)
	{
		selectedImports = oldSelections;
		for (const VirtualPath& path : newlyWritten)
		{
			filesystem.Remove(path);
		}
		return saved;
	}
	importsByKey = std::move(nextImports);
	return {};
}

Vector<AssetRecord> AssetDatabase::Assets() const
{
	Vector<AssetRecord> result;
	result.reserve(assetsByPath.size());
	for (const auto& entry : assetsByPath)
	{
		result.push_back(entry.second);
	}
	return result;
}

Vector<ScriptSourceAsset> AssetDatabase::ScriptSources() const
{
	Vector<ScriptSourceAsset> result;
	for (const auto& entry : assetsByPath)
	{
		if (entry.second.metadata.folder || !EndsWith(entry.first, ".cs"))
		{
			continue;
		}
		bool editorScript = false;
		Ursa::Size begin = 0;
		while (begin < entry.first.size())
		{
			const auto separator = entry.first.find('/', begin);
			const Ursa::Size length = separator == String::npos ? entry.first.size() - begin : separator - begin;
			if (length == 6 && entry.first.compare(begin, length, "Editor") == 0)
			{
				editorScript = true;
				break;
			}
			if (separator == String::npos)
			{
				break;
			}
			begin = separator + 1;
		}
		result.push_back({entry.second.path, entry.second.metadata.guid,
		                  editorScript ? ScriptAssemblyGroup::Editor : ScriptAssemblyGroup::Game});
	}
	return result;
}

Result<Vector<VirtualPath>> AssetDatabase::FilesFor(const VirtualPath& path, bool isFolder) const
{
	if (path.scheme != sourceScheme)
	{
		return Result<Vector<VirtualPath>>::Failure("Asset operation requires scheme " + sourceScheme + ": " +
		                                            path.ToString());
	}
	const auto record = assetsByPath.find(path.relative);
	if (record == assetsByPath.end() || record->second.metadata.folder != isFolder)
	{
		return Result<Vector<VirtualPath>>::Failure("Asset does not exist or has the wrong kind: " + path.ToString());
	}
	auto listed = filesystem.List(sourceScheme);
	if (!listed)
	{
		return Result<Vector<VirtualPath>>::Failure(listed.error);
	}
	Vector<VirtualPath> result;
	const String sidecar = path.relative + ".meta";
	for (const VirtualFileEntry& entry : listed.value)
	{
		if ((isFolder && (entry.path.relative == sidecar || IsPathWithin(entry.path.relative, path.relative))) ||
		    (!isFolder && (entry.path.relative == path.relative || entry.path.relative == sidecar)))
		{
			result.push_back(entry.path);
		}
	}
	if (result.empty())
	{
		return Result<Vector<VirtualPath>>::Failure("Asset has no source files to operate on: " + path.ToString());
	}
	return Result<Vector<VirtualPath>>::Success(std::move(result));
}

Result<Vector<AssetDatabase::FileTransfer>>
AssetDatabase::StageTransfer(const VirtualPath& source, const VirtualPath& destination, bool sourceIsFolder) const
{
	if (source.scheme != destination.scheme || source.scheme != sourceScheme || source == destination)
	{
		return Result<Vector<FileTransfer>>::Failure("Asset move or copy requires distinct paths in " + sourceScheme);
	}
	if (sourceIsFolder && IsPathWithin(destination.relative, source.relative))
	{
		return Result<Vector<FileTransfer>>::Failure("Cannot move or copy a folder into itself: " +
		                                             destination.ToString());
	}
	auto sourceFiles = FilesFor(source, sourceIsFolder);
	if (!sourceFiles)
	{
		return Result<Vector<FileTransfer>>::Failure(sourceFiles.error);
	}
	auto listed = filesystem.List(sourceScheme);
	if (!listed)
	{
		return Result<Vector<FileTransfer>>::Failure(listed.error);
	}
	Map<String, bool> existing;
	for (const VirtualFileEntry& entry : listed.value)
	{
		existing.emplace(entry.path.relative, true);
	}

	Vector<FileTransfer> result;
	result.reserve(sourceFiles.value.size());
	for (const VirtualPath& sourceFile : sourceFiles.value)
	{
		String destinationRelative;
		if (sourceFile.relative == source.relative + ".meta")
		{
			destinationRelative = destination.relative + ".meta";
		}
		else if (sourceFile.relative == source.relative)
		{
			destinationRelative = destination.relative;
		}
		else if (sourceIsFolder && IsPathWithin(sourceFile.relative, source.relative))
		{
			destinationRelative = destination.relative + sourceFile.relative.substr(source.relative.size());
		}
		else
		{
			return Result<Vector<FileTransfer>>::Failure("Cannot map source asset path " + sourceFile.ToString());
		}
		auto destinationFile = VirtualPath::Parse(destination.scheme + ":" + destinationRelative);
		if (!destinationFile)
		{
			return Result<Vector<FileTransfer>>::Failure(destinationFile.error);
		}
		if (existing.find(destinationRelative) != existing.end())
		{
			return Result<Vector<FileTransfer>>::Failure("Destination already contains a file: " +
			                                             destinationFile.value.ToString());
		}
		auto writableSource = filesystem.CheckWritable(sourceFile);
		auto writableDestination = filesystem.CheckWritable(destinationFile.value);
		if (!writableSource || !writableDestination)
		{
			return Result<Vector<FileTransfer>>::Failure(!writableSource ? writableSource.error
			                                                             : writableDestination.error);
		}
		auto bytes = filesystem.Read(sourceFile);
		if (!bytes)
		{
			return Result<Vector<FileTransfer>>::Failure(bytes.error);
		}
		result.push_back({sourceFile, std::move(destinationFile.value), std::move(bytes.value)});
	}
	if (sourceIsFolder)
	{
		for (const auto& entry : existing)
		{
			if (IsPathWithin(entry.first, destination.relative))
			{
				return Result<Vector<FileTransfer>>::Failure("Destination folder already exists: " +
				                                             destination.ToString());
			}
		}
	}
	return Result<Vector<FileTransfer>>::Success(std::move(result));
}

Result<void> AssetDatabase::CommitTransfer(Vector<FileTransfer> files, bool moveSource, bool regenerateGuids)
{
	Vector<VirtualPath> written;
	for (FileTransfer& file : files)
	{
		Vector<Ursa::UInt8> bytes = file.bytes;
		if (regenerateGuids && EndsWith(file.source.relative, ".meta"))
		{
			auto metadata =
			    DecodeMetadata(ToUrsaString(std::string(file.bytes.begin(), file.bytes.end())), file.source);
			if (!metadata)
			{
				for (const VirtualPath& path : written)
				{
					filesystem.Remove(path);
				}
				return Result<void>::Failure(metadata.error);
			}
			metadata.value.guid = ResourceId::Generate();
			auto encoded = EncodeMetadata(metadata.value);
			if (!encoded)
			{
				for (const VirtualPath& path : written)
				{
					filesystem.Remove(path);
				}
				return Result<void>::Failure(encoded.error);
			}
			bytes = BytesOf(ToStdString(encoded.value));
		}
		auto saved = filesystem.WriteAtomic(file.destination, bytes);
		if (!saved)
		{
			for (const VirtualPath& path : written)
			{
				filesystem.Remove(path);
			}
			return Result<void>::Failure(saved.error);
		}
		written.push_back(file.destination);
	}

	if (moveSource)
	{
		Vector<Ursa::Size> removed;
		for (Ursa::Size index = 0; index < files.size(); ++index)
		{
			auto removal = filesystem.Remove(files[index].source);
			if (!removal)
			{
				for (Ursa::Size restoreIndex : removed)
				{
					filesystem.WriteAtomic(files[restoreIndex].source, files[restoreIndex].bytes);
				}
				for (const VirtualPath& path : written)
				{
					filesystem.Remove(path);
				}
				return Result<void>::Failure(removal.error);
			}
			removed.push_back(index);
		}
	}
	return Refresh();
}

Result<void> AssetDatabase::Move(const VirtualPath& source, const VirtualPath& destination)
{
	const auto record = assetsByPath.find(source.relative);
	if (record == assetsByPath.end())
	{
		return Result<void>::Failure("Asset is not indexed: " + source.ToString());
	}
	const bool isFolder = record->second.metadata.folder;
	auto files = StageTransfer(source, destination, isFolder);
	if (!files)
	{
		return Result<void>::Failure(files.error);
	}
	auto moved = CommitTransfer(std::move(files.value), true, false);
	if (!moved || !isFolder)
	{
		return moved;
	}
	auto createdDirectory = filesystem.CreateDirectory(destination);
	if (!createdDirectory)
	{
		return Result<void>::Failure("Folder assets moved but the destination directory could not be created: " +
		                             createdDirectory.error);
	}
	auto removedDirectory = filesystem.RemoveDirectory(source, true);
	if (!removedDirectory)
	{
		return Result<void>::Failure("Folder assets moved but the empty source directory could not be removed: " +
		                             removedDirectory.error);
	}
	return {};
}

Result<void> AssetDatabase::Copy(const VirtualPath& source, const VirtualPath& destination)
{
	const auto record = assetsByPath.find(source.relative);
	if (record == assetsByPath.end())
	{
		return Result<void>::Failure("Asset is not indexed: " + source.ToString());
	}
	auto files = StageTransfer(source, destination, record->second.metadata.folder);
	return files ? CommitTransfer(std::move(files.value), false, true) : Result<void>::Failure(files.error);
}

Result<void> AssetDatabase::Remove(const VirtualPath& path)
{
	const auto record = assetsByPath.find(path.relative);
	if (record == assetsByPath.end())
	{
		return Result<void>::Failure("Asset is not indexed: " + path.ToString());
	}
	auto files = FilesFor(path, record->second.metadata.folder);
	if (!files)
	{
		return Result<void>::Failure(files.error);
	}
	Vector<FileTransfer> backups;
	backups.reserve(files.value.size());
	for (const VirtualPath& file : files.value)
	{
		auto writable = filesystem.CheckWritable(file);
		if (!writable)
		{
			return writable;
		}
		auto bytes = filesystem.Read(file);
		if (!bytes)
		{
			return Result<void>::Failure(bytes.error);
		}
		backups.push_back({file, file, std::move(bytes.value)});
	}
	Vector<Ursa::Size> removed;
	for (Ursa::Size index = 0; index < backups.size(); ++index)
	{
		auto result = filesystem.Remove(backups[index].source);
		if (!result)
		{
			for (Ursa::Size restoreIndex : removed)
			{
				filesystem.WriteAtomic(backups[restoreIndex].source, backups[restoreIndex].bytes);
			}
			return result;
		}
		removed.push_back(index);
	}
	return Refresh();
}
} // namespace Ursa::Editor
