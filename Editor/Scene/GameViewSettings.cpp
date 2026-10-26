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
		const int version = json.at("version").get<int>();
		if (version != 1 && version != 2)
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
		settings.showStats = json.value("showStats", false);
		settings.showGizmos = json.value("showGizmos", true);
		settings.focusGameViewOnPlay = json.value("focusGameViewOnPlay", true);
		settings.disableEditorShortcutsInPlay = json.value("disableEditorShortcutsInPlay", false);
		settings.maximizeOnPlay = json.value("maximizeOnPlay", false);
		settings.warnIfNoGameCamera = json.value("warnIfNoGameCamera", true);
		settings.clearEveryFrameInEditMode = json.value("clearEveryFrameInEditMode", true);
		settings.gridSnapEnabled = json.value("gridSnapEnabled", false);
		settings.snapMoveX = json.value("snapMoveX", 1.f);
		settings.snapMoveY = json.value("snapMoveY", 1.f);
		settings.snapMoveZ = json.value("snapMoveZ", 1.f);
		settings.snapRotation = json.value("snapRotation", 15.f);
		settings.snapScale = json.value("snapScale", 0.1f);
		settings.sceneCameraSpeed = json.value("sceneCameraSpeed", 1.f);
		settings.sceneCameraSpeedMin = json.value("sceneCameraSpeedMin", 0.01f);
		settings.sceneCameraSpeedMax = json.value("sceneCameraSpeedMax", 2.f);
		if (version == 1 && settings.sceneCameraSpeed == 10.f && settings.sceneCameraSpeedMin == 0.01f &&
		    settings.sceneCameraSpeedMax == 99.f)
		{
			settings.sceneCameraSpeed = 1.f;
			settings.sceneCameraSpeedMax = 2.f;
		}
		settings.sceneCameraFieldOfView = json.value("sceneCameraFieldOfView", 45.f);
		settings.sceneCameraNearClip = json.value("sceneCameraNearClip", 0.1f);
		settings.sceneCameraFarClip = json.value("sceneCameraFarClip", 100.f);
		settings.showSceneGrid = json.value("showSceneGrid", true);
		settings.sceneGridSize = json.value("sceneGridSize", 1.f);
		settings.showSceneGizmos = json.value("showSceneGizmos", true);
		settings.showSceneCameraGizmos = json.value("showSceneCameraGizmos", true);
		settings.showSceneLightGizmos = json.value("showSceneLightGizmos", true);
		settings.showSceneGizmoLabels = json.value("showSceneGizmoLabels", true);
		settings.sceneGizmo3DIcons = json.value("sceneGizmo3DIcons", true);
		settings.sceneGizmoIconSize = json.value("sceneGizmoIconSize", 1.f);
		if (!settings.width || !settings.height || !std::isfinite(settings.scale) || settings.scale <= 0 ||
		    !std::isfinite(settings.pan.x) || !std::isfinite(settings.pan.y) || !std::isfinite(settings.snapMoveX) ||
		    settings.snapMoveX <= 0.f || !std::isfinite(settings.snapMoveY) || settings.snapMoveY <= 0.f ||
		    !std::isfinite(settings.snapMoveZ) || settings.snapMoveZ <= 0.f || !std::isfinite(settings.snapRotation) ||
		    settings.snapRotation <= 0.f || !std::isfinite(settings.snapScale) || settings.snapScale <= 0.f ||
		    !std::isfinite(settings.sceneCameraSpeed) || !std::isfinite(settings.sceneCameraSpeedMin) ||
		    !std::isfinite(settings.sceneCameraSpeedMax) || settings.sceneCameraSpeedMin < 0.0001f ||
		    settings.sceneCameraSpeedMin > 9999.f || settings.sceneCameraSpeedMax < 0.0002f ||
		    settings.sceneCameraSpeedMax > 10000.f || settings.sceneCameraSpeedMin >= settings.sceneCameraSpeedMax ||
		    !std::isfinite(settings.sceneCameraFieldOfView) || settings.sceneCameraFieldOfView < 1.f ||
		    settings.sceneCameraFieldOfView > 179.f || !std::isfinite(settings.sceneCameraNearClip) ||
		    settings.sceneCameraNearClip < 0.01f || !std::isfinite(settings.sceneCameraFarClip) ||
		    settings.sceneCameraFarClip <= settings.sceneCameraNearClip ||
		    !std::isfinite(settings.sceneGridSize) || settings.sceneGridSize < 0.0001f ||
		    settings.sceneGridSize > 10000.f ||
		    !std::isfinite(settings.sceneGizmoIconSize) || settings.sceneGizmoIconSize < 0.1f ||
		    settings.sceneGizmoIconSize > 3.f)
		{
			return Result<GameViewSettingsState>::Failure("Game view settings contain invalid values");
		}
		settings.sceneCameraSpeed =
		    std::clamp(settings.sceneCameraSpeed, settings.sceneCameraSpeedMin, settings.sceneCameraSpeedMax);
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
	nlohmann::json json = {{"version", 2},
	                       {"mode", ModeName(settings.mode)},
	                       {"width", settings.width},
	                       {"height", settings.height},
	                       {"scale", settings.scale},
	                       {"fit", settings.fit},
	                       {"panX", settings.pan.x},
	                       {"panY", settings.pan.y},
	                       {"preset", ToStdString(settings.presetId)},
	                       {"showStats", settings.showStats},
	                       {"showGizmos", settings.showGizmos},
	                       {"focusGameViewOnPlay", settings.focusGameViewOnPlay},
	                       {"disableEditorShortcutsInPlay", settings.disableEditorShortcutsInPlay},
	                       {"maximizeOnPlay", settings.maximizeOnPlay},
	                       {"warnIfNoGameCamera", settings.warnIfNoGameCamera},
	                       {"clearEveryFrameInEditMode", settings.clearEveryFrameInEditMode},
	                       {"gridSnapEnabled", settings.gridSnapEnabled},
	                       {"snapMoveX", settings.snapMoveX},
	                       {"snapMoveY", settings.snapMoveY},
	                       {"snapMoveZ", settings.snapMoveZ},
	                       {"snapRotation", settings.snapRotation},
	                       {"snapScale", settings.snapScale},
	                       {"sceneCameraSpeed", settings.sceneCameraSpeed},
	                       {"sceneCameraSpeedMin", settings.sceneCameraSpeedMin},
	                       {"sceneCameraSpeedMax", settings.sceneCameraSpeedMax},
	                       {"sceneCameraFieldOfView", settings.sceneCameraFieldOfView},
	                       {"sceneCameraNearClip", settings.sceneCameraNearClip},
	                       {"sceneCameraFarClip", settings.sceneCameraFarClip}};
	json["showSceneGrid"] = settings.showSceneGrid;
	json["sceneGridSize"] = settings.sceneGridSize;
	json["showSceneGizmos"] = settings.showSceneGizmos;
	json["showSceneCameraGizmos"] = settings.showSceneCameraGizmos;
	json["showSceneLightGizmos"] = settings.showSceneLightGizmos;
	json["showSceneGizmoLabels"] = settings.showSceneGizmoLabels;
	json["sceneGizmo3DIcons"] = settings.sceneGizmo3DIcons;
	json["sceneGizmoIconSize"] = settings.sceneGizmoIconSize;
	return ToUrsaString(json.dump(2));
}
} // namespace Ursa::Editor
