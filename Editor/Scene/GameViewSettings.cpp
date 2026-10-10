#include <Scene/GameViewSettings.hpp>

#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <exception>
#include <string>

namespace Ursa::Editor
{
namespace
{
const char* ModeName(GameViewSizeMode mode)
{
	switch (mode)
	{
		case GameViewSizeMode::FreeAspect:
			return "FreeAspect";
		case GameViewSizeMode::AspectRatio:
			return "AspectRatio";
		case GameViewSizeMode::FixedResolution:
			return "FixedResolution";
	}
	return "FreeAspect";
}

GameViewSizeMode ParseMode(const String& mode)
{
	if (mode == "AspectRatio")
	{
		return GameViewSizeMode::AspectRatio;
	}
	if (mode == "FixedResolution")
	{
		return GameViewSizeMode::FixedResolution;
	}
	if (mode == "FreeAspect")
	{
		return GameViewSizeMode::FreeAspect;
	}
	throw std::runtime_error("Unknown Game view size mode");
}
} // namespace

Result<Vector<GameViewPreset>> DeserializeGameViewPresets(const String& text, UInt32 maxTextureSize,
                                                          UInt64 maxPixelCount)
{
	try
	{
		const auto json = nlohmann::json::parse(ToStdString(text));
		if (json.at("version").get<int>() != 1 || !json.at("presets").is_array())
		{
			return Result<Vector<GameViewPreset>>::Failure("Unsupported custom Game preset settings version");
		}
		Vector<GameViewPreset> presets;
		for (const auto& item : json.at("presets"))
		{
			GameViewPreset preset;
			preset.id = ToUrsaString(item.at("id").get<std::string>());
			preset.name = ToUrsaString(item.at("name").get<std::string>());
			preset.mode = ParseMode(ToUrsaString(item.at("mode").get<std::string>()));
			preset.width = item.at("width").get<UInt32>();
			preset.height = item.at("height").get<UInt32>();
			if (preset.id.empty() || preset.name.empty() || !preset.width || !preset.height ||
			    preset.mode == GameViewSizeMode::FreeAspect || preset.width > maxTextureSize ||
			    preset.height > maxTextureSize ||
			    (preset.mode == GameViewSizeMode::FixedResolution &&
			     static_cast<UInt64>(preset.width) * preset.height > maxPixelCount))
			{
				return Result<Vector<GameViewPreset>>::Failure(
				    "Custom Game preset contains an invalid or unsupported size");
			}
			for (const GameViewPreset& existing : presets)
			{
				if (existing.id == preset.id)
				{
					return Result<Vector<GameViewPreset>>::Failure("Custom Game preset IDs must be unique");
				}
			}
			presets.push_back(std::move(preset));
		}
		return Result<Vector<GameViewPreset>>::Success(std::move(presets));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<GameViewPreset>>::Failure(String("Cannot parse custom Game presets: ") +
		                                               ToUrsaString(error.what()));
	}
}

String SerializeGameViewPresets(const Vector<GameViewPreset>& presets)
{
	nlohmann::json json;
	json["version"] = 1;
	json["presets"] = nlohmann::json::array();
	for (const GameViewPreset& preset : presets)
	{
		json["presets"].push_back({{"id", ToStdString(preset.id)},
		                           {"name", ToStdString(preset.name)},
		                           {"mode", ModeName(preset.mode)},
		                           {"width", preset.width},
		                           {"height", preset.height}});
	}
	return ToUrsaString(json.dump(2));
}

Result<GameViewSettingsState> DeserializeGameViewSettings(const String& text)
{
	try
	{
		const auto json = nlohmann::json::parse(ToStdString(text));
		if (json.at("version").get<int>() != 1)
		{
			return Result<GameViewSettingsState>::Failure("Unsupported Game view settings version");
		}
		GameViewSettingsState settings;
		settings.mode = ParseMode(ToUrsaString(json.at("mode").get<std::string>()));
		settings.width = json.at("width").get<UInt32>();
		settings.height = json.at("height").get<UInt32>();
		settings.scale = json.at("scale").get<Float32>();
		settings.fit = json.at("fit").get<bool>();
		settings.pan = {json.at("panX").get<Float32>(), json.at("panY").get<Float32>()};
		settings.presetId = ToUrsaString(json.value("preset", std::string{}));
		if (!settings.width || !settings.height || !std::isfinite(settings.scale) || settings.scale <= 0 ||
		    !std::isfinite(settings.pan.x) || !std::isfinite(settings.pan.y))
		{
			return Result<GameViewSettingsState>::Failure("Game view settings contain invalid values");
		}
		return Result<GameViewSettingsState>::Success(std::move(settings));
	}
	catch (const std::exception& error)
	{
		return Result<GameViewSettingsState>::Failure(String("Cannot parse Game view settings: ") +
		                                              ToUrsaString(error.what()));
	}
}

String SerializeGameViewSettings(const GameViewSettingsState& settings)
{
	nlohmann::json json = {{"version", 1},
	                       {"mode", ModeName(settings.mode)},
	                       {"width", settings.width},
	                       {"height", settings.height},
	                       {"scale", settings.scale},
	                       {"fit", settings.fit},
	                       {"panX", settings.pan.x},
	                       {"panY", settings.pan.y},
	                       {"preset", ToStdString(settings.presetId)}};
	return ToUrsaString(json.dump(2));
}
} // namespace Ursa::Editor
