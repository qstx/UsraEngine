#pragma once

#include <Scene/ViewportLayout.hpp>

namespace Ursa::Editor
{
struct GameViewPreset
{
	String id;
	String name;
	GameViewSizeMode mode = GameViewSizeMode::FixedResolution;
	UInt32 width = 1280;
	UInt32 height = 720;
};

struct GameViewSettingsState
{
	GameViewSizeMode mode = GameViewSizeMode::FreeAspect;
	UInt32 width = 16;
	UInt32 height = 9;
	Float32 scale = 1;
	bool fit = true;
	ViewportPoint pan;
	String presetId;
};

Result<Vector<GameViewPreset>> DeserializeGameViewPresets(const String& json, UInt32 maxTextureSize,
                                                          UInt64 maxPixelCount);

String SerializeGameViewPresets(const Vector<GameViewPreset>& presets);

Result<GameViewSettingsState> DeserializeGameViewSettings(const String& json);

String SerializeGameViewSettings(const GameViewSettingsState& settings);
} // namespace Ursa::Editor
