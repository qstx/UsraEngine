#include <Assets/ManagedImportRequirements.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/BuildResourceManifest.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

VirtualPath ManifestPath()
{
	return VirtualPath::Parse("settings:ManagedImportRules.json").value;
}

Result<Json> SourceInventory(AssetDatabase& database)
{
	Json inventory = Json::array();
	auto sources = database.ScriptSources();
	std::sort(sources.begin(), sources.end(),
	          [](const auto& a, const auto& b) { return a.path.ToString() < b.path.ToString(); });
	for (const auto& source : sources)
	{
		const auto bytes = database.Filesystem().Read(source.path);
		if (!bytes)
		{
			return Result<Json>::Failure(bytes.error);
		}
		inventory.push_back({{"path", ToStdString(source.path.ToString())},
		                     {"guid", ToStdString(source.guid.ToString())},
		                     {"identities", source.identities},
		                     {"fingerprint", ToStdString(BuildResourceFingerprint(bytes.value))}});
	}
	return Result<Json>::Success(std::move(inventory));
}
} // namespace

Result<void> SaveManagedImportRequirements(AssetDatabase& database, const AssetImportRules& rules)
{
	const auto inventory = SourceInventory(database);
	if (!inventory)
	{
		return Result<void>::Failure(inventory.error);
	}
	Json required = Json::array();
	for (const auto& importer : rules.Importers())
	{
		if (importer.scriptType)
		{
			required.push_back(ToStdString(importer.identity));
		}
	}
	for (const auto& processor : rules.PostprocessorsFor("ursa.Texture", projectScheme))
	{
		required.push_back(ToStdString(processor.type.StableIdentity().value));
	}
	for (const auto& processor : rules.PostprocessorsFor("ursa.Model", projectScheme))
	{
		const auto identity = ToStdString(processor.type.StableIdentity().value);
		if (std::find(required.begin(), required.end(), Json(identity)) == required.end())
		{
			required.push_back(identity);
		}
	}
	const auto text =
	    Json{{"format", "UrsaManagedImportRules"}, {"version", 1}, {"sources", inventory.value}, {"required", required}}
	        .dump(2) +
	    "\n";
	auto& files = database.Filesystem();
	if (!files.IsMounted(projectSettingsScheme) && inventory.value.empty())
	{
		return {};
	}
	const auto exists = files.Exists(ManifestPath());
	if (!exists)
	{
		return Result<void>::Failure(exists.error);
	}
	if (exists.value)
	{
		const auto previous = files.ReadText(ManifestPath());
		if (!previous)
		{
			return Result<void>::Failure(previous.error);
		}
		if (previous.value == ToUrsaString(text))
		{
			return {};
		}
	}
	else if (inventory.value.empty() && required.empty())
	{
		return {};
	}
	return files.WriteAtomic(ManifestPath(), Vector<UInt8>(text.data(), text.data() + text.size()));
}

Result<void> CheckNativeImportCapability(AssetDatabase& database)
{
	const auto refreshed = database.Refresh();
	if (!refreshed)
	{
		return refreshed;
	}
	for (const auto& asset : database.Assets())
	{
		if (asset.metadata.importer.find("script:") == 0)
		{
			return Result<void>::Failure("Required ScriptedImporter needs .NET support: " + asset.path.ToString());
		}
	}
	auto& files = database.Filesystem();
	if (!files.IsMounted(projectSettingsScheme))
	{
		return database.ScriptSources().empty()
		           ? Result<void>{}
		           : Result<void>::Failure("Project scripts require a managed import capability declaration in "
		                                   "ProjectSettings/ManagedImportRules.json");
	}
	const auto exists = files.Exists(ManifestPath());
	if (!exists)
	{
		return Result<void>::Failure(exists.error);
	}
	if (!exists.value)
	{
		if (database.ScriptSources().empty())
		{
			return {};
		}
		return Result<void>::Failure(
		    "Project script import requirements are unknown. Run a .NET-enabled UrsaAssetImport to generate "
		    "ProjectSettings/ManagedImportRules.json before using a native-only tool");
	}
	try
	{
		const auto text = files.ReadText(ManifestPath());
		if (!text)
		{
			return Result<void>::Failure(text.error);
		}
		const auto manifest = Json::parse(ToStdString(text.value));
		if (manifest.at("format") != "UrsaManagedImportRules" || manifest.at("version") != 1 ||
		    !manifest.at("required").is_array() || !manifest.at("sources").is_array())
		{
			return Result<void>::Failure(
			    "Unsupported managed import requirements format; regenerate with the current .NET-enabled tool");
		}
		if (!manifest.at("required").empty())
		{
			return Result<void>::Failure("Project importer/postprocessor rules require .NET support: " +
			                             ToUrsaString(manifest.at("required").dump()));
		}
		const auto current = SourceInventory(database);
		if (!current)
		{
			return Result<void>::Failure(current.error);
		}
		if (current.value != manifest.at("sources"))
		{
			return Result<void>::Failure("Project scripts changed since import capability discovery; regenerate "
			                             "ManagedImportRules.json with a .NET-enabled tool");
		}
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Invalid managed import requirements: " + ToUrsaString(error.what()));
	}
}
} // namespace Ursa::Editor
