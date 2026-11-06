#include <Application/SceneBuildSettings.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/JsonResource.hpp>
#include <set>
#include <cstring>
#include <stdexcept>

namespace Ursa
{
namespace
{
String SceneName(const VirtualPath& path)
{
	const auto slash = path.relative.find_last_of('/');
	const auto name = slash == String::npos ? path.relative : path.relative.substr(slash + 1);
	return name.substr(0, name.size() - String(".ursascene").size());
}

bool IsScenePath(const VirtualPath& path)
{
	constexpr const char* extension = ".ursascene";
	return path.scheme == projectScheme && path.relative.size() > std::strlen(extension) &&
	       path.relative.substr(path.relative.size() - std::strlen(extension)) == extension;
}

void ValidateSceneCatalog(const VirtualFileSystem& files, const ResourceSession::AssetPathResolver& resolver,
                          const nlohmann::json& reference, const SceneBuildSettings& settings)
{
	auto asset = AssetReferenceFromJson(reference, "BuildSettings.sceneCatalog");
	if (!asset || !resolver)
	{
		throw std::runtime_error("Invalid scene catalog asset reference");
	}
	auto path = resolver(asset.value.guid, 0);
	if (!path)
	{
		throw std::runtime_error(path.error.c_str());
	}
	auto text = files.ReadText(path.value);
	if (!text)
	{
		throw std::runtime_error(text.error.c_str());
	}
	const auto catalog = nlohmann::json::parse(ToStdString(text.value));
	if (catalog.value("format", "") != "UrsaScriptableAsset" || catalog.value("version", 0) != 1 ||
	    (asset.value.localFileId != 0 && catalog.at("localFileId").get<UInt64>() != asset.value.localFileId))
	{
		throw std::runtime_error("Scene catalog must reference a version 1 ScriptableObject asset");
	}
	const auto& fields = catalog.at("payload").at("Fields");
	const auto& ids = fields.at("ids");
	const auto& scenes = fields.at("scenes");
	const auto& titles = fields.at("titles");
	const auto& descriptions = fields.at("descriptions");
	if (!ids.is_array() || !scenes.is_array() || !titles.is_array() || !descriptions.is_array() ||
	    ids.size() != scenes.size() || ids.size() != titles.size() || ids.size() != descriptions.size() ||
	    settings.scenes.size() != scenes.size() + 1)
	{
		throw std::runtime_error("Scene catalog columns or enabled Scene set differ");
	}
	std::set<std::string> uniqueIds;
	for (Size i = 0; i < ids.size(); ++i)
	{
		if (!ids[i].is_string() || ids[i].get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos ||
		    !uniqueIds.insert(ids[i].get<std::string>()).second || !titles[i].is_string() ||
		    titles[i].get<std::string>().find_first_not_of(" \t\r\n") == std::string::npos ||
		    !descriptions[i].is_string())
		{
			throw std::runtime_error("Scene catalog requires unique nonempty IDs, titles and text descriptions");
		}
	}
	for (Size i = 0; i < settings.scenes.size(); ++i)
	{
		const auto item =
		    AssetReferenceFromJson(i == 0 ? fields.at("menuScene") : scenes[i - 1], "Scene catalog Scene");
		if (!item || item.value.guid != settings.scenes[i].reference.guid ||
		    item.value.localFileId != settings.scenes[i].reference.localFileId)
		{
			throw std::runtime_error("Scene catalog and enabled BuildSettings Scene order differ");
		}
	}
}
} // namespace

Result<BuildScene> SceneBuildSettings::Resolve(Int32 index) const
{
	if (index < 0 || static_cast<Size>(index) >= scenes.size())
	{
		return Result<BuildScene>::Failure("Scene Build Index is out of range: " + ToUrsaString(std::to_string(index)));
	}
	return Result<BuildScene>::Success(scenes[static_cast<Size>(index)]);
}

Result<BuildScene> SceneBuildSettings::Resolve(const String& nameOrPath, String* diagnostic) const
{
	if (diagnostic)
	{
		diagnostic->clear();
	}
	if (nameOrPath.empty())
	{
		return Result<BuildScene>::Failure("Scene name/path is empty");
	}
	String relative = nameOrPath;
	if (relative.rfind("prj:", 0) == 0)
	{
		relative.erase(0, 4);
	}
	if (relative.rfind("Assets/", 0) == 0)
	{
		relative.erase(0, 7);
	}
	const bool fullPath = relative.find('/') != String::npos || relative.find(".ursascene") != String::npos;
	const BuildScene* found = nullptr;
	for (const auto& scene : scenes)
	{
		if ((fullPath && (relative == scene.path.relative || relative + ".ursascene" == scene.path.relative)) ||
		    (!fullPath && relative == scene.name))
		{
			if (!found)
			{
				found = &scene;
			}
			else if (diagnostic)
			{
				*diagnostic = "Multiple scenes named '" + nameOrPath +
				              "'; using the first enabled Build Index. Use a full asset path to select another scene.";
			}
		}
	}
	return found ? Result<BuildScene>::Success(*found)
	             : Result<BuildScene>::Failure("Scene is not enabled in BuildSettings: " + nameOrPath);
}

Result<BuildScene> SceneBuildSettings::Resolve(AssetReference reference) const
{
	for (const auto& scene : scenes)
	{
		if (scene.reference.guid == reference.guid && scene.reference.localFileId == reference.localFileId)
		{
			return Result<BuildScene>::Success(scene);
		}
	}
	return Result<BuildScene>::Failure("Scene asset is not enabled in BuildSettings: " + reference.guid.ToString());
}

Result<SceneBuildSettings> ReadSceneBuildSettings(const VirtualFileSystem& files,
                                                  const ResourceSession::AssetPathResolver& resolver, bool runtime)
{
	const auto path = VirtualPath::Parse(runtime ? sceneBuildSettingsRuntime : sceneBuildSettingsSource).value;
	if (!files.IsMounted(path.scheme))
	{
		return Result<SceneBuildSettings>::Success({});
	}
	auto exists = files.Exists(path);
	if (!exists)
	{
		return Result<SceneBuildSettings>::Failure(exists.error);
	}
	if (!exists.value)
	{
		return Result<SceneBuildSettings>::Success({});
	}
	auto bytes = files.Read(path);
	if (!bytes)
	{
		return Result<SceneBuildSettings>::Failure(bytes.error);
	}
	try
	{
		if (bytes.value.size() > 4 * 1024 * 1024)
		{
			throw std::runtime_error("Scene list exceeds four MiB");
		}
		const auto json = nlohmann::json::parse(bytes.value.begin(), bytes.value.end());
		if (!json.is_object() ||
		    json.value("format", std::string{}) != (runtime ? "UrsaSceneBuildList" : "UrsaBuildSettings") ||
		    !json.contains("version") || json.at("version") != 1 || !json.contains("scenes") ||
		    !json["scenes"].is_array() || json["scenes"].size() > 4096)
		{
			throw std::runtime_error("Invalid or unsupported BuildSettings schema");
		}
		SceneBuildSettings result;
		result.configured = true;
		Set<String> identities;
		for (const auto& entry : json["scenes"])
		{
			if (!entry.is_object() || !entry.contains("scene") ||
			    (!runtime && (!entry.contains("enabled") || !entry["enabled"].is_boolean())))
			{
				throw std::runtime_error("Each scene entry requires a stable reference and enabled flag");
			}
			auto reference = AssetReferenceFromJson(entry["scene"], path.ToString() + ".scenes.scene");
			if (!reference || !reference.value.guid)
			{
				throw std::runtime_error(reference ? "Scene GUID is empty" : reference.error.c_str());
			}
			if (!runtime && !entry["enabled"].get<bool>())
			{
				continue;
			}
			const String key =
			    reference.value.guid.ToString() + ":" + ToUrsaString(std::to_string(reference.value.localFileId));
			if (!identities.insert(key).second)
			{
				throw std::runtime_error("Duplicate enabled Scene asset identity");
			}
			BuildScene scene;
			scene.reference = reference.value;
			scene.buildIndex = static_cast<Int32>(result.scenes.size());
			if (runtime)
			{
				if (!entry.contains("path") || !entry["path"].is_string() || !entry.contains("name") ||
				    !entry["name"].is_string() || !entry.contains("buildIndex") ||
				    entry["buildIndex"] != scene.buildIndex)
				{
					throw std::runtime_error("Invalid runtime Scene entry");
				}
				auto scenePath = VirtualPath::Parse(ToUrsaString(entry["path"].get<std::string>()));
				if (!scenePath)
				{
					throw std::runtime_error(scenePath.error.c_str());
				}
				scene.path = scenePath.value;
				scene.name = ToUrsaString(entry["name"].get<std::string>());
			}
			else
			{
				if (!resolver)
				{
					throw std::runtime_error("BuildSettings requires an asset identity resolver");
				}
				auto scenePath = resolver(scene.reference.guid, scene.reference.localFileId);
				if (!scenePath)
				{
					throw std::runtime_error(scenePath.error.c_str());
				}
				scene.path = scenePath.value;
				if (!ReadJsonResource(files, scene.path, ResourceKind::Scene))
				{
					throw std::runtime_error("Enabled BuildSettings reference is not a readable Scene");
				}
				scene.name = SceneName(scene.path);
			}
			if (!IsScenePath(scene.path) || scene.name.empty())
			{
				throw std::runtime_error("BuildSettings reference must name a project Scene");
			}
			result.scenes.push_back(std::move(scene));
		}
		if (!runtime && json.contains("sceneCatalog"))
		{
			ValidateSceneCatalog(files, resolver, json["sceneCatalog"], result);
		}
		return Result<SceneBuildSettings>::Success(std::move(result));
	}
	catch (const std::exception& error)
	{
		return Result<SceneBuildSettings>::Failure(path.ToString() + ": " + ToUrsaString(error.what()));
	}
}

Result<Vector<UInt8>> EncodeSceneBuildSettings(const SceneBuildSettings& settings)
{
	nlohmann::json result{{"format", "UrsaSceneBuildList"}, {"version", 1u}, {"scenes", nlohmann::json::array()}};
	for (Size index = 0; index < settings.scenes.size(); ++index)
	{
		const auto& scene = settings.scenes[index];
		if (!scene.reference.guid || !IsScenePath(scene.path) || scene.name.empty() ||
		    scene.buildIndex != static_cast<Int32>(index))
		{
			return Result<Vector<UInt8>>::Failure("Invalid Scene build list entry");
		}
		result["scenes"].push_back({{"scene", AssetReferenceToJson(scene.reference)},
		                            {"name", ToStdString(scene.name)},
		                            {"path", ToStdString(scene.path.ToString())},
		                            {"buildIndex", scene.buildIndex}});
	}
	const auto text = result.dump(2);
	return Result<Vector<UInt8>>::Success(Vector<UInt8>(text.data(), text.data() + text.size()));
}
} // namespace Ursa
