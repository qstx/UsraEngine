#pragma once

#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Core/Result.hpp>
#include <Rhi/RHI.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <Window/Window.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <Workspace/EditorSelectionContext.hpp>
#include <Assets/ProjectBrowserModel.hpp>
#include <Assets/ProjectBrowserPanel.hpp>
#include <Inspector/AssetInspectorPanel.hpp>
#include <Console/EditorConsolePanel.hpp>
#include <Assets/MaterialPreview.hpp>
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

	void SetStartupAsset(VirtualPath path)
	{
		startupAsset = std::move(path);
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

	Result<void> ReloadEditorScripts();

	Result<void> OpenEditorAsset(const VirtualPath& path);

	Result<void> CompileScripts();

	bool ScriptWorkPending() const
	{
		return scriptBuildJob.valid() || scriptCheckJob.valid() || (assetImportJob.valid() && !play.Active());
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
	void FinishAssetJobs();

	Result<void> PresentGuiFrame();

	void RequestAssetImport(const VirtualPath& scope = {}, bool force = false);

	std::future<Result<AssetImportReport>> assetImportJob;
	std::future<Result<String>> assetScanJob;
	String observedAssetSources;
	String assetImportStatus;
	VirtualPath requestedImportScope;
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

	void DrawPlayToolbar(const EditorDocument* current);

	Result<void> OpenProjectBrowserAsset(const ProjectBrowserEntry& asset);

	Result<void> CreateProjectAsset(ResourceKind kind, MaterialType materialType = MaterialType::Unlit,
	                                const String& requestedPath = {});

	Result<void> CreateProjectFolder(const String& requestedName = {});

	Result<void> RenameProjectAsset(const VirtualPath& source, const VirtualPath& destination);

	Result<void> LoadPanelVisibility();

	Result<void> SavePanelVisibility();

	EditorProjectSetup projectSetup;
	VirtualPath startupAsset;
	std::unique_ptr<EditorProject> project;
	String activeEditorTool;
	std::unique_ptr<ImGuiBridge> gui;
	std::unique_ptr<MaterialPreview> materialPreview;
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
	std::unique_ptr<ProjectWorkspace> workspace;
	std::unique_ptr<EditorDocuments> documents;
	ProjectBrowserModel projectBrowser;
	ProjectBrowserPanel projectBrowserPanel;
	AssetInspectorPanel assetInspectorPanel;
	EditorSelectionContext selectionContext;
	EditorConsolePanel consolePanel;
	std::unique_ptr<AssetDatabase> scriptAssetDatabase;
	std::unique_ptr<ScriptBuildService> scriptBuildService;
	std::shared_ptr<Ursa::Scripting::DotNetHost> managedHost;
	std::future<Result<ScriptBuildReport>> scriptBuildJob;
	std::future<Result<String>> scriptCheckJob;
	String observedScriptSources;
	std::chrono::steady_clock::time_point nextScriptCheck;
	String scriptBuildStatus;
	UInt64 editingScriptSession = 0;
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
	bool dockLayoutInitialized = false;
	bool showProjectPanel = true;
	bool showHierarchyPanel = true;
	bool showInspectorPanel = true;
	bool showScenePanel = true;
	bool showGamePanel = true;
	bool showConsolePanel = true;
	String savedPanelVisibility;
};
} // namespace Ursa::Editor
