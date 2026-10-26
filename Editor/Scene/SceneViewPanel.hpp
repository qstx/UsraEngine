#pragma once

#include <Application/PlaySession.hpp>
#include <Assets/SceneDraft.hpp>
#include <Scene/GameViewSettings.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Scene/ViewportLayout.hpp>
#include <Scene/EditorRenderViewport.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Window/Window.hpp>
#include <memory>
#include <Workspace/EditorDragSession.hpp>

namespace Ursa::Editor
{
class EditorScenePreview;
struct EditorSelectionContext;

struct SceneDropPoint
{
	Vec3 world;
	bool hitGeometry = false;
};

Result<SceneDropPoint> ResolveSceneDropPoint(const Camera& camera, Float32 aspect, const ViewportLayout& layout,
                                             ViewportPoint screenPosition,
                                             const Vector<Rendering::RenderObject>& renderObjects);

class SceneViewPanel final
{
public:
	static Result<std::unique_ptr<SceneViewPanel>> Create(Rhi::DevicePtr device, ImGuiBridge& gui);

	~SceneViewPanel();

	SceneViewPanel(const SceneViewPanel&) = delete;
	SceneViewPanel& operator=(const SceneViewPanel&) = delete;

	Result<void> Draw(SceneDraft& draft, EditorDocument& document, EditorHierarchy& hierarchy, PlaySession& play,
	                  VirtualFileSystem* userFiles, const String& projectKey, bool& showScene, bool& showGame,
	                  bool& viewportFocused, String& diagnostic,
	                  const std::function<Result<void>(const EditorDragSession&, Vec3)>& placeAssets = {},
	                  ImGuiViewport** focusedGameViewport = nullptr, EditorSelectionContext* selection = nullptr);

	void DrawEmpty(bool& showScene, bool& showGame);

	Result<void> ReloadResources();

	bool DisableEditorShortcutsInPlay() const
	{
		return disableEditorShortcutsInPlay;
	}

	bool WillFocusGameViewOnPlayStart() const
	{
		return !gameWasPlaying && (focusGameViewOnPlay || maximizeGameViewOnPlay);
	}

	Result<void> RenderPending(SceneDraft& draft, PlaySession& play, const EditorRenderViewport& renderViewport);
	Result<void> TransformSelectionToView(Scene& scene, const EditorSelectionContext& selection,
	                                      ResourceId documentId, bool playSession, bool alignRotation);

	const ViewportLayout& SceneLayout() const
	{
		return sceneLayout;
	}

private:
	enum class SceneNavigationGesture
	{
		None,
		Orbit,
		Pan,
		HandPan,
		DragZoom,
		Flythrough
	};

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
	void SetSceneCursorCaptured(bool captured);

	Result<void> EnsureTarget(ViewportTarget& target, ViewportSize size);

	void DrawGameToolbar(VirtualFileSystem* userFiles, const String& projectKey, String& diagnostic);

	void LoadSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic);

	void SaveProjectSettings(VirtualFileSystem& userFiles, const String& projectKey, String& diagnostic);

	void SaveCustomPresets(VirtualFileSystem& userFiles, String& diagnostic);

	Rhi::DevicePtr device;
	ImGuiBridge* gui = nullptr;
	ViewportTarget sceneTarget;
	ViewportTarget gameTarget;
	ViewportLayout sceneLayout;
	Rhi::SamplerPtr sampler;
	GameViewSizeMode gameSizeMode = GameViewSizeMode::FreeAspect;
	UInt32 gamePresetWidth = 16;
	UInt32 gamePresetHeight = 9;
	Float32 gameScale = 1;
	ViewportPoint gamePan;
	bool gameFit = true;
	bool gameScaleSnapPending = false;
	Float64 gameScaleSnapAt = 0;
	bool showGameStats = false;
	bool showGameGizmos = true;
	bool focusGameViewOnPlay = true;
	bool disableEditorShortcutsInPlay = false;
	bool maximizeGameViewOnPlay = false;
	bool gameWasPlaying = false;
	bool warnIfNoGameCamera = true;
	bool clearEveryFrameInEditMode = true;

	Vector<GameViewPreset> customPresets;
	String selectedCustomPreset;
	String loadedProjectKey;
	String loadedSceneViewStateKey;
	UInt64 savedSceneViewOverridesRevision = 0;
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
	bool sceneHandTool = false;
	bool sceneView2DMode = false;
	bool sceneView2DModePending = false;
	bool sceneView2DModePendingTarget = false;
	bool sceneViewRotationLocked = false;
	bool showSceneOrientationBackground = true;
	Float32 sceneFlythroughSpeed = 1.f;
	Float32 sceneFlythroughMinSpeed = 0.01f;
	Float32 sceneFlythroughMaxSpeed = 2.f;
	Float32 sceneFlythroughTargetSpeed = 0.f;
	Float32 sceneFlythroughCurrentSpeed = 0.f;
	Float32 sceneDragZoomStart = 0.f;
	Float32 sceneDragZoomSpeed = 0.3f;
	Float32 sceneDragZoomTotalMotion = 0.f;
	bool sceneZoomUseYSign = false;
	Float32 sceneCameraFieldOfView = 45.f;
	bool showSceneGrid = true;
	Float32 sceneGridSize = 1.f;
	bool showSceneGizmos = true;
	bool showSceneCameraGizmos = true;
	bool showSceneLightGizmos = true;
	bool showSceneGizmoLabels = true;
	bool sceneGizmo3DIcons = true;
	Float32 sceneGizmoIconSize = 1.f;
	Float32 sceneCameraNearClip = 0.1f;
	Float32 sceneCameraFarClip = 100.f;
	SceneNavigationGesture sceneNavigationGesture = SceneNavigationGesture::None;
	std::shared_ptr<Window> sceneNavigationWindow;
	bool sceneCursorCaptured = false;
	bool sceneSelectionPending = false;
	ViewportPoint sceneSelectionStart;
	bool sceneSelectionControl = false;
	bool sceneSelectionShift = false;
	Vector<ResourceId> scenePickCycleCandidates;
	Size scenePickCycleIndex = 0;
	ViewportPoint scenePickCyclePosition;
	Float64 scenePickCycleTime = -1.0;
	ResourceId scenePickCycleDocument;
	bool scenePickCyclePlaySession = false;
	Vector<ResourceId> scenePiercingCandidates;
	bool scenePiercingAddSelection = false;
	ResourceId sceneViewLockedTarget;
	ResourceId sceneViewLockedDocument;
	Vec3 sceneViewLockedPositionOffset{};
	Vec3 sceneViewLockedTargetOffset{};
	bool sceneViewLockedPlaySession = false;
	GizmoOperation gizmoOperation = GizmoOperation::Translate;
	GizmoPivotMode gizmoPivotMode = GizmoPivotMode::Pivot;
	GizmoSpace gizmoSpace = GizmoSpace::Local;
	GizmoSnapSettings gizmoSnapSettings;
	ResourceId sceneViewCameraId = ResourceId::Generate();
};
} // namespace Ursa::Editor
