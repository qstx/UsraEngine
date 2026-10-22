#include <Assets/ImportSettingsDraft.hpp>

#include <Core/StringInterop.hpp>
#include <exception>

namespace Ursa::Editor
{
Result<ImportSettingsDraft> ImportSettingsDraft::Open(AssetDatabase& selectedDatabase, VirtualPath source,
                                                      AssetImporterDescriptor descriptor, nlohmann::json defaults,
                                                      Validator validate)
{
	if (descriptor.identity.empty() || !descriptor.version || !defaults.is_object())
	{
		return Result<ImportSettingsDraft>::Failure("Importer draft requires identity, version and default settings");
	}
	if (descriptor.scriptType)
	{
		auto identity = descriptor.scriptType->StableIdentity();
		if (!identity || identity.value != descriptor.identity)
		{
			return Result<ImportSettingsDraft>::Failure("Importer draft has a mismatched stable script identity");
		}
	}
	auto metadata = selectedDatabase.Metadata(source);
	if (!metadata || metadata.value.folder)
	{
		return Result<ImportSettingsDraft>::Failure(metadata ? "A folder cannot have an importer draft"
		                                                     : metadata.error);
	}
	if (!metadata.value.importer.empty() && metadata.value.importer != descriptor.identity)
	{
		return Result<ImportSettingsDraft>::Failure("Importer draft type changed for " + source.ToString());
	}
	ImportSettingsDraft result;
	result.database = &selectedDatabase;
	result.sourcePath = std::move(source);
	result.originalMetadata = std::move(metadata.value);
	result.importer = std::move(descriptor);
	result.baselineSettings =
	    result.originalMetadata.importer.empty() ? std::move(defaults) : result.originalMetadata.importSettings;
	result.draftSettings = result.baselineSettings;
	result.validator = std::move(validate);
	return Result<ImportSettingsDraft>::Success(std::move(result));
}

Result<void> ImportSettingsDraft::Edit(nlohmann::json settings)
{
	if (!database || !settings.is_object())
	{
		return Result<void>::Failure("Importer settings draft must be an active settings object");
	}
	draftSettings = std::move(settings);
	return {};
}

Result<bool> ImportSettingsDraft::Apply()
{
	if (!database)
	{
		return Result<bool>::Failure("Importer settings draft is not initialized");
	}
	if (!HasModified())
	{
		return Result<bool>::Success(false);
	}
	try
	{
		std::shared_ptr<VirtualFileLease> lease;
		if (database->Filesystem().IsMounted(editorLibraryScheme))
		{
			auto locked = database->Filesystem().AcquireExclusiveLease(VirtualPath::Parse("lib:Import.lock").value);
			if (!locked)
			{
				return Result<bool>::Failure(locked.error);
			}
			lease = std::move(locked.value);
		}
		if (validator)
		{
			auto valid = validator(draftSettings);
			if (!valid)
			{
				return Result<bool>::Failure(valid.error);
			}
		}
		auto refreshed = database->Refresh();
		if (!refreshed)
		{
			return Result<bool>::Failure(refreshed.error);
		}
		auto latest = database->Metadata(sourcePath);
		if (!latest || latest.value.guid != originalMetadata.guid ||
		    latest.value.importer != originalMetadata.importer ||
		    latest.value.importerVersion != originalMetadata.importerVersion ||
		    latest.value.importSettings != originalMetadata.importSettings)
		{
			return Result<bool>::Failure("Importer settings changed externally; reopen or recover the draft: " +
			                             sourcePath.ToString());
		}
		auto saved = database->SetImporterSettings(sourcePath, importer.identity, importer.version, draftSettings);
		if (!saved)
		{
			return Result<bool>::Failure(saved.error);
		}
		originalMetadata = std::move(latest.value);
		originalMetadata.importer = importer.identity;
		originalMetadata.importerVersion = importer.version;
		originalMetadata.importSettings = draftSettings;
		baselineSettings = draftSettings;
		return Result<bool>::Success(true);
	}
	catch (const std::exception& error)
	{
		return Result<bool>::Failure("Importer settings validation failed: " + ToUrsaString(error.what()));
	}
}

Result<void> ImportSettingsDraft::Revert()
{
	if (!database)
	{
		return Result<void>::Failure("Importer settings draft is not initialized");
	}
	auto refreshed = database->Refresh();
	if (!refreshed)
	{
		return refreshed;
	}
	auto latest = database->Metadata(sourcePath);
	if (!latest || latest.value.guid != originalMetadata.guid ||
	    (!latest.value.importer.empty() && latest.value.importer != importer.identity))
	{
		return Result<void>::Failure("Cannot revert settings for a missing or changed importer target");
	}
	if (!latest.value.importer.empty())
	{
		baselineSettings = latest.value.importSettings;
	}
	originalMetadata = std::move(latest.value);
	draftSettings = baselineSettings;
	return {};
}
} // namespace Ursa::Editor
