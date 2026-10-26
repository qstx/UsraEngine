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
	bool showStats = false;
	bool showGizmos = true;
	bool focusGameViewOnPlay = true;
	bool disableEditorShortcutsInPlay = false;
	bool maximizeOnPlay = false;
	bool warnIfNoGameCamera = true;
	bool clearEveryFrameInEditMode = true;
	bool gridSnapEnabled = false;
	Float32 snapMoveX = 1.f;
	Float32 snapMoveY = 1.f;
	Float32 snapMoveZ = 1.f;
	Float32 snapRotation = 15.f;
	Float32 snapScale = 0.1f;
	Float32 sceneCameraSpeed = 10.f;
	Float32 sceneCameraSpeedMin = 0.01f;
	Float32 sceneCameraSpeedMax = 99.f;
	Float32 sceneCameraFieldOfView = 45.f;
	Float32 sceneCameraNearClip = 0.1f;
	Float32 sceneCameraFarClip = 100.f;
	bool showSceneGizmos = true;
	bool showSceneCameraGizmos = true;
	bool showSceneLightGizmos = true;
	bool showSceneGizmoLabels = true;
	bool sceneGizmo3DIcons = true;
	Float32 sceneGizmoIconSize = 1.f;
};

Result<Vector<GameViewPreset>> DeserializeGameViewPresets(const String& json, UInt32 maxTextureSize,
                                                          UInt64 maxPixelCount);

String SerializeGameViewPresets(const Vector<GameViewPreset>& presets);

Result<GameViewSettingsState> DeserializeGameViewSettings(const String& json);

String SerializeGameViewSettings(const GameViewSettingsState& settings);
} // namespace Ursa::Editor
