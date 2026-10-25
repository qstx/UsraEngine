#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetImportRules.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/ResourceFile.hpp>
#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace Ursa::Editor
{
String ImportedArtifactRecord::Label() const
{
	return displayName + " (" + kind + ")" +
	       (ambiguousName ? " [" + (outputKey.empty() ? ToUrsaString(std::to_string(localFileId)) : outputKey) + "]"
	                      : String{});
}

Vector<ImportedArtifactRecord> AssetDatabase::ImportedArtifacts() const
{
	if (presentationRevision == revision)
	{
		return presentationDirectory;
	}
	Vector<ImportedArtifactRecord> records;
	for (const auto& entry : assetsByPath)
	{
		if (const auto* cached = FindImport(entry.second.metadata.guid.ToString()))
		{
			for (const auto& artifact : cached->artifacts)
			{
				records.push_back({entry.second.metadata.guid, artifact.localFileId, artifact.kind, artifact.path,
				                   artifact.localFileId == cached->mainObjectLocalFileId, artifact.displayName,
				                   artifact.visibleInProject, entry.second.path, artifact.outputKey});
			}
		}
	}
	Map<String, Size> names;
	for (const auto& record : records)
	{
		if (record.visibleInProject && !record.isMainObject)
		{
			++names[record.guid.ToString() + ":" + record.kind + ":" + record.displayName];
		}
	}
	for (auto& record : records)
	{
		record.ambiguousName = names[record.guid.ToString() + ":" + record.kind + ":" + record.displayName] > 1;
	}
	presentationDirectory = records;
	presentationRevision = revision;
	return records;
}

Vector<ImportedArtifactRecord> AssetDatabase::VisibleImportedObjects() const
{
	auto records = ImportedArtifacts();
	records.erase(std::remove_if(records.begin(), records.end(),
	                             [](const auto& record) { return !record.isMainObject && !record.visibleInProject; }),
	              records.end());
	std::sort(records.begin(), records.end(),
	          [](const auto& left, const auto& right)
	          {
		          if (left.sourcePath != right.sourcePath)
		          {
			          return left.sourcePath.ToString() < right.sourcePath.ToString();
		          }
		          if (left.isMainObject != right.isMainObject)
		          {
			          return left.isMainObject;
		          }
		          if (left.displayName != right.displayName)
		          {
			          return left.displayName < right.displayName;
		          }
		          return left.localFileId < right.localFileId;
	          });
	return records;
}

Result<ImportedArtifactRecord> AssetDatabase::ImportedObject(ResourceId guid, UInt64 localFileId) const
{
	const auto* cached = FindImport(guid.ToString());
	if (cached)
	{
		const auto selected = localFileId ? localFileId : cached->mainObjectLocalFileId;
		for (const auto& artifact : cached->artifacts)
		{
			if (artifact.localFileId == selected)
			{
				auto source = SourcePath(guid);
				if (source)
				{
					const auto duplicates = std::count_if(
					    cached->artifacts.begin(), cached->artifacts.end(),
					    [&](const auto& other)
					    {
						    return other.visibleInProject && other.localFileId != cached->mainObjectLocalFileId &&
						           other.kind == artifact.kind && other.displayName == artifact.displayName;
					    });
					return Result<ImportedArtifactRecord>::Success({guid, selected, artifact.kind, artifact.path,
					                                                selected == cached->mainObjectLocalFileId,
					                                                artifact.displayName, artifact.visibleInProject,
					                                                source.value, artifact.outputKey, duplicates > 1});
				}
			}
		}
	}
	return Result<ImportedArtifactRecord>::Failure("Imported object is missing: " + guid.ToString() + ":" +
	                                               ToUrsaString(std::to_string(localFileId)));
}

namespace
{
using Json = nlohmann::json;
constexpr Ursa::UInt32 importCacheVersion = 4;

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
		if (metadata.importer.compare(0, 7, "script:") == 0 &&
		    (!ImportScriptType::FromIdentity(metadata.importer) || !metadata.importerVersion))
		{
			return Result<AssetMetadata>::Failure("Invalid script importer identity/version at " + sidecar.ToString());
		}
		if (document.contains("scriptTypes"))
		{
			const auto& identities = document["scriptTypes"];
			if (!identities.is_object() || identities.value("Version", 0u) != 1 || !identities.contains("Types") ||
			    !identities["Types"].is_array())
			{
				return Result<AssetMetadata>::Failure("Invalid script type metadata at " + sidecar.ToString());
			}
			metadata.scriptTypes = identities;
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
		if (!metadata.scriptTypes.empty())
		{
			document["scriptTypes"] = metadata.scriptTypes;
		}
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
	if (!capturingCandidates)
	{
		candidatePublications.clear();
	}
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
			    !entry.contains("mainObjectLocalFileId") || !entry["mainObjectLocalFileId"].is_number_unsigned() ||
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
			cached.mainObjectLocalFileId = entry["mainObjectLocalFileId"].get<UInt64>();
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
				if (!artifact.contains("outputKey") || !artifact["outputKey"].is_string() ||
				    !artifact.contains("displayName") || !artifact["displayName"].is_string() ||
				    artifact["displayName"].get<std::string>().empty() || !artifact.contains("visibleInProject") ||
				    !artifact["visibleInProject"].is_boolean())
				{
					valid = false;
					break;
				}
				cached.artifacts.push_back(
				    {artifact["localFileId"].get<Ursa::UInt64>(), ToUrsaString(artifact["kind"].get<std::string>()),
				     std::move(path.value), ToUrsaString(artifact.value("contentFingerprint", std::string{})),
				     artifact.value("size", Ursa::UInt64{0}), ToUrsaString(artifact["outputKey"].get<std::string>()),
				     ToUrsaString(artifact["displayName"].get<std::string>()),
				     artifact["visibleInProject"].get<bool>()});
			}
			if (valid && !cached.artifacts.empty())
			{
				if (std::none_of(cached.artifacts.begin(), cached.artifacts.end(), [&](const auto& artifact)
				                 { return artifact.localFileId == cached.mainObjectLocalFileId; }))
				{
					continue;
				}
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
			entry["mainObjectLocalFileId"] = item.second.mainObjectLocalFileId;
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
				entry["artifacts"].back()["outputKey"] = ToStdString(artifact.outputKey);
				entry["artifacts"].back()["displayName"] = ToStdString(artifact.displayName);
				entry["artifacts"].back()["visibleInProject"] = artifact.visibleInProject;
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
			metadata.scriptTypes = Json::object();
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

Result<void> AssetDatabase::UpdateScriptIdentities(const ScriptSourceAsset& source, const Json& identities)
{
	if (!EndsWith(source.path.relative, ".cs") || !identities.is_object() || identities.value("Version", 0u) != 1 ||
	    !identities.contains("Types") || !identities["Types"].is_array())
	{
		return Result<void>::Failure("Script identity update has an invalid source or metadata schema");
	}
	auto current = ReadMetadata(source.path);
	if (!current || current.value.guid != source.guid || current.value.scriptTypes != source.identities)
	{
		return Result<void>::Failure("Script metadata changed during type discovery: " + source.path.ToString());
	}
	if (current.value.scriptTypes == identities || (current.value.scriptTypes.empty() && identities["Types"].empty()))
	{
		return {};
	}
	current.value.scriptTypes = identities;
	auto saved = WriteMetadata(source.path, current.value);
	if (saved)
	{
		assetsByPath[source.path.relative].metadata = std::move(current.value);
		++revision;
	}
	return saved;
}

Result<void> AssetDatabase::SetImporterSettings(const VirtualPath& path, const String& importer,
                                                Ursa::UInt32 importerVersion, const nlohmann::json& settings)
{
	if (importer.empty() || !importerVersion || !settings.is_object() ||
	    (importer.compare(0, 7, "script:") == 0 && !ImportScriptType::FromIdentity(importer)))
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

Result<VirtualPath> AssetDatabase::SourcePath(ResourceId guid) const
{
	const auto source = pathsByGuid.find(guid.ToString());
	return source == pathsByGuid.end() ? Result<VirtualPath>::Failure("Missing asset GUID " + guid.ToString())
	                                   : VirtualPath::Parse(source->second);
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
			const auto rootArtifact = std::find_if(imported->artifacts.begin(), imported->artifacts.end(),
			                                       [&](const ImportedArtifactLocation& item)
			                                       { return item.localFileId == imported->mainObjectLocalFileId; });
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

Result<ImportedArtifactLocation> AssetDatabase::ResolveImportedArtifact(ResourceId guid, UInt64 localFileId) const
{
	const auto* cached = FindImport(guid.ToString());
	if (!cached)
	{
		return Result<ImportedArtifactLocation>::Failure("Asset has no completed import: " + guid.ToString());
	}
	const UInt64 selected = localFileId ? localFileId : cached->mainObjectLocalFileId;
	const auto artifact = std::find_if(cached->artifacts.begin(), cached->artifacts.end(),
	                                   [&](const auto& value) { return value.localFileId == selected; });
	if (artifact == cached->artifacts.end())
	{
		return Result<ImportedArtifactLocation>::Failure("Completed import has no requested output: " +
		                                                 guid.ToString());
	}
	const auto bytes = filesystem.Read(artifact->path);
	FingerprintBuilder fingerprint;
	if (bytes)
	{
		fingerprint.Add(bytes.value);
	}
	if (!bytes || bytes.value.size() != artifact->size || fingerprint.ToString() != artifact->contentFingerprint)
	{
		return Result<ImportedArtifactLocation>::Failure("Completed import output is missing or corrupt: " +
		                                                 artifact->path.ToString());
	}
	return Result<ImportedArtifactLocation>::Success(*artifact);
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
	status.importer = importer;
	status.importerVersion = importerVersion;
	status.target = target;
	const String guid = asset->second.metadata.guid.ToString();
	const String key = guid + ":" + target;
	const auto cached = importsByKey.find(key);
	if (cached == importsByKey.end())
	{
		return Result<ImportCacheStatus>::Success(std::move(status));
	}
	status.dependencies = cached->second.dependencies;
	status.mainObjectLocalFileId = cached->second.mainObjectLocalFileId;
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
	const auto selected = selectedImports.find(guid);
	if (selected == selectedImports.end() || selected->second != key)
	{
		++revision;
	}
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
			result.push_back({asset.metadata.guid, artifact.localFileId, artifact.kind, artifact.path,
			                  artifact.localFileId == current.value.mainObjectLocalFileId, artifact.displayName,
			                  artifact.visibleInProject, asset.path, artifact.outputKey});
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
                                          const Vector<ImportedArtifact>& inputArtifacts, UInt64 mainObjectLocalFileId)
{
	auto artifacts = inputArtifacts;
	for (auto& artifact : artifacts)
	{
		if (artifact.displayName.empty())
		{
			artifact.displayName = artifact.localFileId == mainObjectLocalFileId ? source.FileName()
			                       : !artifact.outputKey.empty()                 ? artifact.outputKey
			                                                                     : artifact.kind;
		}
		if (artifact.kind == "ModelNode")
		{
			artifact.visibleInProject = false;
		}
	}
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
	Map<String, bool> localFileIds;
	Set<String> outputKeys;
	for (const ImportedArtifact& artifact : artifacts)
	{
		if (artifact.kind.empty())
		{
			return Result<void>::Failure("Imported artifacts need a kind at " + source.ToString());
		}
		const String key = ToUrsaString(std::to_string(artifact.localFileId));
		if (!artifact.outputKey.empty())
		{
			auto expected = StableImportObjectId(importer, artifact.outputKey);
			if (!expected || expected.value != artifact.localFileId || !outputKeys.insert(artifact.outputKey).second)
			{
				return Result<void>::Failure("Invalid or duplicate imported object key " + artifact.outputKey + " at " +
				                             source.ToString());
			}
		}
		if (!localFileIds.emplace(key, true).second)
		{
			return Result<void>::Failure("Importer produced a duplicate local file ID " + key + " at " +
			                             source.ToString());
		}
	}
	if (localFileIds.find(ToUrsaString(std::to_string(mainObjectLocalFileId))) == localFileIds.end())
	{
		return Result<void>::Failure("Imported main object is not present at " + source.ToString());
	}
	const auto oldSelections = selectedImports;
	auto status = CheckImport(source, importer, importerVersion, target, settings, dependencies);
	selectedImports = oldSelections;
	if (!status)
	{
		return Result<void>::Failure(status.error);
	}
	const auto sameOutputs = [&]
	{
		if (!status.value.upToDate || status.value.mainObjectLocalFileId != mainObjectLocalFileId ||
		    status.value.artifacts.size() != artifacts.size())
		{
			return false;
		}
		for (const auto& output : artifacts)
		{
			const auto previous =
			    std::find_if(status.value.artifacts.begin(), status.value.artifacts.end(),
			                 [&](const auto& item) { return item.localFileId == output.localFileId; });
			FingerprintBuilder content;
			content.Add(output.bytes);
			if (previous == status.value.artifacts.end() || previous->kind != output.kind ||
			    previous->outputKey != output.outputKey || previous->displayName != output.displayName ||
			    previous->visibleInProject != output.visibleInProject || previous->size != output.bytes.size() ||
			    previous->contentFingerprint != content.ToString())
			{
				return false;
			}
		}
		return true;
	};
	const auto selected = selectedImports.find(asset->second.metadata.guid.ToString());
	if (sameOutputs() && selected != selectedImports.end() &&
	    selected->second == asset->second.metadata.guid.ToString() + ":" + target)
	{
		return {};
	}

	CachedImport candidate;
	candidate.importer = importer;
	candidate.importerVersion = importerVersion;
	candidate.target = target;
	candidate.settings = settings;
	candidate.fingerprint = status.value.fingerprint;
	candidate.dependencies = dependencies;
	candidate.mainObjectLocalFileId = mainObjectLocalFileId;
	Vector<VirtualPath> newlyWritten;
	for (const ImportedArtifact& artifact : artifacts)
	{
		FingerprintBuilder content;
		content.Add(artifact.bytes);
		const char* extension = ".bin";
		if (artifact.kind == "Mesh")
		{
			extension = ".ursamesh";
		}
		else if (artifact.kind == "Texture")
		{
			extension = ".ursatex";
		}
		else if (artifact.kind == "Shader")
		{
			extension = ".ursashaderasset";
		}
		else if (artifact.kind == "Model")
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
		                        ToUrsaString(std::to_string(artifact.localFileId)) + "-" + content.ToString() +
		                        extension;
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
		candidate.artifacts.push_back({artifact.localFileId, artifact.kind, std::move(artifactPath.value),
		                               content.ToString(), static_cast<Ursa::UInt64>(artifact.bytes.size()),
		                               artifact.outputKey, artifact.displayName, artifact.visibleInProject});
	}

	const String guid = asset->second.metadata.guid.ToString();
	const String key = guid + ":" + target;
	Map<String, CachedImport> nextImports = importsByKey;
	nextImports[key] = std::move(candidate);
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
	if (capturingCandidates)
	{
		candidatePublications.insert(key);
	}
	++revision;
	return {};
}

void AssetDatabase::BeginCandidateCapture()
{
	capturingCandidates = true;
	candidatePublications.clear();
}

Result<void> AssetDatabase::PublishCandidates(const AssetDatabase& candidates)
{
	if (&candidates == this || sourceScheme != candidates.sourceScheme || !candidates.capturingCandidates)
	{
		return Result<void>::Failure(
		    "Import publication requires a separate candidate database for the same source scheme");
	}
	if (candidates.candidatePublications.empty())
	{
		return {};
	}
	Map<String, CachedImport> nextImports = importsByKey;
	auto nextSelections = selectedImports;
	for (const auto& key : candidates.candidatePublications)
	{
		const auto entry = candidates.importsByKey.find(key);
		auto guid = ResourceId::Parse(key.substr(0, key.find(':')));
		if (entry == candidates.importsByKey.end() || !guid)
		{
			return Result<void>::Failure("Candidate import is missing its source identity");
		}
		auto source = SourcePath(guid.value);
		if (!source)
		{
			return Result<void>::Failure(source.error);
		}
		const auto& candidate = entry->second;
		auto current = Fingerprint(source.value, candidate.importer, candidate.importerVersion, candidate.target,
		                           candidate.settings, candidate.dependencies);
		if (!current || current.value != candidate.fingerprint)
		{
			return Result<void>::Failure("Candidate dependency inputs changed before publication: " +
			                             source.value.ToString());
		}
		nextImports[key] = candidate;
		const auto selection = candidates.selectedImports.find(guid.value.ToString());
		if (selection != candidates.selectedImports.end())
		{
			nextSelections[selection->first] = selection->second;
		}
	}

	struct StagedFiles
	{
		VirtualFileSystem& files;
		Vector<VirtualPath> paths;
		bool committed = false;

		~StagedFiles()
		{
			if (!committed)
			{
				for (const auto& path : paths)
				{
					files.Remove(path);
				}
			}
		}
	};

	StagedFiles staged{filesystem, {}, false};
	for (const auto& key : candidates.candidatePublications)
	{
		for (const auto& artifact : candidates.importsByKey.at(key).artifacts)
		{
			if (artifact.path.scheme != editorLibraryScheme)
			{
				return Result<void>::Failure("Candidate artifact is outside Library");
			}
			auto bytes = candidates.filesystem.Read(artifact.path);
			FingerprintBuilder content;
			if (bytes)
			{
				content.Add(bytes.value);
			}
			if (!bytes || bytes.value.size() != artifact.size || content.ToString() != artifact.contentFingerprint)
			{
				return Result<void>::Failure("Candidate artifact changed before publication: " +
				                             artifact.path.ToString());
			}
			auto exists = filesystem.Exists(artifact.path);
			if (!exists)
			{
				return Result<void>::Failure(exists.error);
			}
			if (exists.value)
			{
				auto previous = filesystem.Read(artifact.path);
				if (previous && previous.value == bytes.value)
				{
					continue;
				}
			}
			auto written = filesystem.WriteAtomic(artifact.path, bytes.value);
			if (!written)
			{
				return written;
			}
			if (!exists.value)
			{
				staged.paths.push_back(artifact.path);
			}
		}
	}
	const auto oldSelections = selectedImports;
	selectedImports = std::move(nextSelections);
	auto saved = SaveCache(nextImports);
	if (!saved)
	{
		selectedImports = oldSelections;
		return saved;
	}
	importsByKey = std::move(nextImports);
	if (capturingCandidates)
	{
		candidatePublications.insert(candidates.candidatePublications.begin(), candidates.candidatePublications.end());
	}
	++revision;
	staged.committed = true;
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
		                  editorScript ? ScriptAssemblyGroup::Editor : ScriptAssemblyGroup::Game,
		                  entry.second.metadata.scriptTypes});
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

Result<void> AssetDatabase::CommitTransfer(Vector<FileTransfer> files, bool regenerateGuids)
{
	Vector<VirtualPath> written;
	const auto rollback = [&]()
	{
		String failures;
		for (auto path = written.rbegin(); path != written.rend(); ++path)
		{
			auto removed = filesystem.Remove(*path);
			if (!removed)
			{
				failures += "\nremove staged copy " + path->ToString() + ": " + removed.error;
			}
		}
		auto refreshed = Refresh();
		if (!refreshed)
		{
			failures += "\nrefresh restored asset index: " + refreshed.error;
		}
		return failures;
	};
	for (FileTransfer& file : files)
	{
		Vector<Ursa::UInt8> bytes = file.bytes;
		if (regenerateGuids && EndsWith(file.source.relative, ".meta"))
		{
			auto metadata =
			    DecodeMetadata(ToUrsaString(std::string(file.bytes.begin(), file.bytes.end())), file.source);
			if (!metadata)
			{
				const String compensation = rollback();
				return Result<void>::Failure(metadata.error + compensation);
			}
			metadata.value.guid = ResourceId::Generate();
			metadata.value.scriptTypes = Json::object();
			auto encoded = EncodeMetadata(metadata.value);
			if (!encoded)
			{
				const String compensation = rollback();
				return Result<void>::Failure(encoded.error + compensation);
			}
			bytes = BytesOf(ToStdString(encoded.value));
		}
		auto saved = filesystem.WriteAtomic(file.destination, bytes);
		if (!saved)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot stage copy destination " + file.destination.ToString() + ": " +
			                             saved.error + compensation);
		}
		written.push_back(file.destination);
	}

	auto refreshed = Refresh();
	if (!refreshed)
	{
		const String compensation = rollback();
		return Result<void>::Failure("Asset copy rolled back after index refresh failed: " + refreshed.error +
		                             compensation);
	}
	return {};
}

Result<std::shared_ptr<VirtualFileLease>> AssetDatabase::AcquireAssetOperationLease() const
{
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return Result<std::shared_ptr<VirtualFileLease>>::Success({});
	}
	auto leasePath = VirtualPath::Parse(String(editorLibraryScheme) + ":Import.lock");
	if (!leasePath)
	{
		return Result<std::shared_ptr<VirtualFileLease>>::Failure(leasePath.error);
	}
	return filesystem.AcquireExclusiveLease(leasePath.value);
}

Result<void> AssetDatabase::Move(const VirtualPath& source, const VirtualPath& destination)
{
	return MoveBatch({AssetMoveRequest{source, destination}});
}

Result<void> AssetDatabase::MoveBatch(const Vector<AssetMoveRequest>& moves)
{
	if (moves.empty())
	{
		return {};
	}
	auto lease = AcquireAssetOperationLease();
	if (!lease)
	{
		return Result<void>::Failure(lease.error);
	}

	struct RootMove
	{
		VirtualPath source;
		VirtualPath destination;
		bool folder = false;
	};

	Vector<RootMove> roots;
	Vector<FileTransfer> files;
	Set<String> sourcePaths;
	Set<String> destinationPaths;
	Set<String> sourceFolders;
	Set<String> destinationFolders;
	Set<String> destinationParentFolders;
	for (const AssetMoveRequest& request : moves)
	{
		const auto record = assetsByPath.find(request.source.relative);
		if (record == assetsByPath.end() || request.source.scheme != sourceScheme)
		{
			return Result<void>::Failure("Asset is not indexed: " + request.source.ToString());
		}
		if (request.destination.scheme != sourceScheme)
		{
			return Result<void>::Failure("Asset move destination must use scheme " + sourceScheme);
		}
		if (!sourcePaths.insert(request.source.relative).second)
		{
			return Result<void>::Failure("An asset move batch contains a duplicate source: " +
			                             request.source.ToString());
		}
		for (String parentPath = ParentRelativePath(request.destination.relative); !parentPath.empty();
		     parentPath = ParentRelativePath(parentPath))
		{
			const auto parent = assetsByPath.find(parentPath);
			if (parent != assetsByPath.end() && parent->second.metadata.folder)
			{
				break;
			}
			destinationParentFolders.insert(parentPath);
		}
		RootMove root{request.source, request.destination, record->second.metadata.folder};
		roots.push_back(root);
		if (root.folder)
		{
			sourceFolders.insert(root.source.relative);
			for (const auto& asset : assetsByPath)
			{
				if (asset.second.metadata.folder &&
				    (asset.first == root.source.relative || IsPathWithin(asset.first, root.source.relative)))
				{
					destinationFolders.insert(root.destination.relative +
					                          asset.first.substr(root.source.relative.size()));
					sourceFolders.insert(asset.first);
				}
			}
		}
		auto staged = StageTransfer(root.source, root.destination, root.folder);
		if (!staged)
		{
			return Result<void>::Failure(staged.error);
		}
		for (auto& file : staged.value)
		{
			if (!sourcePaths.insert(file.source.relative).second && file.source.relative != root.source.relative)
			{
				return Result<void>::Failure("Move batch sources overlap at " + file.source.ToString());
			}
			if (!destinationPaths.insert(file.destination.relative).second)
			{
				return Result<void>::Failure("Move batch destinations overlap at " + file.destination.ToString());
			}
			files.push_back(std::move(file));
		}
	}
	for (Ursa::Size left = 0; left < roots.size(); ++left)
	{
		for (Ursa::Size right = left + 1; right < roots.size(); ++right)
		{
			if ((roots[left].folder && IsPathWithin(roots[right].source.relative, roots[left].source.relative)) ||
			    (roots[right].folder && IsPathWithin(roots[left].source.relative, roots[right].source.relative)))
			{
				return Result<void>::Failure("Move batch contains both a folder and one of its descendants");
			}
		}
	}
	for (const String& folder : destinationFolders)
	{
		destinationParentFolders.erase(folder);
	}

	Vector<VirtualPath> createdDestinationFolders;
	Vector<VirtualPath> writtenDestinations;
	Vector<Ursa::Size> removedSources;
	const auto rollback = [&]()
	{
		String failures;
		for (const RootMove& root : roots)
		{
			if (root.folder)
			{
				for (const String& folder : sourceFolders)
				{
					if (folder == root.source.relative || IsPathWithin(folder, root.source.relative))
					{
						auto restored = filesystem.CreateDirectory(VirtualPath{sourceScheme, folder});
						if (!restored)
						{
							failures += "\nrestore directory " + folder + ": " + restored.error;
						}
					}
				}
			}
		}
		for (Ursa::Size index : removedSources)
		{
			auto restored = filesystem.WriteAtomic(files[index].source, files[index].bytes);
			if (!restored)
			{
				failures += "\nrestore source " + files[index].source.ToString() + ": " + restored.error;
			}
		}
		for (auto destination = writtenDestinations.rbegin(); destination != writtenDestinations.rend(); ++destination)
		{
			auto removed = filesystem.Remove(*destination);
			if (!removed)
			{
				failures += "\nremove staged destination " + destination->ToString() + ": " + removed.error;
			}
		}
		for (auto directory = createdDestinationFolders.rbegin(); directory != createdDestinationFolders.rend();
		     ++directory)
		{
			auto removed = filesystem.RemoveDirectory(*directory, true);
			if (!removed)
			{
				failures += "\nremove staged destination directory " + directory->ToString() + ": " + removed.error;
			}
		}
		auto refreshed = Refresh();
		if (!refreshed)
		{
			failures += "\nrefresh restored asset index: " + refreshed.error;
		}
		return failures;
	};

	Vector<String> orderedDestinationParents(destinationParentFolders.begin(), destinationParentFolders.end());
	std::sort(orderedDestinationParents.begin(), orderedDestinationParents.end(),
	          [](const String& left, const String& right)
	          {
		          const Ursa::Size leftDepth = static_cast<Ursa::Size>(std::count(left.begin(), left.end(), '/'));
		          const Ursa::Size rightDepth = static_cast<Ursa::Size>(std::count(right.begin(), right.end(), '/'));
		          return leftDepth == rightDepth ? left < right : leftDepth < rightDepth;
	          });
	for (const String& folder : orderedDestinationParents)
	{
		auto destination = VirtualPath::Parse(sourceScheme + ":" + folder);
		if (!destination)
		{
			return Result<void>::Failure(destination.error);
		}
		auto created = filesystem.CreateDirectory(destination.value);
		if (!created)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot stage destination parent folder: " + created.error + compensation);
		}
		createdDestinationFolders.push_back(destination.value);
	}
	for (const String& folder : destinationFolders)
	{
		auto destination = VirtualPath::Parse(sourceScheme + ":" + folder);
		if (!destination)
		{
			return Result<void>::Failure(destination.error);
		}
		auto created = filesystem.CreateDirectory(destination.value);
		if (!created)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot stage destination folder: " + created.error + compensation);
		}
		createdDestinationFolders.push_back(destination.value);
	}
	for (FileTransfer& file : files)
	{
		auto written = filesystem.WriteAtomic(file.destination, file.bytes);
		if (!written)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot stage move destination " + file.destination.ToString() + ": " +
			                             written.error + compensation);
		}
		writtenDestinations.push_back(file.destination);
	}
	for (Ursa::Size index = 0; index < files.size(); ++index)
	{
		auto removed = filesystem.Remove(files[index].source);
		removedSources.push_back(index);
		if (!removed)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot remove move source " + files[index].source.ToString() + ": " +
			                             removed.error + compensation);
		}
	}
	for (const RootMove& root : roots)
	{
		if (!root.folder)
		{
			continue;
		}
		auto removed = filesystem.RemoveDirectory(root.source, true);
		if (!removed)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot remove moved source directory " + root.source.ToString() + ": " +
			                             removed.error + compensation);
		}
	}
	auto refreshed = Refresh();
	if (!refreshed)
	{
		const String compensation = rollback();
		return Result<void>::Failure("Asset move batch rolled back after index refresh failed: " + refreshed.error +
		                             compensation);
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
	auto lease = AcquireAssetOperationLease();
	if (!lease)
	{
		return Result<void>::Failure(lease.error);
	}
	auto files = StageTransfer(source, destination, record->second.metadata.folder);
	return files ? CommitTransfer(std::move(files.value), true) : Result<void>::Failure(files.error);
}

Result<void> AssetDatabase::Remove(const VirtualPath& path)
{
	return RemoveBatch({path});
}

Result<void> AssetDatabase::RemoveBatch(const Vector<VirtualPath>& paths)
{
	if (paths.empty())
	{
		return {};
	}
	auto lease = AcquireAssetOperationLease();
	if (!lease)
	{
		return Result<void>::Failure(lease.error);
	}
	Vector<VirtualPath> roots;
	Vector<FileTransfer> backups;
	Set<String> sourceFiles;
	Set<String> folders;
	for (const VirtualPath& path : paths)
	{
		const auto record = assetsByPath.find(path.relative);
		if (path.scheme != sourceScheme || record == assetsByPath.end())
		{
			return Result<void>::Failure("Asset is not indexed: " + path.ToString());
		}
		if (std::any_of(roots.begin(), roots.end(), [&](const VirtualPath& root) { return root == path; }))
		{
			return Result<void>::Failure("Remove batch contains a duplicate source: " + path.ToString());
		}
		for (const VirtualPath& root : roots)
		{
			const auto rootRecord = assetsByPath.find(root.relative);
			if ((rootRecord != assetsByPath.end() && rootRecord->second.metadata.folder &&
			     IsPathWithin(path.relative, root.relative)) ||
			    (record->second.metadata.folder && IsPathWithin(root.relative, path.relative)))
			{
				return Result<void>::Failure("Remove batch contains both a folder and one of its descendants");
			}
		}
		roots.push_back(path);
		auto files = FilesFor(path, record->second.metadata.folder);
		if (!files)
		{
			return Result<void>::Failure(files.error);
		}
		if (record->second.metadata.folder)
		{
			for (const auto& asset : assetsByPath)
			{
				if (asset.second.metadata.folder &&
				    (asset.first == path.relative || IsPathWithin(asset.first, path.relative)))
				{
					folders.insert(asset.first);
				}
			}
		}
		for (const VirtualPath& file : files.value)
		{
			if (!sourceFiles.insert(file.relative).second)
			{
				return Result<void>::Failure("Remove batch sources overlap at " + file.ToString());
			}
			auto writable = filesystem.CheckWritable(file);
			auto bytes = filesystem.Read(file);
			if (!writable || !bytes)
			{
				return Result<void>::Failure(!writable ? writable.error : bytes.error);
			}
			backups.push_back({file, file, std::move(bytes.value)});
		}
	}
	Vector<Ursa::Size> removed;
	const auto rollback = [&]()
	{
		String failures;
		for (const String& folder : folders)
		{
			auto restored = filesystem.CreateDirectory(VirtualPath{sourceScheme, folder});
			if (!restored)
			{
				failures += "\nrestore directory " + folder + ": " + restored.error;
			}
		}
		for (Ursa::Size index : removed)
		{
			auto restored = filesystem.WriteAtomic(backups[index].source, backups[index].bytes);
			if (!restored)
			{
				failures += "\nrestore source " + backups[index].source.ToString() + ": " + restored.error;
			}
		}
		auto refreshed = Refresh();
		if (!refreshed)
		{
			failures += "\nrefresh restored asset index: " + refreshed.error;
		}
		return failures;
	};
	for (Ursa::Size index = 0; index < backups.size(); ++index)
	{
		auto result = filesystem.Remove(backups[index].source);
		removed.push_back(index);
		if (!result)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot remove asset source " + backups[index].source.ToString() + ": " +
			                             result.error + compensation);
		}
	}
	for (const VirtualPath& root : roots)
	{
		if (!folders.count(root.relative))
		{
			continue;
		}
		auto result = filesystem.RemoveDirectory(root, true);
		if (!result)
		{
			const String compensation = rollback();
			return Result<void>::Failure("Cannot remove asset directory " + root.ToString() + ": " + result.error +
			                             compensation);
		}
	}
	auto refreshed = Refresh();
	if (!refreshed)
	{
		const String compensation = rollback();
		return Result<void>::Failure("Asset remove batch rolled back after index refresh failed: " + refreshed.error +
		                             compensation);
	}
	return {};
}
} // namespace Ursa::Editor
