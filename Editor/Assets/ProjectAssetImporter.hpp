#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Scripting/ScriptTypeDescriptor.hpp>
#include <Serialization/AssetReference.hpp>
#include <functional>
#include <atomic>

namespace Ursa::Editor
{
struct ArtifactDependencyRequest
{
	VirtualPath source;
	AssetReference reference;
	nlohmann::json initialSettings = nlohmann::json::object();
};

struct AssetImportResult
{
	ResourceId guid;
	Vector<ImportedArtifactLocation> artifacts;
	Vector<VirtualPath> dependencies;
	bool cacheHit = false;
	Vector<String> diagnostics;
	UInt64 mainObjectLocalFileId = 0;
	Vector<ArtifactDependencyRequest> pendingArtifacts;
};

class ProjectAssetImporter final
{
public:
	using ImportFunction = std::function<Result<AssetImportResult>(AssetDatabase&, const VirtualPath&, bool)>;

	ProjectAssetImporter();
	ProjectAssetImporter(const ProjectAssetImporter&) = delete;
	ProjectAssetImporter& operator=(const ProjectAssetImporter&) = delete;

	UInt64 Generation() const
	{
		return std::atomic_load(&registrations)->generation;
	}

	Result<void> Register(const String& extension, ImportFunction importer);
	Result<void> RegisterScripted(const String& extension, const String& identity, UInt32 version,
	                              ImportFunction importer);
	Result<void> RegisterNativeProcessing(const String& identity, const Vector<String>& extensions,
	                                      ImportFunction importer);

	Result<void> RegisterScriptableAssets(Vector<Ursa::ScriptTypeDescriptor> types);
	Result<void> ReplaceManagedRules(const ProjectAssetImporter& prepared, UInt64 token = 0);

	UInt64 ManagedRulesToken() const
	{
		return std::atomic_load(&registrations)->managedRulesToken;
	}

	static Result<AssetImportResult> LoadCurrent(AssetDatabase& database, const VirtualPath& source);

	Result<AssetImportResult> Import(AssetDatabase& database, const VirtualPath& source, bool force = false) const;
	Result<Vector<AssetImportResult>> ImportBatch(AssetDatabase& database, const Vector<VirtualPath>& sources,
	                                              bool force = false) const;

private:
	struct Registry
	{
		Map<String, ImportFunction> importers;
		Map<String, ImportFunction> nativeImporters;
		Map<String, String> scriptedIdentities;
		Map<String, UInt32> scriptedVersions;
		Set<String> processedExtensions;
		UInt64 generation = 1;
		UInt64 managedRulesToken = 0;
	};

	std::shared_ptr<const Registry> registrations;
	Result<void> Register(const String& extension, ImportFunction importer, const String& scriptedIdentity,
	                      UInt32 version = 0);
};
} // namespace Ursa::Editor
