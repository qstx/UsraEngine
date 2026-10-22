#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetImportRules.hpp>
#include <functional>

namespace Ursa::Editor
{
// JSON is internal archive storage; project-facing fields use the typed property adapter.
class ImportSettingsDraft final
{
public:
	using Validator = std::function<Result<void>(const nlohmann::json&)>;

	static Result<ImportSettingsDraft> Open(AssetDatabase& selectedDatabase, VirtualPath source,
	                                        AssetImporterDescriptor descriptor, nlohmann::json defaults,
	                                        Validator validate = {});

	const nlohmann::json& Settings() const
	{
		return draftSettings;
	}

	const nlohmann::json& PersistentSettings() const
	{
		return baselineSettings;
	}

	nlohmann::json EffectiveSettings() const
	{
		return baselineSettings;
	}

	bool HasModified() const
	{
		return draftSettings != baselineSettings;
	}

	Result<void> Edit(nlohmann::json settings);
	Result<bool> Apply();
	Result<void> Revert();

	void Discard()
	{
		draftSettings = baselineSettings;
	}

private:
	AssetDatabase* database = nullptr;
	VirtualPath sourcePath;
	AssetMetadata originalMetadata;
	AssetImporterDescriptor importer;
	nlohmann::json baselineSettings = nlohmann::json::object();
	nlohmann::json draftSettings = nlohmann::json::object();
	Validator validator;
};
} // namespace Ursa::Editor
