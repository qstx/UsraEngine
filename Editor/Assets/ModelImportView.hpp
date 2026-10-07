#pragma once

#include <Assets/ModelImporter.hpp>
#include <functional>

namespace Ursa::Editor
{
class SceneDraft;

class ModelImportView final
{
public:
	static Result<std::unique_ptr<ModelImportView>> Open(VirtualPath source, ModelImportSettings settings);

	Result<void> Reimport(AssetDatabase& database);

	Result<void> Draw(AssetDatabase& database, bool canReimport = true,
	                  const std::function<Result<void>()>& instantiate = {});

	Result<void> Instantiate(SceneDraft& draft) const;

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
	bool dirty = false;
};
} // namespace Ursa::Editor
