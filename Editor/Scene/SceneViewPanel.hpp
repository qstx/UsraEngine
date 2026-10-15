#pragma once

#include <Application/PlaySession.hpp>
#include <Assets/SceneDraft.hpp>
#include <Scene/GameViewSettings.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Scene/ViewportLayout.hpp>
#include <Scene/EditorRenderViewport.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <memory>

namespace Ursa::Editor
{
class EditorScenePreview;

class SceneViewPanel final
{
public:
	static Result<std::unique_ptr<SceneViewPanel>> Create(Rhi::DevicePtr device, ImGuiBridge& gui);

	~SceneViewPanel();

	SceneViewPanel(const SceneViewPanel&) = delete;
	SceneViewPanel& operator=(const SceneViewPanel&) = delete;

	Result<void> Draw(SceneDraft& draft, EditorDocument& document, EditorHierarchy& hierarchy, PlaySession& play,
	                  VirtualFileSystem* userFiles, const String& projectKey, bool& showScene, bool& showGame,
	                  bool& viewportFocused, String& diagnostic);

	void DrawEmpty(bool& showScene, bool& showGame);

	Result<void> ReloadResources();

	Result<void> RenderPending(SceneDraft& draft, PlaySession& play, const EditorRenderViewport& renderViewport);

private:
	struct ViewportTarget
	{
		std::unique_ptr<EditorScenePreview> preview;
		std::shared_ptr<Rendering::RenderTextureResource> output;
		Rhi::TexturePtr color;
		ImTextureID imageId = ImTextureID_Invalid;
		ViewportSize size;
		bool visible = false;
	};

	SceneViewPanel(Rhi::DevicePtr selectedDevice, ImGuiBridge& selectedGui)
	    : device(std::move(selectedDevice)), gui(&selectedGui)
	{
	}

	Result<void> Initialize();

	Result<void> EnsureTarget(ViewportTarget& target, ViewportSize size);

	void DrawGameToolbar(VirtualFileSystem* userFiles, const String& projectKey, String& diagnostic);

	void LoadSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic);

	void SaveProjectSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic);

	void SaveCustomPresets(VirtualFileSystem& userFiles, String& diagnostic);

	Rhi::DevicePtr device;
	ImGuiBridge* gui = nullptr;
	ViewportTarget sceneTarget;
	ViewportTarget gameTarget;
	Rhi::SamplerPtr sampler;
	GameViewSizeMode gameSizeMode = GameViewSizeMode::FreeAspect;
	UInt32 gamePresetWidth = 16;
	UInt32 gamePresetHeight = 9;
	Float32 gameScale = 1;
	ViewportPoint gamePan;
	bool gameFit = true;

	Vector<GameViewPreset> customPresets;
	String selectedCustomPreset;
	String loadedProjectKey;
	bool globalSettingsLoaded = false;
	bool projectSettingsLoaded = false;
	bool settingsChanged = false;
	bool presetsChanged = false;
	bool editCustomPreset = false;
	String customPresetEditingId;
	Array<char, 128> customPresetName{};
	Int32 customPresetWidth = 1280;
	Int32 customPresetHeight = 720;
	Int32 customPresetMode = 0;
	bool gizmoWasOver = false;
	bool gizmoWasUsing = false;
	GizmoOperation gizmoOperation = GizmoOperation::Translate;
	ResourceId sceneViewCameraId = ResourceId::Generate();
};
} // namespace Ursa::Editor
