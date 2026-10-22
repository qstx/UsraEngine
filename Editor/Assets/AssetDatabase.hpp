#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/ResourceId.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Vfs/VirtualFileSystem.hpp>
#include <nlohmann/json.hpp>
#include <memory>

namespace Ursa::Editor
{
inline constexpr Ursa::UInt32 assetMetadataVersion = 1;

struct AssetMetadata
{
	ResourceId guid;
	String importer;
	Ursa::UInt32 importerVersion = 0;
	nlohmann::json importSettings = nlohmann::json::object();
	bool folder = false;
	nlohmann::json scriptTypes = nlohmann::json::object();
};

struct AssetRecord
{
	VirtualPath path;
	AssetMetadata metadata;
};

struct AssetGuidAssignment
{
	VirtualPath path;
	ResourceId guid;
};

using ScriptAssemblyGroup = Ursa::ScriptAssemblyGroup;

struct ScriptSourceAsset
{
	VirtualPath path;
	ResourceId guid;
	ScriptAssemblyGroup group = ScriptAssemblyGroup::Game;
	nlohmann::json identities = nlohmann::json::object();
};

struct ImportedArtifact
{
	Ursa::UInt64 localFileId = 0;
	String kind;
	Vector<Ursa::UInt8> bytes;
	String outputKey;
};

struct ImportedArtifactLocation
{
	Ursa::UInt64 localFileId = 0;
	String kind;
	VirtualPath path;
	String contentFingerprint;
	Ursa::UInt64 size = 0;
	String outputKey;
};

struct ImportedArtifactRecord
{
	ResourceId guid;
	Ursa::UInt64 localFileId = 0;
	String kind;
	VirtualPath path;
	bool isMainObject = false;
};

struct ImportCacheStatus
{
	bool upToDate = false;
	String fingerprint;
	Vector<ImportedArtifactLocation> artifacts;
	Vector<VirtualPath> dependencies;
	nlohmann::json settings = nlohmann::json::object();
	UInt64 mainObjectLocalFileId = 0;
	String importer;
	UInt32 importerVersion = 0;
	String target;
};

class AssetDatabase final
{
public:
	static Result<std::unique_ptr<AssetDatabase>> Open(VirtualFileSystem& filesystem,
	                                                   String sourceScheme = projectScheme);

	Result<void> Refresh();

	Result<void> ReloadImports();

	Result<void> CreateFolder(const VirtualPath& path);

	Result<AssetMetadata> Metadata(const VirtualPath& path) const;

	Result<void> UpdateScriptIdentities(const ScriptSourceAsset& source, const nlohmann::json& identities);

	Result<void> SetImporterSettings(const VirtualPath& path, const String& importer, Ursa::UInt32 importerVersion,
	                                 const nlohmann::json& settings);

	Result<VirtualPath> Resolve(ResourceId guid, Ursa::UInt64 localFileId = 0) const;
	Result<ImportedArtifactLocation> ResolveImportedArtifact(ResourceId guid, UInt64 localFileId = 0) const;
	Result<VirtualPath> SourcePath(ResourceId guid) const;

	bool Contains(ResourceId guid) const;

	Result<void> ApplyGuidMigration(const Vector<AssetGuidAssignment>& assignments);

	Result<ImportCacheStatus> CheckImport(const VirtualPath& source, const String& importer,
	                                      Ursa::UInt32 importerVersion, const String& target,
	                                      const nlohmann::json& settings, const Vector<VirtualPath>& dependencies);

	Result<Vector<VirtualPath>> ImportDependencies(const VirtualPath& source, const String& importer) const;

	Result<Vector<ImportedArtifactRecord>> CurrentImportedArtifacts();

	Vector<ImportedArtifactRecord> ImportedArtifacts() const;

	Result<ImportCacheStatus> CurrentImport(const VirtualPath& source);

	Result<void> PublishImport(const VirtualPath& source, const String& importer, Ursa::UInt32 importerVersion,
	                           const String& target, const nlohmann::json& settings,
	                           const Vector<VirtualPath>& dependencies, const Vector<ImportedArtifact>& artifacts,
	                           UInt64 mainObjectLocalFileId = 0);

	void BeginCandidateCapture();
	Result<void> PublishCandidates(const AssetDatabase& candidates);

	Vector<AssetRecord> Assets() const;

	Ursa::UInt64 Revision() const
	{
		return revision;
	}

	Vector<ScriptSourceAsset> ScriptSources() const;

	Result<void> Move(const VirtualPath& source, const VirtualPath& destination);

	Result<void> Copy(const VirtualPath& source, const VirtualPath& destination);

	Result<void> Remove(const VirtualPath& path);

	const String& SourceScheme() const
	{
		return sourceScheme;
	}

	VirtualFileSystem& Filesystem()
	{
		return filesystem;
	}

private:
	struct CachedImport;

	const CachedImport* FindImport(const String& guid) const;

	struct FileTransfer
	{
		VirtualPath source;
		VirtualPath destination;
		Vector<Ursa::UInt8> bytes;
	};

	struct CachedImport
	{
		String importer;
		Ursa::UInt32 importerVersion = 0;
		String target;
		nlohmann::json settings = nlohmann::json::object();
		String fingerprint;
		Vector<VirtualPath> dependencies;
		Vector<ImportedArtifactLocation> artifacts;
		UInt64 mainObjectLocalFileId = 0;
	};

	AssetDatabase(VirtualFileSystem& selectedFilesystem, String selectedScheme)
	    : filesystem(selectedFilesystem), sourceScheme(std::move(selectedScheme))
	{
	}

	Result<VirtualPath> SidecarPath(const VirtualPath& assetPath) const;

	Result<AssetMetadata> ReadMetadata(const VirtualPath& assetPath) const;

	Result<void> WriteMetadata(const VirtualPath& assetPath, const AssetMetadata& metadata);

	Result<Vector<FileTransfer>> StageTransfer(const VirtualPath& source, const VirtualPath& destination,
	                                           bool sourceIsFolder) const;

	Result<void> CommitTransfer(Vector<FileTransfer> files, bool moveSource, bool regenerateGuids);

	Result<Vector<VirtualPath>> FilesFor(const VirtualPath& path, bool isFolder) const;

	Result<String> Fingerprint(const VirtualPath& source, const String& importer, Ursa::UInt32 importerVersion,
	                           const String& target, const nlohmann::json& settings,
	                           const Vector<VirtualPath>& dependencies) const;

	Result<void> LoadCache();

	Result<void> SaveCache(const Map<String, CachedImport>& imports) const;

	VirtualFileSystem& filesystem;
	String sourceScheme;
	Map<String, AssetRecord> assetsByPath;
	Map<String, String> pathsByGuid;
	Map<String, CachedImport> importsByKey;
	Map<String, String> selectedImports;
	bool cacheLoaded = false;
	Ursa::UInt64 revision = 0;
	bool capturingCandidates = false;
	Set<String> candidatePublications;
};
} // namespace Ursa::Editor
