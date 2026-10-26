#include <Workspace/ProjectTagLayerSettings.hpp>
#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Ursa::Editor
{
namespace
{
constexpr const char* settingsPath = "settings:TagManager.json";

bool ValidNames(const Vector<String>& names, bool allowEmpty)
{
	Set<String> unique;
	for (const auto& name : names)
	{
		if (name.empty())
		{
			if (allowEmpty)
			{
				continue;
			}
			return false;
		}
		if (name.size() > 64 || name.find('\0') != String::npos || !unique.insert(name).second)
		{
			return false;
		}
	}
	return true;
}
} // namespace

ProjectTagLayerSettings ProjectTagLayerSettings::Defaults()
{
	ProjectTagLayerSettings settings;
	settings.tags = {"Untagged", "Respawn", "Finish", "EditorOnly", "MainCamera", "Player", "GameController"};
	settings.layers[0] = "Default";
	settings.layers[1] = "TransparentFX";
	settings.layers[2] = "Ignore Raycast";
	settings.layers[4] = "Water";
	settings.layers[5] = "UI";
	return settings;
}

Result<ProjectTagLayerSettings> ProjectTagLayerSettings::Read(const VirtualFileSystem& files)
{
	const auto path = VirtualPath::Parse(settingsPath).value;
	auto exists = files.Exists(path);
	if (!exists)
	{
		return Result<ProjectTagLayerSettings>::Failure(exists.error);
	}
	if (!exists.value)
	{
		return Result<ProjectTagLayerSettings>::Success(Defaults());
	}
	auto text = files.ReadText(path);
	if (!text)
	{
		return Result<ProjectTagLayerSettings>::Failure(text.error);
	}
	try
	{
		const auto document = nlohmann::json::parse(ToStdString(text.value));
		if (!document.is_object() || document.value("format", std::string{}) != "UrsaTagManager" ||
		    document.value("version", 0u) != 1u || !document.contains("tags") || !document.at("tags").is_array() ||
		    !document.contains("layers") || !document.at("layers").is_array() || document.at("layers").size() != 32)
		{
			return Result<ProjectTagLayerSettings>::Failure("Invalid Tag Manager settings schema");
		}
		ProjectTagLayerSettings settings;
		for (const auto& tag : document.at("tags"))
		{
			if (!tag.is_string())
			{
				return Result<ProjectTagLayerSettings>::Failure("Tag Manager tags must be strings");
			}
			settings.tags.push_back(ToUrsaString(tag.get<std::string>()));
		}
		for (Size index = 0; index < settings.layers.size(); ++index)
		{
			const auto& layer = document.at("layers").at(index);
			if (!layer.is_string())
			{
				return Result<ProjectTagLayerSettings>::Failure("Tag Manager layers must be strings");
			}
			settings.layers[index] = ToUrsaString(layer.get<std::string>());
		}
		Vector<String> layers(settings.layers.begin(), settings.layers.end());
		if (settings.tags.empty() || settings.tags.front() != "Untagged" || !ValidNames(settings.tags, false) ||
		    !ValidNames(layers, true) || settings.layers[0] != "Default")
		{
			return Result<ProjectTagLayerSettings>::Failure(
			    "Tag Manager names must be unique and preserve built-in slots");
		}
		return Result<ProjectTagLayerSettings>::Success(std::move(settings));
	}
	catch (const std::exception& error)
	{
		return Result<ProjectTagLayerSettings>::Failure(String("Cannot parse Tag Manager settings: ") +
		                                                ToUrsaString(error.what()));
	}
}

Result<void> ProjectTagLayerSettings::Save(VirtualFileSystem& files) const
{
	Vector<String> layerNames(layers.begin(), layers.end());
	if (tags.empty() || tags.front() != "Untagged" || !ValidNames(tags, false) || !ValidNames(layerNames, true) ||
	    layers[0] != "Default")
	{
		return Result<void>::Failure("Tag Manager names must be unique and preserve built-in slots");
	}
	nlohmann::json tagArray = nlohmann::json::array();
	for (const auto& tag : tags)
	{
		tagArray.push_back(ToStdString(tag));
	}
	nlohmann::json layerArray = nlohmann::json::array();
	for (const auto& layer : layers)
	{
		layerArray.push_back(ToStdString(layer));
	}
	const String text = ToUrsaString(nlohmann::json{
	    {"format", "UrsaTagManager"}, {"version", 1}, {"tags", std::move(tagArray)}, {"layers", std::move(layerArray)}}
	                                     .dump(2));
	return files.WriteAtomic(VirtualPath::Parse(settingsPath).value, Vector<UInt8>(text.begin(), text.end()));
}
} // namespace Ursa::Editor
