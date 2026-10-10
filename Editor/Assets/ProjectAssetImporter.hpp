#pragma once

#include <Assets/AssetDatabase.hpp>
#include <functional>

namespace Ursa::Editor
{
struct AssetImportResult
{
	ResourceId guid;
	Vector<ImportedArtifactLocation> artifacts;
	Vector<VirtualPath> dependencies;
	bool cacheHit = false;
	Vector<String> diagnostics;
};

class ProjectAssetImporter final
{
public:
	using ImportFunction = std::function<Result<AssetImportResult>(AssetDatabase&, const VirtualPath&, bool)>;

	ProjectAssetImporter();

	Result<void> Register(const String& extension, ImportFunction importer);

	static Result<AssetImportResult> LoadCurrent(AssetDatabase& database, const VirtualPath& source);

	Result<AssetImportResult> Import(AssetDatabase& database, const VirtualPath& source, bool force = false) const;

private:
	Map<String, ImportFunction> importers;
};
} // namespace Ursa::Editor
