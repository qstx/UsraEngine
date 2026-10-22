#pragma once

#include <Assets/ModelImporter.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <functional>

namespace Ursa::Editor
{
class SceneDraft;
class ProjectAssetImporter;
class AssetImporterInspector;

class ModelImportView final
{
public:
	static Result<std::unique_ptr<ModelImportView>> Open(VirtualPath source, ModelImportSettings settings);

	Result<void> Reimport(AssetDatabase& database, ProjectAssetImporter& importer, bool cachedOnly = false);

	Result<void> Draw(AssetDatabase& database, ProjectAssetImporter& importer, bool canReimport = true,
	                  const std::function<Result<void>()>& instantiate = {},
	                  const std::function<void(const VirtualPath&)>& requestImport = {},
	                  ProjectWorkspace* workspace = nullptr, const AssetImporterInspector::CustomDraw& custom = {},
	                  bool embedded = false);

	void SetSettingsInspector(std::shared_ptr<AssetImporterInspector> value)
	{
		settingsInspector = std::move(value);
	}

	Result<void> Instantiate(SceneDraft& draft) const;
	Result<std::shared_ptr<AssetImporterInspector>> SettingsInspector(AssetDatabase& database);
	bool HasModifiedSettings() const;

	const VirtualPath& SourcePath() const
	{
		return sourcePath;
	}

	const String& Diagnostic() const
	{
		return diagnostic;
	}

private:
	explicit ModelImportView(VirtualPath source, ModelImportSettings sourceSettings)
	    : sourcePath(std::move(source)), settings(sourceSettings)
	{
	}

	Result<void> RefreshImportedDocument(const VirtualFileSystem& filesystem);

	VirtualPath sourcePath;
	ModelImportSettings settings;
	ModelImportResult imported;
	nlohmann::json document;
	String diagnostic;
	bool hasImported = false;
	std::shared_ptr<AssetImporterInspector> settingsInspector;
};
} // namespace Ursa::Editor
