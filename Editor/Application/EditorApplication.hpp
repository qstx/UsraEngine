#pragma once
#include <Scripting/ExternalScriptEditor.hpp>

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <Rhi/RHI.hpp>
#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Window/Window.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <Workspace/ProjectTagLayerSettings.hpp>
#include <Assets/ProjectBrowserModel.hpp>
#include <Assets/ProjectBrowserPanel.hpp>
#include <Inspector/AssetInspectorPanel.hpp>
#include <Console/EditorConsolePanel.hpp>
#include <Assets/AssetPreviewPanel.hpp>
#include <Assets/MaterialInspectors.hpp>
#include <Assets/ShaderImportView.hpp>
#include <Assets/TextureImportView.hpp>
#include <Assets/ModelImportView.hpp>
#include <Assets/ScriptBuildService.hpp>
#include <Assets/AssetImportPipeline.hpp>
#include <Scripting/ManagedEditorBridge.hpp>
#include <Scene/EditorHierarchy.hpp>
#include <Scene/SceneViewPanel.hpp>
#include <Scene/ComponentEditors.hpp>
#include <Tools/EditorTools.hpp>
#include <Application/PlaySession.hpp>
#include <functional>
#include <future>
#include <mutex>
#include <chrono>

namespace Ursa::Scripting
{
class DotNetHost;
}

namespace Ursa
{
class PrefabAsset;
}

namespace Ursa::Editor
{
struct EditorProject
{
	ComponentRegistry components;
	ComponentEditors componentEditors;
	MaterialInspectors materialInspectors;
	EditorTools tools;
	std::function<Result<void>(ComponentLoadContext&)> editContextSetup;
	PlayContextSetup playContextSetup;
};

using EditorProjectSetup = std::function<Result<void>(EditorProject&)>;

class EditorApplication
{
public:
	explicit EditorApplication(EditorProjectSetup setup = {}) : projectSetup(std::move(setup))
	{
	}

	virtual ~EditorApplication();

	Result<void> Run(Ursa::UInt32 width = 1280, Ursa::UInt32 height = 720, const String& title = "Ursa Editor",
	                 bool visible = true, Ursa::UInt32 frameLimit = 0);

	Result<void> OpenProject(const std::filesystem::path& root, const std::filesystem::path& package = {});

	Result<void> SetWindowIcon(const std::filesystem::path& path);

	Result<void> SetFrameCapture(std::filesystem::path output, UInt32 frame = 1)
	{
		return frameCapture.Configure(std::move(output), frame);
	}

	void SetStartupAsset(VirtualPath path)
	{
		startupAsset = std::move(path);
	}

	void SetStartupWindow(String name)
	{
		startupWindow = std::move(name);
	}

	void SetStartupPlay(bool enabled)
	{
		startupPlay = enabled;
	}

protected:
	virtual Result<void> OnStart();

	virtual Result<void> OnFrame(Ursa::Float32 deltaSeconds)
	{
		(void)deltaSeconds;
		return {};
	}

	virtual Result<void> OnGui();

	Result<void> RegisterScriptComponentTypes(const Vector<ManagedExtensionDescriptor>* extensions = nullptr,
	                                          UInt64 sessionId = 0);

	Result<void> EnsureManagedHost();
	Result<bool> DrawAssetExtension(ManagedEditorContext& context);
	void ConfigureAssetPreview(ManagedEditorContext& context);
	void ConfigureReferencePreviewFactory(ManagedEditorContext& context);
	bool BeforeImporterSelection(const EditorSelectionContext& next);
	void DrawImporterSelectionPrompt();

	Result<void> ReloadEditorScripts();

	Result<void> OpenEditorAsset(const VirtualPath& path);

	Result<void> CompileScripts();

	Result<void> OpenCSharpProject(const VirtualPath& source = {}, UInt32 line = 1, UInt32 column = 1,
	                               bool openEditor = true);

	String ScriptDebugInfo() const;

	Result<void> OpenScriptDiagnostic(Size index);

	const Vector<ScriptDiagnostic>& ScriptDiagnostics() const
	{
		return scriptDiagnostics;
	}

	const String& ScriptIdeStatus() const
	{
		return scriptIdeStatus;
	}

	bool ScriptWorkPending() const
	{
		return scriptBuildJob.valid() || scriptCheckJob.valid() || scriptIdeJob.valid() || ideGenerationRequested ||
		       (assetImportJob.valid() && !play.Active());
	}

	bool ScriptCompilationPending() const
	{
		return scriptBuildJob.valid();
	}

	bool EditingRenderReady() const
	{
		return editingScriptSession != 0 && editingScriptServices != nullptr;
	}

	const String& ScriptStatus() const
	{
		return scriptBuildStatus;
	}

	const String& InspectorStatus() const
	{
		return inspectorError;
	}

	void RequestPlayToggle()
	{
		requestRun = true;
	}

	Scene* RunningScene()
	{
		return play.GetScene();
	}

	bool PlayPaused() const
	{
		return play.Paused();
	}

	const EditorSelectionContext& SelectionState() const
	{
		return selectionContext;
	}

	void FinishScriptJobs();

	void DrawManagedMenus(const String& root);

	Result<bool> InvokeEditorTool(const ManagedExtensionDescriptor& descriptor);

	virtual void OnStop()
	{
	}

	std::shared_ptr<Window> window;
	Rhi::DevicePtr device;
	Rhi::SwapchainPtr swapchain;

	ImGuiBridge& Gui()
	{
		return *gui;
	}

	EditorProject& Project()
	{
		return *project;
	}

	ProjectWorkspace* Workspace() const
	{
		return workspace.get();
	}

	EditorDocuments* Documents() const
	{
		return documents.get();
	}

	AssetPreviewPanel* InspectorPreview() const
	{
		return assetPreview.get();
	}

	void RefreshAssets(const VirtualPath& scope = {}, bool force = false)
	{
		RequestAssetImport(scope, force);
	}

	bool AssetWorkPending() const
	{
		return assetImportRequested || assetImportJob.valid();
	}

	const String& AssetStatus() const
	{
		return assetImportStatus;
	}

private:
	friend struct EditorPlayToolbarTest;

	String MainWindowTitle() const;

	Result<void> DecodeWindowIcon(const UInt8* encoded, Size size);

	String editorProductName = "Ursa Editor";
	Vector<UInt8> windowIconPixels;
	UInt32 windowIconWidth = 0;
	UInt32 windowIconHeight = 0;

	Rendering::FrameCapture frameCapture;

	void FinishAssetJobs();

	Result<void> PresentGuiFrame(bool capture = true);

	void RequestAssetImport(const VirtualPath& scope = {}, bool force = false);
	void RequestAssetReimport(const Vector<VirtualPath>& sources);

	std::future<Result<AssetImportReport>> assetImportJob;
	std::future<Result<String>> assetScanJob;
	String observedAssetSources;
	String assetImportStatus;
	VirtualPath requestedImportScope;
	Vector<VirtualPath> requestedImportSources;
	bool assetImportRequested = false;
	bool forceAssetImport = false;
	bool autoRefreshAssets = true;

	struct ImportProgress
	{
		std::mutex mutex;
		UInt64 done = 0;
		UInt64 total = 0;
		String path;
	};

	std::shared_ptr<ImportProgress> importProgress = std::make_shared<ImportProgress>();
	std::chrono::steady_clock::time_point assetRequestDeadline{};
	UInt64 assetFocusSubscription = 0;
	std::chrono::steady_clock::time_point nextAssetScan{};

	void DrawMainMenuBar();
	bool CanCopyGameObjectSelection() const;
	bool CanPasteGameObjectSelection() const;
	bool CanPasteGameObjectSelectionAsChild() const;
	Result<void> CopyGameObjectSelection();
	Result<void> CutGameObjectSelection();
	Result<void> PasteGameObjectSelection(bool asChild);
	void ResetEditorLayout();

	void PumpScriptIde();

	void DrawExternalTools();

	void SetScriptEditorInput(const ScriptEditorConfig& config);

	void DrawGraphicsSettings();
	void DrawTagLayerSettings();

	void DrawPlayToolbar(const EditorDocument* current);

	Result<void> OpenProjectBrowserAsset(const ProjectBrowserEntry& asset);

	Result<void> CreateProjectAsset(ResourceKind kind, MaterialType materialType = MaterialType::Unlit,
	                                const String& requestedPath = {});

	Result<void> CreateProjectScriptableAsset(const ScriptTypeDescriptor& type, const String& requestedName);
	Result<void> ExportScenePrefabs(const EditorDragSession& session, const String& destinationFolder);

	Result<void> ApplyGraphicsSettings(AssetReference pipeline);

	Result<void> RenderManagedViewport(Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                                   std::optional<Rendering::CameraType> cameraFilter,
	                                   std::optional<Rendering::CameraData> cameraOverride = {},
	                                   Vector<ResourceId> excludedObjects = {});

	Result<void> CreateProjectFolder(const String& requestedName = {});

	Result<void> RenameProjectAsset(const VirtualPath& source, const VirtualPath& destination);

	Result<void> LoadPanelVisibility();

	Result<void> SavePanelVisibility();

	EditorProjectSetup projectSetup;
	VirtualPath startupAsset;
	String startupWindow;
	bool startupPlay = false;
	std::unique_ptr<EditorProject> project;
	String activeEditorTool;
	std::unique_ptr<ImGuiBridge> gui;
	std::unique_ptr<AssetPreviewPanel> assetPreview;
	std::unique_ptr<ShaderImportView> shaderImportView;
	std::unique_ptr<TextureImportView> textureImportView;
	std::unique_ptr<ModelImportView> modelImportView;
	EditorHierarchy hierarchy;
	ResourceId editSelectionBeforePlay;
	EditorSelectionContext selectionBeforePlay;
	ResourceId displayedSceneId;
	std::unique_ptr<SceneViewPanel> sceneViewPanel;
	PlaySession play;
	bool viewportFocused = false;
	ImGuiViewport* focusedGameViewport = nullptr;
	std::unique_ptr<ProjectWorkspace> workspace;
	std::unique_ptr<EditorDocuments> documents;
	ProjectBrowserModel projectBrowser;
	ProjectBrowserPanel projectBrowserPanel;
	AssetInspectorPanel assetInspectorPanel;
	std::shared_ptr<AssetImporterInspector> assetImporterInspector;
	std::optional<EditorSelectionContext> pendingImporterSelection;
	std::optional<EditorSelectionContext> pendingPickerSelection;
	bool closeDocumentAfterPickerCancellation = false;
	bool importerSelectionPopup = false;
	bool pendingSourceAsset = false;
	VirtualPath pendingImporterOpen;
	EditorSelectionContext selectionContext;
	std::shared_ptr<Ursa::PrefabAsset> gameObjectClipboardSnapshot;
	ResourceId gameObjectClipboardPrimary;
	Vector<Mat4> gameObjectClipboardWorldTransforms;
	EditorConsolePanel consolePanel;
	std::unique_ptr<AssetDatabase> scriptAssetDatabase;
	std::unique_ptr<ScriptBuildService> scriptBuildService;
	std::shared_ptr<Ursa::Scripting::DotNetHost> managedHost;
	std::future<Result<ScriptBuildReport>> scriptBuildJob;
	std::future<Result<String>> scriptCheckJob;
	std::future<Result<ScriptIdeWorkspace>> scriptIdeJob;
	std::future<Result<void>> scriptEditorJob;
	std::future<Vector<ScriptEditorConfig>> scriptEditorDiscoveryJob;
	Vector<ScriptEditorConfig> discoveredScriptEditors;
	ScriptEditorConfig scriptEditorConfig;
	std::filesystem::path scriptEditorPreferencesDirectory;
	bool scriptEditorPreferencesLoaded = false;
	bool scriptEditorDiscoveryRequested = false;
	bool showExternalTools = false;
	bool ideGenerationRequested = false;
	bool ideOpenRequested = false;
	VirtualPath ideSource;
	UInt32 ideSourceLine = 1;
	UInt32 ideSourceColumn = 1;
	String scriptIdeStatus;
	Array<char, 2048> scriptEditorExecutableInput{};
	Array<char, 4096> scriptEditorProjectArgumentsInput{};
	Array<char, 4096> scriptEditorSourceArgumentsInput{};
	String observedScriptSources;
	std::chrono::steady_clock::time_point nextScriptCheck;
	String scriptBuildStatus;
	Vector<ScriptDiagnostic> scriptDiagnostics;
	UInt64 editingScriptSession = 0;
	std::shared_ptr<void> editingScriptServices;
	Vector<ManagedExtensionDescriptor> managedExtensions;
	Map<String, ComponentEditor> managedEditorFallbacks;
	Set<String> managedOpenWindows;
	VirtualPath pendingManagedAsset;
	ResourceId pendingRunDocument;
	bool runAfterScriptBuild = false;
	bool requestRun = false;
	Set<String> registeredScriptComponentTypes;
	Ursa::Array<char, 256> projectSearch{};
	Ursa::Array<char, 128> folderNameInput{};
	String workspaceError;
	String inspectorError;
	bool exitRequested = false;
	bool exitAfterInspectorPrompt = false;
	bool dockLayoutInitialized = false;
	bool showProjectPanel = true;
	bool showHierarchyPanel = true;
	bool showInspectorPanel = true;
	bool showScenePanel = true;
	bool showGamePanel = true;
	bool showConsolePanel = true;
	bool showGraphicsSettings = false;
	String graphicsSettingsProject;
	AssetReference graphicsSettingsCandidate;
	String graphicsSettingsDiagnostic;
	bool showTagLayerSettings = false;
	int requestedTagLayerSettingsTab = -1;
	String tagLayerSettingsProject;
	ProjectTagLayerSettings tagLayerSettings = ProjectTagLayerSettings::Defaults();
	String tagLayerSettingsDiagnostic;
	Ursa::Array<char, 128> tagInput{};
	Ursa::Array<char, 128> tagEditInput{};
	Ursa::Array<Ursa::Array<char, 65>, 32> layerEditInputs{};
	int selectedTagIndex = 0;
	String savedPanelVisibility;
};
} // namespace Ursa::Editor
