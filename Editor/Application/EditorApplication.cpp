#include <Core/Types.hpp>
#include <Application/EditorApplication.hpp>
#include <Assets/AssetImportPipeline.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Assets/TextureDraft.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/SceneDraft.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/DotNet/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#endif
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Core/Profiler.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/PakArchive.hpp>
#include <imgui_internal.h>
#include <cctype>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace Ursa::Editor
{
namespace
{
#if defined(URSA_ENABLE_DOTNET)
struct ManagedPlaySessionState
{
	std::shared_ptr<Scripting::DotNetHost> host;
	std::shared_ptr<void> projectServices;
	std::unique_ptr<Scripting::ManagedSceneApi> sceneApi;
	std::unique_ptr<Scripting::ManagedScriptRuntime> scriptRuntime;
	UInt64 sessionId = 0;

	~ManagedPlaySessionState()
	{
		scriptRuntime.reset();
		sceneApi.reset();
		if (host && sessionId)
		{
			auto closed = host->CloseScriptSession(sessionId);
			if (!closed)
			{
				URSA_LOG_ERROR("Script", closed.error);
			}
		}
	}
};
#endif

Result<VirtualPath> ProjectAssetPath(const char* input)
{
	const String name = ToUrsaString(std::string(input));
	if (name.empty())
	{
		return Result<VirtualPath>::Failure("Enter an asset path such as Snake/Level.ursascene");
	}
	return VirtualPath::Parse(String(projectResourceScheme) + ":" + name);
}

String ProjectSettingsKey(const std::filesystem::path& projectRoot)
{
	const std::string normalized = ToStdString(PathToUrsaString(projectRoot.lexically_normal()));
	UInt64 hash = 14695981039346656037ull;
	for (unsigned char value : normalized)
	{
		hash ^= static_cast<UInt64>(std::tolower(value));
		hash *= 1099511628211ull;
	}
	char text[17]{};
	std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
	return ToUrsaString(text);
}

String AssetDisplayPath(const VirtualPath& path)
{
	return path.scheme == projectResourceScheme ? String("Assets/") + path.relative : path.ToString();
}

bool IsTextureSource(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return false;
	}
	String extension = path.relative.substr(dot);
	for (char& character : extension)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character - 'A' + 'a');
		}
	}
	return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

bool IsModelSource(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return false;
	}
	String extension = path.relative.substr(dot);
	for (char& character : extension)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character - 'A' + 'a');
		}
	}
	return extension == ".fbx" || extension == ".obj";
}

bool HasManagedScriptComponent(Scene& scene)
{
	Vector<GameObject::Ptr> pending = scene.RootGameObjects();
	while (!pending.empty())
	{
		GameObject::Ptr object = std::move(pending.back());
		pending.pop_back();
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
		for (ComponentId id : scene.ComponentsOf(object).value)
		{
			if (dynamic_cast<Scripting::ScriptBehaviourComponent*>(scene.GetComponent(id)))
			{
				return true;
			}
		}
	}
	return false;
}
} // namespace

EditorApplication::~EditorApplication()
{
	if (assetImportJob.valid())
	{
		assetImportJob.wait();
	}
	if (assetScanJob.valid())
	{
		assetScanJob.wait();
	}
	FinishScriptJobs();
	play.Stop();
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost && editingScriptSession)
	{
		auto closed = managedHost->CloseScriptSession(editingScriptSession);
		if (!closed)
		{
			URSA_LOG_ERROR("Script", closed.error);
		}
	}
#endif
}

void EditorApplication::FinishScriptJobs()
{
	if (scriptCheckJob.valid())
	{
		scriptCheckJob.wait();
		scriptCheckJob = {};
	}
	if (scriptBuildJob.valid())
	{
		scriptBuildJob.wait();
		scriptBuildJob = {};
	}
}

Result<void> EditorApplication::EnsureManagedHost()
{
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost)
	{
		return managedHost->IsSessionFaulted() ? Result<void>::Failure("脚本环境卸载失败，请重启编辑器")
		                                       : Result<void>{};
	}
	if (!workspace)
	{
		return Result<void>::Failure("Open a project before initializing scripts");
	}
	auto host = Scripting::DotNetHost::Create();
	if (!host)
	{
		return Result<void>::Failure(host.error);
	}
	const auto path = [](const char* value) { return VirtualPath::Parse(value).value; };
	Scripting::ManagedHostFiles files;
	files.runtimeConfig = path("ursa:Ursa.ManagedHost.runtimeconfig.json");
	files.entryAssembly = path("ursa:Ursa.ManagedHost.dll");
	files.dependencies = {path("ursa:Ursa.ManagedHost.deps.json"), path("ursa:Ursa.Runtime.dll"),
	                      path("ursa:Ursa.Editor.dll")};
	auto started = host.value->Start(workspace->ContentFiles(), files,
	                                 [](UInt32 level, const String& message)
	                                 {
		                                 if (level >= static_cast<UInt32>(LogLevel::Error))
		                                 {
			                                 URSA_LOG_ERROR("Script", message);
		                                 }
		                                 else
		                                 {
			                                 URSA_LOG_INFO("Script", message);
		                                 }
	                                 });
	if (!started)
	{
		return started;
	}
	managedHost = std::shared_ptr<Scripting::DotNetHost>(std::move(host.value));
	return managedHost->LoadEditorApi(path("ursa:Ursa.Editor.dll"));
#else
	return Result<void>::Failure("This build does not enable .NET scripting");
#endif
}

Result<void> EditorApplication::ReloadEditorScripts()
{
#if defined(URSA_ENABLE_DOTNET)
	if (play.Active())
	{
		return Result<void>::Failure("Stop before reloading editing scripts");
	}
	auto ready = EnsureManagedHost();
	if (!ready)
	{
		return ready;
	}
	auto candidate = managedHost->CreateScriptSession(true);
	if (!candidate)
	{
		return Result<void>::Failure(candidate.error);
	}
	const auto abandon = [&]() { return managedHost->CloseScriptSession(candidate.value); };
	auto content = managedHost->LoadScriptContent(candidate.value,
	                                              VirtualPath::Parse("lib:ScriptBuild/Manifests/Editor.json").value);
	if (!content)
	{
		abandon();
		return content;
	}
	Vector<GameObject::Ptr> objects;
	if (documents && documents->Current())
	{
		auto draft = documents->Current()->Payload<SceneDraft>();
		if (draft)
		{
			if (draft->Kind() == ResourceKind::Prefab && draft->GetPrefabAsset())
			{
				objects = {draft->GetPrefabAsset()->Root()};
			}
			else
			{
				objects = draft->GetScene().RootGameObjects();
			}
		}
	}
	nlohmann::json snapshots = nlohmann::json::array();
	Map<String, Scripting::ScriptBehaviourComponent*> components;
	while (!objects.empty())
	{
		auto object = std::move(objects.back());
		objects.pop_back();
		for (const auto& child : object->Children())
		{
			objects.push_back(child);
		}
		for (Component* component : object->Components())
		{
			auto* script = dynamic_cast<Scripting::ScriptBehaviourComponent*>(component);
			if (!script)
			{
				continue;
			}
			if (!script->HasSerializedPayload())
			{
				abandon();
				return Result<void>::Failure("Script component has no persistent payload");
			}
			try
			{
				const String id = component->PersistentId().ToString();
				snapshots.push_back({{"Id", ToStdString(id)},
				                     {"Payload", nlohmann::json::parse(ToStdString(script->SerializedPayload()))}});
				components[id] = script;
			}
			catch (const std::exception& error)
			{
				abandon();
				return Result<void>::Failure(ToUrsaString(error.what()));
			}
		}
	}
	auto restored = managedHost->PrepareScriptReload(candidate.value, ToUrsaString(snapshots.dump()));
	if (!restored)
	{
		abandon();
		return Result<void>::Failure(restored.error);
	}

	struct CandidateFields
	{
		Scripting::ScriptBehaviourComponent* component;
		String type;
		String payload;
	};

	Vector<CandidateFields> replacements;
	try
	{
		for (const auto& item : nlohmann::json::parse(ToStdString(restored.value)))
		{
			const String id = ToUrsaString(item.at("Id").get<std::string>());
			auto found = components.find(id);
			if (found == components.end())
			{
				throw std::runtime_error("Candidate snapshot has an unknown component ID");
			}
			replacements.push_back({found->second, ToUrsaString(item.at("TypeName").get<std::string>()),
			                        ToUrsaString(item.at("Payload").dump())});
		}
	}
	catch (const std::exception& error)
	{
		abandon();
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
	auto extensionDirectory = managedHost->PrepareEditorExtensions(candidate.value);
	if (!extensionDirectory)
	{
		abandon();
		return Result<void>::Failure(extensionDirectory.error);
	}
	Vector<ManagedExtensionDescriptor> candidateExtensions;
	try
	{
		for (const auto& extension : nlohmann::json::parse(ToStdString(extensionDirectory.value)))
		{
			candidateExtensions.push_back({ToUrsaString(extension.at("Id").get<std::string>()),
			                               ToUrsaString(extension.at("Kind").get<std::string>()),
			                               ToUrsaString(extension.at("Path").get<std::string>()),
			                               ToUrsaString(extension.at("Target").get<std::string>()),
			                               ToUrsaString(extension.at("Title").get<std::string>())});
		}
	}
	catch (const std::exception& error)
	{
		abandon();
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
	auto registered = RegisterScriptComponentTypes(&candidateExtensions, candidate.value);
	if (!registered)
	{
		abandon();
		return registered;
	}
	const UInt64 previous = editingScriptSession;
	editingScriptSession = candidate.value;
	managedExtensions = std::move(candidateExtensions);
	for (auto& fields : replacements)
	{
		fields.component->SetTypeName(std::move(fields.type));
		fields.component->SetSerializedPayload(std::move(fields.payload));
	}
	if (previous)
	{
		auto closed = managedHost->CloseScriptSession(previous);
		if (!closed)
		{
			return closed;
		}
	}
	return managedHost->SelectScriptSession(editingScriptSession);
#else
	return {};
#endif
}

Result<void> EditorApplication::OpenEditorAsset(const VirtualPath& path)
{
	if (!workspace || !documents || !project || play.Active())
	{
		return Result<void>::Failure("Asset opening requires Stop mode and an open project");
	}
	auto kind = ResourceKindForPath(path);
	Result<std::unique_ptr<EditorDocument>> opened =
	    Result<std::unique_ptr<EditorDocument>>::Failure("This asset has no editing document");
	if (kind && (kind.value == ResourceKind::Scene || kind.value == ResourceKind::Prefab))
	{
		opened =
		    OpenSceneDocument(path, kind.value, project->components, documents->Session(), project->editContextSetup);
	}
	else if (kind && kind.value == ResourceKind::Texture)
	{
		opened = OpenTextureDocument(path, workspace->Index());
	}
	else if (kind && kind.value == ResourceKind::Material)
	{
		opened = OpenMaterialDocument(path, documents->Session(), *workspace);
	}
	else if (IsTextureSource(path) && workspace->Database() && gui)
	{
		auto settings = TextureImporter::Settings(*workspace->Database(), path);
		if (!settings)
		{
			return Result<void>::Failure(settings.error);
		}
		auto view = TextureImportView::Open(path, settings.value);
		if (!view)
		{
			return Result<void>::Failure(view.error);
		}
		if (textureImportView)
		{
			textureImportView->Close(*gui);
		}
		textureImportView = std::move(view.value);
		auto preview = textureImportView->Reimport(*workspace->Database(), device, *gui, true);
		if (!preview)
		{
			RequestAssetImport(path);
		}
		return preview;
	}
	else if (IsModelSource(path) && workspace->Database())
	{
		auto settings = ModelImporter::Settings(*workspace->Database(), path);
		if (!settings)
		{
			return Result<void>::Failure(settings.error);
		}
		auto view = ModelImportView::Open(path, settings.value);
		if (!view)
		{
			return Result<void>::Failure(view.error);
		}
		modelImportView = std::move(view.value);
		auto preview = modelImportView->Reimport(*workspace->Database(), true);
		if (!preview)
		{
			RequestAssetImport(path);
		}
		return preview;
	}
	else if (path.relative.size() >= 11 && path.relative.substr(path.relative.size() - 11) == ".ursashader")
	{
		auto view = ShaderImportView::Open(workspace->Index().Filesystem(), path);
		if (!view)
		{
			return Result<void>::Failure(view.error);
		}
		shaderImportView = std::move(view.value);
		return {};
	}
	if (!opened)
	{
		return Result<void>::Failure(opened.error);
	}
	return documents->RequestOpen(std::move(opened.value));
}

Result<bool> EditorApplication::InvokeEditorTool(const ManagedExtensionDescriptor& descriptor)
{
#if defined(URSA_ENABLE_DOTNET)
	if (!managedHost || !editingScriptSession)
	{
		return Result<bool>::Failure("No editing script session is available");
	}
	ManagedEditorContext context;
	context.document = documents ? documents->Current() : nullptr;
	context.documents = documents.get();
	context.workspace = workspace.get();
	context.registry = &project->components;
	context.openAsset = [this](const VirtualPath& path)
	{
		pendingManagedAsset = path;
		return Result<void>{};
	};
	if (context.document)
	{
		auto draft = context.document->Payload<SceneDraft>();
		if (draft)
		{
			if (draft->Kind() == ResourceKind::Prefab && draft->GetPrefabAsset())
			{
				context.selection.prefab = draft->GetPrefabAsset();
				context.selection.roots = {draft->GetPrefabAsset()->Root()};
				auto selected = hierarchy.SelectedGameObject(*draft->GetPrefabAsset());
				if (selected)
				{
					context.selection.object = selected.value;
				}
			}
			else
			{
				context.selection.scene = &draft->GetScene();
				context.selection.roots = draft->GetScene().RootGameObjects();
				auto selected = hierarchy.SelectedGameObject(draft->GetScene());
				if (selected)
				{
					context.selection.object = selected.value;
				}
			}
		}
	}
	return InvokeManagedEditorExtension(*managedHost, editingScriptSession, descriptor.id, context);
#else
	(void)descriptor;
	return Result<bool>::Failure("This build does not enable .NET scripting");
#endif
}

void EditorApplication::DrawManagedMenus(const String& root)
{
	Set<String> submenus;
	const String prefix = root + "/";
	for (const auto& extension : managedExtensions)
	{
		if ((extension.kind != "Menu" && extension.kind != "Window") || extension.path.rfind(prefix, 0) != 0)
		{
			continue;
		}
		const String remainder = extension.path.substr(prefix.size());
		const Size separator = remainder.find('/');
		if (separator != String::npos)
		{
			submenus.insert(remainder.substr(0, separator));
			continue;
		}
		if (ImGui::MenuItem(remainder.c_str()))
		{
			if (extension.kind == "Window")
			{
				managedOpenWindows.insert(extension.id);
			}
			else
			{
				auto executed = InvokeEditorTool(extension);
				if (!executed)
				{
					inspectorError = executed.error;
				}
			}
		}
	}
	for (const String& submenu : submenus)
	{
		if (ImGui::BeginMenu(submenu.c_str()))
		{
			DrawManagedMenus(prefix + submenu);
			ImGui::EndMenu();
		}
	}
}

Result<void> EditorApplication::OnStart()
{
	return startupAsset.scheme.empty() ? Result<void>{} : OpenEditorAsset(startupAsset);
}

Result<void> EditorApplication::OpenProject(const std::filesystem::path& root, const std::filesystem::path& package)
{
	if (assetImportJob.valid() || assetScanJob.valid())
	{
		return Result<void>::Failure("Wait for asset import/discovery before changing projects");
	}

	if (scriptCheckJob.valid() && scriptCheckJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		return Result<void>::Failure("Wait for script discovery to finish before changing projects");
	}
	if (scriptBuildJob.valid() && scriptBuildJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		return Result<void>::Failure("Wait for script compilation to finish before changing projects");
	}
	if (play.Active())
	{
		return Result<void>::Failure("Stop Play mode before changing projects");
	}
	if (documents && documents->Current() && documents->Current()->Dirty())
	{
		return Result<void>::Failure("Close or save the dirty document before changing projects");
	}

	RuntimeMountProfileConfig profile;
	profile.mode = package.empty() ? RuntimeMountMode::Editor : RuntimeMountMode::PackagedRuntime;
	profile.applicationId = PathToUrsaString(CurrentExecutablePath().stem());
	profile.projectAssetsDirectory = root / "Assets";
	profile.projectLibraryDirectory = root / "Library";
	if (package.empty())
	{
		auto sourceRoot = ResolveEngineSourceRoot();
		if (!sourceRoot)
		{
			return Result<void>::Failure(sourceRoot.error);
		}
		profile.engineDirectories = {sourceRoot.value};
	}
	else
	{
		auto archive = PakArchive::Open(package);
		if (!archive)
		{
			return Result<void>::Failure(archive.error);
		}
		for (const String scheme : {String(engineScheme), String(projectScheme)})
		{
			auto view = archive.value->View(scheme);
			if (!view)
			{
				return Result<void>::Failure(view.error);
			}
			profile.packagedProviders.push_back({scheme, view.value});
		}
	}
	auto files = OpenRuntimeFileSystem(profile);
	if (!files)
	{
		return Result<void>::Failure(files.error);
	}
	auto openedResult = ProjectWorkspace::FromFilesystem(root, files.value);
	if (!openedResult)
	{
		return Result<void>::Failure(openedResult.error);
	}
	auto openedWorkspace = std::move(openedResult.value);
	if (package.empty())
	{
		AssetImportOptions options;
		options.bootstrapOnly = true;
		options.buildScripts = false;
		auto prepared = AssetImportPipeline::Prepare(*openedWorkspace, options);
		if (!prepared)
		{
			return Result<void>::Failure(prepared.error);
		}
		auto assigned = openedWorkspace->SetRuntimeContentView(prepared.value.runtimeFiles);
		if (!assigned)
		{
			return assigned;
		}
	}
	else
	{
		// A read-only player package has no Editor shaders or font. Prepare only
		// SDK bootstrap content in this project's Library, without modifying the pak.
		VirtualFileSystem bootstrapFiles;
		auto builtinRoot = ResolveEngineSourceRoot();
		if (!builtinRoot)
		{
			return Result<void>::Failure(builtinRoot.error);
		}
		auto mounted = bootstrapFiles.Mount(engineScheme, builtinRoot.value, MountAccess::ReadOnly);
		if (mounted)
		{
			mounted = bootstrapFiles.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(),
			                                       MountAccess::ReadWrite);
		}
		if (mounted)
		{
			auto library = DirectoryFileProvider::Open(root / "Library", true);
			mounted = library ? bootstrapFiles.MountProvider(editorLibraryScheme, library.value, MountAccess::ReadWrite)
			                  : Result<void>::Failure(library.error);
		}
		if (!mounted)
		{
			return mounted;
		}
		auto bootstrap = ProjectWorkspace::FromFilesystem(root, bootstrapFiles);
		if (!bootstrap)
		{
			return Result<void>::Failure(bootstrap.error);
		}
		AssetImportOptions options;
		options.bootstrapOnly = true;
		options.buildScripts = false;
		auto prepared = AssetImportPipeline::Prepare(*bootstrap.value, options);
		if (!prepared)
		{
			return Result<void>::Failure(prepared.error);
		}
		auto assigned = openedWorkspace->SetRuntimeContentView(prepared.value.runtimeFiles);
		if (!assigned)
		{
			return assigned;
		}
	}
	auto documentsResult = EditorDocuments::Create(*openedWorkspace);
	if (!documentsResult)
	{
		return Result<void>::Failure(documentsResult.error);
	}
	if (textureImportView && gui)
	{
		textureImportView->Close(*gui);
	}
	textureImportView.reset();
	modelImportView.reset();
	shaderImportView.reset();
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost && editingScriptSession)
	{
		auto closed = managedHost->CloseScriptSession(editingScriptSession);
		editingScriptSession = 0;
		if (!closed)
		{
			return closed;
		}
	}
#endif
	if (project)
	{
		for (const auto& fallback : managedEditorFallbacks)
		{
			project->componentEditors.ReplaceEditor(fallback.first, fallback.second);
		}
		for (const String& type : registeredScriptComponentTypes)
		{
			project->componentEditors.UnregisterType(type);
		}
	}
	managedExtensions.clear();
	managedOpenWindows.clear();
	managedEditorFallbacks.clear();
	registeredScriptComponentTypes.clear();
	activeEditorTool.clear();
	pendingManagedAsset = {};
	pendingRunDocument = {};
	requestRun = false;
	runAfterScriptBuild = false;
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(openedWorkspace->ContentFiles()));
	documents = std::move(documentsResult.value);
	workspace = std::move(openedWorkspace);
	showProjectPanel = true;
	showHierarchyPanel = true;
	showInspectorPanel = true;
	showScenePanel = true;
	showGamePanel = true;
	showConsolePanel = true;
	savedPanelVisibility.clear();
	auto loadedPanelState = LoadPanelVisibility();
	if (!loadedPanelState)
	{
		workspaceError = loadedPanelState.error;
	}
	hierarchy.ClearSelection();
	displayedSceneId = {};
	selectionContext.Clear();
	projectBrowser = ProjectBrowserModel{};
	projectBrowserPanel.Reset();
	auto browserRefreshed = projectBrowser.Refresh(*workspace);
	if (!browserRefreshed)
	{
		return browserRefreshed;
	}
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost)
	{
		managedHost->SetFilesystem(workspace->ContentFiles());
	}
#endif
	scriptCheckJob = {};
	observedScriptSources.clear();
	scriptBuildService.reset();
	scriptAssetDatabase.reset();
#if defined(URSA_ENABLE_DOTNET)
	if (package.empty() && workspace->Database())
	{
		auto scriptDatabase = AssetDatabase::Open(workspace->Database()->Filesystem());
		if (scriptDatabase)
		{
			scriptAssetDatabase = std::move(scriptDatabase.value);
			auto service = ScriptBuildService::Open(scriptAssetDatabase->Filesystem(), *scriptAssetDatabase,
			                                        std::filesystem::u8path(URSA_DOTNET_EXECUTABLE_PATH),
			                                        std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH),
			                                        std::filesystem::u8path(URSA_DOTNET_EDITOR_ASSEMBLY_PATH),
			                                        std::filesystem::u8path(URSA_DOTNET_CATALOG_GENERATOR_PATH),
			                                        std::filesystem::u8path(URSA_DOTNET_REFERENCE_PACK_PATH));
			if (service)
			{
				scriptBuildService = std::move(service.value);
			}
			else
			{
				scriptBuildStatus = service.error;
			}
		}
		else
		{
			scriptBuildStatus = scriptDatabase.error;
		}
	}
#endif
	activeEditorTool.clear();
	workspaceError.clear();
	URSA_LOG_INFO("Editor", "Opened project " + PathToUrsaString(workspace->ProjectRoot()));
	return {};
}

Result<void> EditorApplication::CompileScripts()
{
#if defined(URSA_ENABLE_DOTNET)
	if (!scriptBuildService || play.Active() || ScriptWorkPending())
	{
		return Result<void>::Failure("Script compilation requires an idle editing project");
	}
	ScriptBuildService* service = scriptBuildService.get();
	scriptBuildJob = std::async(std::launch::async, [service] { return service->Build(ScriptAssemblyGroup::Editor); });
	scriptBuildStatus = "正在编译脚本……";
	return {};
#else
	return Result<void>::Failure("This build does not enable .NET scripting");
#endif
}

void EditorApplication::DrawMainMenuBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (!assetImportStatus.empty())
		{
			ImGui::TextDisabled("%s", assetImportStatus.c_str());
			ImGui::Separator();
		}
		ImGui::TextUnformatted("Ursa Editor");
		ImGui::Separator();
		EditorDocument* current = documents ? documents->Current() : nullptr;
		if (ImGui::BeginMenu("File"))
		{
			ImGui::BeginDisabled(!documents || !current || play.Active());
			if (ImGui::MenuItem("Save", "Ctrl+S"))
			{
				auto saved = documents->SaveCurrent();
				workspaceError = saved ? String{} : saved.error;
				if (saved)
				{
					RequestAssetImport();
				}
			}
			if (ImGui::MenuItem("Close Document"))
			{
				auto closed = documents->RequestClose();
				workspaceError = closed ? String{} : closed.error;
			}
			ImGui::EndDisabled();
			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
			{
				exitRequested = true;
			}
			DrawManagedMenus("File");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			ImGui::BeginDisabled();
			ImGui::MenuItem("Undo", "Ctrl+Z");
			ImGui::MenuItem("Redo", "Ctrl+Y");
			ImGui::EndDisabled();
			DrawManagedMenus("Edit");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Assets"))
		{
			ImGui::BeginDisabled(!workspace || play.Active());
			ImGui::MenuItem("Auto Refresh", nullptr, &autoRefreshAssets);
			if (ImGui::MenuItem("Reimport All"))
			{
				RequestAssetImport({}, true);
			}
			if (ImGui::MenuItem("Refresh", "Ctrl+R"))
			{
				RequestAssetImport();
				auto refreshed = workspace->Refresh();
				workspaceError = refreshed ? String{} : refreshed.error;
				if (refreshed)
				{
					refreshed = projectBrowser.Refresh(*workspace);
					workspaceError = refreshed ? String{} : refreshed.error;
				}
			}
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem("Folder"))
				{
					auto created = CreateProjectFolder();
					workspaceError = created ? String{} : created.error;
				}
				if (ImGui::MenuItem("Scene"))
				{
					auto created = CreateProjectAsset(ResourceKind::Scene);
					workspaceError = created ? String{} : created.error;
				}
				if (ImGui::MenuItem("Prefab"))
				{
					auto created = CreateProjectAsset(ResourceKind::Prefab);
					workspaceError = created ? String{} : created.error;
				}
				if (ImGui::MenuItem("Material"))
				{
					auto created = CreateProjectAsset(ResourceKind::Material);
					workspaceError = created ? String{} : created.error;
				}
				if (ImGui::MenuItem("PBR Material"))
				{
					auto created = CreateProjectAsset(ResourceKind::Material, MaterialType::PbrMetallicRoughness);
					workspaceError = created ? String{} : created.error;
				}
				if (ImGui::MenuItem("Texture"))
				{
					auto created = CreateProjectAsset(ResourceKind::Texture);
					workspaceError = created ? String{} : created.error;
				}
				ImGui::EndMenu();
			}
			ImGui::BeginDisabled(selectionContext.kind != EditorSelectionKind::Asset || play.Active());
			if (ImGui::MenuItem("Rename Selected"))
			{
				projectBrowserPanel.RequestRenameSelected();
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			DrawManagedMenus("Assets");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("GameObject"))
		{
			const bool canCreateObject = current && current->Kind() == ResourceKind::Scene && !play.Active();
			ImGui::BeginDisabled(!canCreateObject);
			if (ImGui::MenuItem("Create Empty"))
			{
				auto draft = current->Payload<SceneDraft>();
				auto& scene = draft->GetScene();
				GameObject::Ptr parent;
				auto selected = hierarchy.SelectedGameObject(scene);
				if (selected)
				{
					parent = selected.value;
				}
				auto created = scene.CreateGameObject("GameObject", parent);
				workspaceError = created ? String{} : created.error;
				if (created)
				{
					hierarchy.Select(scene, created.value);
					selectionContext.SelectGameObject(current->Id(), created.value->Id());
					current->MarkDirty();
				}
			}
			ImGui::EndDisabled();
			DrawManagedMenus("GameObject");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Component"))
		{
			auto draft = current ? current->Payload<SceneDraft>() : nullptr;
			GameObject::Ptr selectedObject;
			ComponentInspectorContext context;
			ComponentLoadContext* loadContext = nullptr;
			std::unique_ptr<ComponentLoadContext> ownedLoadContext;
			if (draft && current->Kind() == ResourceKind::Scene)
			{
				context.scene = &draft->GetScene();
				auto selected = hierarchy.SelectedGameObject(*context.scene);
				if (selected)
				{
					selectedObject = selected.value;
					context.roots = context.scene->RootGameObjects();
				}
			}
			else if (draft && current->Kind() == ResourceKind::Prefab)
			{
				context.prefab = draft->GetPrefabAsset();
				if (context.prefab)
				{
					auto selected = hierarchy.SelectedGameObject(*context.prefab);
					if (selected)
					{
						selectedObject = selected.value;
						context.roots = {context.prefab->Root()};
					}
				}
			}
			if (draft && selectedObject && documents)
			{
				context.object = selectedObject;
				ownedLoadContext = std::make_unique<ComponentLoadContext>(documents->Session());
				loadContext = ownedLoadContext.get();
				context.loadContext = loadContext;
				if (draft->ContextSetup())
				{
					auto prepared = draft->ContextSetup()(*loadContext);
					if (!prepared)
					{
						inspectorError = prepared.error;
					}
				}
			}
			Vector<ComponentTypeDescriptor> types = project->componentEditors.Types();
			std::sort(types.begin(), types.end(),
			          [](const auto& left, const auto& right)
			          {
				          return left.category == right.category ? left.displayName < right.displayName
				                                                 : left.category < right.category;
			          });
			String category;
			for (const auto& type : types)
			{
				if (type.category != category)
				{
					if (!category.empty())
					{
						ImGui::EndMenu();
					}
					category = type.category;
					if (!ImGui::BeginMenu(category.c_str()))
					{
						category.clear();
						continue;
					}
				}
				auto available = selectedObject ? project->componentEditors.CanCreate(type.type, context)
				                                : Result<void>::Failure("Select a GameObject first");
				ImGui::BeginDisabled(!available || play.Active());
				if (ImGui::MenuItem(type.displayName.c_str()))
				{
					auto created = project->componentEditors.Create(type.type, context);
					workspaceError = created ? String{} : created.error;
					if (created && current)
					{
						selectionContext.SelectGameObject(current->Id(), selectedObject->Id());
						current->MarkDirty();
					}
				}
				ImGui::EndDisabled();
			}
			if (!category.empty())
			{
				ImGui::EndMenu();
			}
			DrawManagedMenus("Component");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window"))
		{
			if (ImGui::MenuItem("Reset Layout"))
			{
				auto cleared = gui->ClearSavedLayout();
				workspaceError = cleared ? String{} : cleared.error;
				if (cleared)
				{
					showProjectPanel = true;
					showHierarchyPanel = true;
					showInspectorPanel = true;
					showScenePanel = true;
					showGamePanel = true;
					showConsolePanel = true;
					savedPanelVisibility.clear();
					ImGui::ClearIniSettings();
					dockLayoutInitialized = false;
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Project", nullptr, showProjectPanel))
			{
				showProjectPanel = !showProjectPanel;
			}
			if (ImGui::MenuItem("Hierarchy", nullptr, showHierarchyPanel))
			{
				showHierarchyPanel = !showHierarchyPanel;
			}
			if (ImGui::MenuItem("Inspector", nullptr, showInspectorPanel))
			{
				showInspectorPanel = !showInspectorPanel;
			}
			if (ImGui::MenuItem("Scene", nullptr, showScenePanel))
			{
				showScenePanel = !showScenePanel;
			}
			if (ImGui::MenuItem("Game", nullptr, showGamePanel))
			{
				showGamePanel = !showGamePanel;
			}
			if (ImGui::MenuItem("Console", nullptr, showConsolePanel))
			{
				showConsolePanel = !showConsolePanel;
			}
			DrawManagedMenus("Window");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			ImGui::MenuItem("Ursa Editor Documentation", nullptr, false, false);
			ImGui::MenuItem("About Ursa Editor", nullptr, false, false);
			DrawManagedMenus("Help");
			ImGui::EndMenu();
		}
		auto makeToolContext = [&]()
		{
			EditorToolContext context;
			context.document = const_cast<EditorDocument*>(current);
			if (documents)
			{
				context.resources = &documents->Session();
			}
			if (!current)
			{
				return context;
			}
			auto draft = current->Payload<SceneDraft>();
			if (!draft)
			{
				return context;
			}
			if (current->Kind() == ResourceKind::Prefab)
			{
				context.prefab = draft->GetPrefabAsset();
				if (context.prefab)
				{
					auto selected = hierarchy.SelectedGameObject(*context.prefab);
					if (selected)
					{
						context.selection.push_back(selected.value);
					}
				}
			}
			else if (current->Kind() == ResourceKind::Scene)
			{
				context.scene = &draft->GetScene();
				auto selected = hierarchy.SelectedGameObject(*context.scene);
				if (selected)
				{
					context.selection.push_back(selected.value);
				}
			}
			return context;
		};
		if (ImGui::BeginMenu("Tools"))
		{
			auto context = makeToolContext();
			auto registeredTools = project->tools.Tools();
			std::sort(registeredTools.begin(), registeredTools.end(),
			          [](const EditorToolDescriptor& left, const EditorToolDescriptor& right)
			          {
				          return left.category == right.category ? left.displayName < right.displayName
				                                                 : left.category < right.category;
			          });
			String category;
			for (const auto& tool : registeredTools)
			{
				if (tool.category != category)
				{
					if (!category.empty())
					{
						ImGui::EndMenu();
					}
					category = tool.category;
					if (!ImGui::BeginMenu(category.c_str()))
					{
						category.clear();
						continue;
					}
				}
				auto available = project->tools.CanExecute(tool.id, context);
				ImGui::BeginDisabled(play.Active() || !available);
				if (ImGui::MenuItem(tool.displayName.c_str()))
				{
					activeEditorTool = tool.id;
				}
				ImGui::EndDisabled();
			}
			if (!category.empty())
			{
				ImGui::EndMenu();
			}
			DrawManagedMenus("Tools");
			ImGui::EndMenu();
		}
		Set<String> managedMenuRoots;
		for (const auto& extension : managedExtensions)
		{
			const String root = extension.path.substr(0, extension.path.find('/'));
			if (!root.empty() && root != "Tools" && root != "Scripts" && root != "File" && root != "Edit" &&
			    root != "Assets" && root != "GameObject" && root != "Component" && root != "Window" && root != "Help" &&
			    (extension.kind == "Menu" || extension.kind == "Window"))
			{
				managedMenuRoots.insert(root);
			}
		}
		for (const String& root : managedMenuRoots)
		{
			if (ImGui::BeginMenu(root.c_str()))
			{
				DrawManagedMenus(root);
				ImGui::EndMenu();
			}
		}
		if (scriptBuildService && ImGui::BeginMenu("Scripts"))
		{
			const bool compiling = scriptBuildJob.valid() || scriptCheckJob.valid();
			ImGui::BeginDisabled(compiling || play.Active());
			if (ImGui::MenuItem("Compile Scripts"))
			{
				CompileScripts();
			}
			ImGui::EndDisabled();
			DrawManagedMenus("Scripts");
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (workspace)
		{
			const auto name = PathToUrsaString(workspace->ProjectRoot().filename());
			ImGui::Text("Project: %s", name.c_str());
		}
		ImGui::EndMainMenuBar();
	}
	const auto* current = documents ? documents->Current() : nullptr;
	DrawPlayToolbar(current);
}

Result<void> EditorApplication::OpenProjectBrowserAsset(const ProjectBrowserEntry& asset)
{
	if (!workspace || !documents || asset.folder)
	{
		return Result<void>::Failure("Project asset cannot be opened in the current workspace");
	}
	projectBrowser.SelectAsset(asset.path);
	selectionContext.SelectAsset(asset.path);
	const auto openDocument = [&](Result<std::unique_ptr<EditorDocument>> opened) -> Result<void>
	{
		if (!opened)
		{
			return Result<void>::Failure(opened.error);
		}
		return documents->RequestOpen(std::move(opened.value));
	};
	if (asset.kind)
	{
		switch (*asset.kind)
		{
			case ResourceKind::Texture:
				return openDocument(OpenTextureDocument(asset.path, workspace->Index()));
			case ResourceKind::Material:
				return openDocument(OpenMaterialDocument(asset.path, documents->Session(), *workspace));
			case ResourceKind::Scene:
			case ResourceKind::Prefab:
				return openDocument(OpenSceneDocument(asset.path, *asset.kind, project->components,
				                                      documents->Session(), project->editContextSetup));
			case ResourceKind::Shader:
			{
				auto opened = ShaderImportView::Open(workspace->Index().Filesystem(), asset.path);
				if (!opened)
				{
					return Result<void>::Failure(opened.error);
				}
				shaderImportView = std::move(opened.value);
				return {};
			}
		}
	}
	if (IsTextureSource(asset.path) && workspace->Database())
	{
		auto settings = TextureImporter::Settings(*workspace->Database(), asset.path);
		auto opened = settings ? TextureImportView::Open(asset.path, settings.value)
		                       : Result<std::unique_ptr<TextureImportView>>::Failure(settings.error);
		if (!opened)
		{
			return Result<void>::Failure(opened.error);
		}
		if (textureImportView && gui)
		{
			textureImportView->Close(*gui);
		}
		textureImportView = std::move(opened.value);
		auto preview = textureImportView->Reimport(*workspace->Database(), device, *gui, true);
		if (!preview)
		{
			RequestAssetImport(asset.path);
		}
		return preview;
	}
	if (IsModelSource(asset.path) && workspace->Database())
	{
		auto settings = ModelImporter::Settings(*workspace->Database(), asset.path);
		auto opened = settings ? ModelImportView::Open(asset.path, settings.value)
		                       : Result<std::unique_ptr<ModelImportView>>::Failure(settings.error);
		if (!opened)
		{
			return Result<void>::Failure(opened.error);
		}
		modelImportView = std::move(opened.value);
		auto preview = modelImportView->Reimport(*workspace->Database(), true);
		if (!preview)
		{
			RequestAssetImport(asset.path);
		}
		return preview;
	}
	return Result<void>::Failure("No editor is registered for " + asset.path.ToString());
}

Result<void> EditorApplication::RenameProjectAsset(const VirtualPath& source, const VirtualPath& destination)
{
	if (!workspace || !workspace->Database())
	{
		return Result<void>::Failure("Project AssetDatabase is unavailable");
	}
	Result<void> renamed;
	if (documents && documents->Current() && documents->Current()->Path() == source)
	{
		renamed = documents->RenameCurrent(destination);
	}
	else
	{
		renamed = workspace->Database()->Move(source, destination);
	}
	if (!renamed)
	{
		return renamed;
	}
	RequestAssetImport();
	auto refreshed = workspace->Refresh();
	if (!refreshed)
	{
		return refreshed;
	}
	refreshed = projectBrowser.Refresh(*workspace);
	if (!refreshed)
	{
		return refreshed;
	}
	projectBrowser.SelectAsset(destination);
	selectionContext.SelectAsset(destination);
	return {};
}

Result<void> EditorApplication::CreateProjectAsset(ResourceKind kind, MaterialType materialType,
                                                   const String& requestedPath)
{
	if (!documents || !workspace || play.Active())
	{
		return Result<void>::Failure("Create an asset after opening a project and stopping Play mode");
	}
	String relativePath = requestedPath;
	if (relativePath.empty())
	{
		const char* filename = kind == ResourceKind::Scene     ? "NewScene.ursascene"
		                       : kind == ResourceKind::Prefab  ? "NewPrefab.ursaprefab"
		                       : kind == ResourceKind::Texture ? "NewTexture.ursatex"
		                                                       : "NewMaterial.ursamat";
		relativePath =
		    projectBrowser.CurrentFolder().empty() ? String(filename) : projectBrowser.CurrentFolder() + "/" + filename;
	}
	auto path = ProjectAssetPath(relativePath.c_str());
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	Result<std::unique_ptr<EditorDocument>> created =
	    Result<std::unique_ptr<EditorDocument>>::Failure("Unsupported Project asset kind");
	if (kind == ResourceKind::Scene || kind == ResourceKind::Prefab)
	{
		created =
		    NewSceneDocument(path.value, kind, project->components, documents->Session(), project->editContextSetup);
	}
	else if (kind == ResourceKind::Texture)
	{
		created = NewTextureDocument(path.value);
	}
	else if (kind == ResourceKind::Material)
	{
		created = NewMaterialDocument(path.value, materialType, *workspace);
	}
	if (!created)
	{
		return Result<void>::Failure(created.error);
	}
	auto opened = documents->RequestOpen(std::move(created.value));
	return opened ? Result<void>{} : opened;
}

Result<void> EditorApplication::CreateProjectFolder(const String& requestedName)
{
	if (!workspace || !workspace->Database() || play.Active())
	{
		return Result<void>::Failure("Create a folder after opening a project and stopping Play mode");
	}
	String folderName = requestedName.empty() ? String(folderNameInput.data()) : requestedName;
	if (folderName.empty())
	{
		folderName = "New Folder";
	}
	if (folderName.find('/') == String::npos && !projectBrowser.CurrentFolder().empty())
	{
		folderName = projectBrowser.CurrentFolder() + "/" + folderName;
	}
	auto path = ProjectAssetPath(folderName.c_str());
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	auto created = workspace->Database()->CreateFolder(path.value);
	if (!created)
	{
		return created;
	}
	RequestAssetImport();
	auto refreshed = workspace->Refresh();
	if (!refreshed)
	{
		return refreshed;
	}
	return projectBrowser.Refresh(*workspace);
}

Result<void> EditorApplication::LoadPanelVisibility()
{
	if (!workspace)
	{
		return {};
	}
	const auto path = VirtualPath::Parse(String(editorLibraryScheme) + ":Editor/Workspace/PanelVisibility.json");
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	auto exists = workspace->Index().Filesystem().Exists(path.value);
	if (!exists)
	{
		return Result<void>::Failure(exists.error);
	}
	if (!exists.value)
	{
		savedPanelVisibility.clear();
		return {};
	}
	auto text = workspace->Index().Filesystem().ReadText(path.value);
	if (!text)
	{
		return Result<void>::Failure(text.error);
	}
	try
	{
		const nlohmann::json state = nlohmann::json::parse(ToStdString(text.value));
		if (!state.is_object())
		{
			return Result<void>::Failure("Editor panel state must be a JSON object");
		}
		auto readFlag = [&](const char* name, bool& value)
		{
			if (state.contains(name) && state[name].is_boolean())
			{
				value = state[name].get<bool>();
			}
		};
		readFlag("project", showProjectPanel);
		readFlag("hierarchy", showHierarchyPanel);
		readFlag("inspector", showInspectorPanel);
		readFlag("scene", showScenePanel);
		readFlag("game", showGamePanel);
		readFlag("console", showConsolePanel);
		savedPanelVisibility = ToUrsaString(state.dump());
		return {};
	}
	catch (const nlohmann::json::exception& error)
	{
		return Result<void>::Failure("Cannot parse Editor panel state: " + ToUrsaString(error.what()));
	}
}

Result<void> EditorApplication::SavePanelVisibility()
{
	if (!workspace)
	{
		return {};
	}
	const nlohmann::json state = {{"project", showProjectPanel},     {"hierarchy", showHierarchyPanel},
	                              {"inspector", showInspectorPanel}, {"scene", showScenePanel},
	                              {"game", showGamePanel},           {"console", showConsolePanel}};
	const String encoded = ToUrsaString(state.dump());
	if (encoded == savedPanelVisibility)
	{
		return {};
	}
	const auto path = VirtualPath::Parse(String(editorLibraryScheme) + ":Editor/Workspace/PanelVisibility.json");
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	const std::string native = ToStdString(encoded);
	Vector<UInt8> bytes;
	bytes.reserve(native.size());
	for (unsigned char character : native)
	{
		bytes.push_back(character);
	}
	auto written = workspace->Index().Filesystem().WriteAtomic(path.value, bytes);
	if (written)
	{
		savedPanelVisibility = encoded;
	}
	return written;
}

void EditorApplication::DrawPlayToolbar(const EditorDocument* current)
{
	const auto* selectedDocument = current;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 36.0f), ImGuiCond_Always);
	constexpr ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
	                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::Begin("##EditorToolbar", nullptr, toolbarFlags);
	ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - 180.0f) * 0.5f));
	const bool canRun = selectedDocument && selectedDocument->Kind() == ResourceKind::Scene &&
	                    !documents->PromptPending() && !scriptBuildJob.valid() && !scriptCheckJob.valid();
	if (requestRun && !canRun)
	{
		requestRun = false;
	}
	ImGui::BeginDisabled(!play.Active() && !canRun);
	if (ImGui::Button(play.Active() ? "Stop" : "Play") || requestRun)
	{
		requestRun = false;
		runAfterScriptBuild = false;
		if (play.Active())
		{
			play.Stop();
			if (documents && documents->Current())
			{
				auto draft = documents->Current()->Payload<SceneDraft>();
				auto selected = draft && editSelectionBeforePlay
				                    ? draft->GetScene().GameObjectById(editSelectionBeforePlay)
				                    : Result<GameObject::Ptr>::Failure("No pre-play selection");
				if (selected)
				{
					auto restored = hierarchy.Select(draft->GetScene(), selected.value);
					if (!restored)
					{
						inspectorError = restored.error;
					}
				}
				else
				{
					hierarchy.ClearSelection();
				}
			}
			editSelectionBeforePlay = {};
			selectionContext = selectionBeforePlay;
			selectionBeforePlay = {};
			documents->SetSaveBlocked(false);
			viewportFocused = false;
		}
		else
		{
			bool scriptsReady = true;
#if !defined(URSA_ENABLE_DOTNET)
			if (selectedDocument && selectedDocument->Payload<SceneDraft>() &&
			    HasManagedScriptComponent(selectedDocument->Payload<SceneDraft>()->GetScene()))
			{
				scriptsReady = false;
				scriptBuildStatus = "当前构建未启用 .NET 脚本，场景中的脚本数据已保留；启用脚本支持后才能进入 Run。";
			}
#endif
			PlayContextSetup playSetup = project->playContextSetup;
			if (scriptBuildService)
			{
				auto selectedDocumentBuild = scriptBuildService->IsBuildCurrent(ScriptAssemblyGroup::Game);
				scriptsReady = selectedDocumentBuild && selectedDocumentBuild.value;
				if (!selectedDocumentBuild)
				{
					scriptBuildStatus = selectedDocumentBuild.error;
				}
				else if (!selectedDocumentBuild.value)
				{
					ScriptBuildService* service = scriptBuildService.get();
					scriptBuildJob = std::async(std::launch::async,
					                            [service] { return service->Build(ScriptAssemblyGroup::Editor); });
					runAfterScriptBuild = true;
					pendingRunDocument = selectedDocument->Id();
					scriptBuildStatus = "正在编译最新脚本，成功后进入 Run……";
				}
#if defined(URSA_ENABLE_DOTNET)
				if (scriptsReady && scriptAssetDatabase)
				{
					Vector<ScriptSourceAsset> sources = scriptAssetDatabase->ScriptSources();
					const bool hasScriptComponent =
					    selectedDocument && selectedDocument->Kind() == ResourceKind::Scene &&
					    selectedDocument->Payload<SceneDraft>() &&
					    HasManagedScriptComponent(selectedDocument->Payload<SceneDraft>()->GetScene());
					if (sources.empty() && hasScriptComponent)
					{
						scriptsReady = false;
						scriptBuildStatus = "当前场景包含脚本组件，但项目没有可用的 C# 脚本源码。";
					}
					else if (!sources.empty())
					{
						const VirtualPath gameAssemblyPath =
						    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Manifests/Game.json").value;
						auto assemblyBytes = scriptAssetDatabase->Filesystem().Read(gameAssemblyPath);
						if (!assemblyBytes)
						{
							scriptsReady = false;
							scriptBuildStatus = "无法读取当前游戏脚本程序集：" + assemblyBytes.error;
						}
						else
						{
							auto readyHost = EnsureManagedHost();
							if (!readyHost)
							{
								scriptsReady = false;
								scriptBuildStatus = readyHost.error;
							}
							if (scriptsReady && managedHost)
							{
								auto userPlaySetup = project->playContextSetup;
								auto managed = managedHost;
								auto activeWindow = window;
								VirtualPath gameContent = gameAssemblyPath;
								playSetup = [userPlaySetup, managed, gameContent, activeWindow](
								                ComponentLoadContext& context) -> Result<std::shared_ptr<void>>
								{
									std::shared_ptr<void> projectServices;
									if (userPlaySetup)
									{
										auto setup = userPlaySetup(context);
										if (!setup)
										{
											return Result<std::shared_ptr<void>>::Failure(setup.error);
										}
										projectServices = std::move(setup.value);
									}
									auto session = std::make_shared<ManagedPlaySessionState>();
									session->host = managed;
									session->projectServices = std::move(projectServices);
									auto domain = managed->CreateScriptSession(false);
									if (!domain)
									{
										return Result<std::shared_ptr<void>>::Failure(domain.error);
									}
									session->sessionId = domain.value;
									auto loaded = managed->LoadScriptContent(domain.value, gameContent);
									if (!loaded)
									{
										return Result<std::shared_ptr<void>>::Failure(loaded.error);
									}
									auto validated = managed->ValidateScriptSession(domain.value);
									if (!validated)
									{
										return Result<std::shared_ptr<void>>::Failure(validated.error);
									}
									context.SetSceneReadyCallback(
									    [session, activeWindow](Scene& scene,
									                            ComponentLoadContext& loadContext) -> Result<void>
									    {
										    session->sceneApi = std::make_unique<Scripting::ManagedSceneApi>(
										        scene, &loadContext.Resources(),
										        [activeWindow](const String& title)
										        {
											        if (activeWindow)
											        {
												        activeWindow->SetTitle(title);
											        }
										        });
										    auto bound = session->host->BindScriptSession(session->sessionId,
										                                                  *session->sceneApi);
										    if (!bound)
										    {
											    return bound;
										    }
										    session->scriptRuntime = std::make_unique<Scripting::ManagedScriptRuntime>(
										        scene, *session->host, *session->sceneApi, session->sessionId);
										    return session->scriptRuntime->AttachSerializedComponents();
									    });
									return Result<std::shared_ptr<void>>::Success(session);
								};
							}
						}
					}
				}
#endif
			}
			auto draft = scriptsReady ? selectedDocument->Payload<SceneDraft>() : nullptr;
			if (draft)
			{
				editSelectionBeforePlay = hierarchy.SelectedId();
				selectionBeforePlay = selectionContext;
				auto started =
				    play.Start(*draft, selectedDocument->Id(), project->components, documents->Session(), playSetup);
				inspectorError = started ? String{} : started.error;
				if (started)
				{
					documents->SetSaveBlocked(true);
				}
				else
				{
					editSelectionBeforePlay = {};
					selectionBeforePlay = {};
				}
			}
		}
	}
	ImGui::EndDisabled();
	if (play.Active())
	{
		ImGui::SameLine();
		if (ImGui::Button(play.Paused() ? "Resume" : "Pause"))
		{
			if (play.Paused())
			{
				play.Resume();
			}
			else
			{
				play.Pause();
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!play.Paused());
		if (ImGui::Button("Step"))
		{
			play.Step();
		}
		ImGui::EndDisabled();
	}
	if (!scriptBuildStatus.empty())
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("Script Build"))
		{
			ImGui::OpenPopup("Script Build Diagnostics");
		}
		if (ImGui::BeginPopup("Script Build Diagnostics"))
		{
			ImGui::SetNextWindowSize({700.0f, 360.0f}, ImGuiCond_Appearing);
			ImGui::TextUnformatted(scriptBuildStatus.c_str());
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

Result<void> EditorApplication::OnGui()
{
	selectionContext.SetActiveDocument(documents && documents->Current() ? documents->Current()->Id() : ResourceId{});
	if (!pendingManagedAsset.scheme.empty())
	{
		const VirtualPath path = std::move(pendingManagedAsset);
		pendingManagedAsset = {};
		auto opened = OpenEditorAsset(path);
		if (!opened)
		{
			workspaceError = opened.error;
		}
	}
	if (scriptBuildJob.valid() && scriptBuildJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto result = scriptBuildJob.get();
		if (!result)
		{
			scriptBuildStatus = result.error;
			runAfterScriptBuild = false;
		}
		else
		{
			scriptBuildStatus = result.value.succeeded ? "脚本编译成功。\n" + result.value.diagnostics
			                                           : "脚本编译失败。\n" + result.value.diagnostics;
			if (result.value.succeeded)
			{
				auto registered = ReloadEditorScripts();
				if (!registered)
				{
					scriptBuildStatus += "\n" + registered.error;
					runAfterScriptBuild = false;
				}
				else if (runAfterScriptBuild)
				{
					requestRun = documents && documents->Current() && documents->Current()->Id() == pendingRunDocument;
				}
			}
			if (!result.value.succeeded)
			{
				runAfterScriptBuild = false;
			}
		}
	}
	if (scriptCheckJob.valid() && scriptCheckJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto checked = scriptCheckJob.get();
		if (!checked)
		{
			scriptBuildStatus = checked.error;
		}
		else if (checked.value != observedScriptSources)
		{
			observedScriptSources = std::move(checked.value);
			if (!play.Active() && !scriptBuildJob.valid())
			{
				ScriptBuildService* service = scriptBuildService.get();
				scriptBuildJob =
				    std::async(std::launch::async, [service] { return service->Build(ScriptAssemblyGroup::Editor); });
				scriptBuildStatus = "检测到脚本变化，正在后台编译……";
			}
		}
	}
	if (selectionContext.kind == EditorSelectionKind::Asset && documents && documents->Current() &&
	    documents->Current()->Path() == selectionContext.assetPath)
	{
		selectionContext.Clear();
	}
	const auto checkTime = std::chrono::steady_clock::now();
	if (scriptBuildService && !requestRun && !play.Active() && !scriptBuildJob.valid() && !scriptCheckJob.valid() &&
	    checkTime >= nextScriptCheck)
	{
		ScriptBuildService* service = scriptBuildService.get();
		scriptCheckJob = std::async(std::launch::async, [service] { return service->SourceFingerprint(); });
		nextScriptCheck = checkTime + std::chrono::seconds(2);
	}
	DrawMainMenuBar();
	if (!activeEditorTool.empty())
	{
		const auto* tool = project->tools.Find(activeEditorTool);
		if (!tool)
		{
			activeEditorTool.clear();
		}
		else
		{
			bool open = true;
			const String windowName = tool->displayName + "##" + tool->id;
			if (ImGui::Begin(windowName.c_str(), &open))
			{
				auto context = [&]()
				{
					EditorToolContext result;
					result.document = documents ? documents->Current() : nullptr;
					if (documents)
					{
						result.resources = &documents->Session();
					}
					if (result.document)
					{
						auto draft = result.document->Payload<SceneDraft>();
						if (draft && result.document->Kind() == ResourceKind::Prefab)
						{
							result.prefab = draft->GetPrefabAsset();
							if (result.prefab)
							{
								auto selected = hierarchy.SelectedGameObject(*result.prefab);
								if (selected)
								{
									result.selection.push_back(selected.value);
								}
							}
						}
						else if (draft && result.document->Kind() == ResourceKind::Scene)
						{
							result.scene = &draft->GetScene();
							auto selected = hierarchy.SelectedGameObject(*result.scene);
							if (selected)
							{
								result.selection.push_back(selected.value);
							}
						}
					}
					return result;
				}();
				ImGuiInspectorUI ui;
				auto drawn = project->tools.Draw(activeEditorTool, context, ui);
				if (!drawn)
				{
					inspectorError = drawn.error;
				}
				else if (drawn.value && context.document)
				{
					context.document->MarkDirty();
				}
			}
			ImGui::End();
			if (!open)
			{
				activeEditorTool.clear();
			}
		}
	}
	for (const auto& extension : managedExtensions)
	{
		if (extension.kind != "Window" || !managedOpenWindows.count(extension.id))
		{
			continue;
		}
		bool open = true;
		const String title = extension.title + "###" + extension.id;
		if (ImGui::Begin(title.c_str(), &open))
		{
			auto drawn = InvokeEditorTool(extension);
			if (!drawn)
			{
				ImGui::TextWrapped("%s", drawn.error.c_str());
			}
		}
		ImGui::End();
		if (!open)
		{
			managedOpenWindows.erase(extension.id);
		}
	}
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	constexpr Ursa::Float32 toolbarHeight = 36.0f;
	const ImVec2 dockspacePosition(viewport->WorkPos.x, viewport->WorkPos.y + toolbarHeight);
	const ImVec2 dockspaceSize(viewport->WorkSize.x, std::max(1.0f, viewport->WorkSize.y - toolbarHeight));
	ImGui::SetNextWindowPos(dockspacePosition, ImGuiCond_Always);
	ImGui::SetNextWindowSize(dockspaceSize, ImGuiCond_Always);
	constexpr ImGuiWindowFlags dockspaceHostFlags =
	    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
	    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
	    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::Begin("##UrsaEditorDockspaceHost", nullptr, dockspaceHostFlags);
	const ImGuiID dockspace = ImGui::GetID("UrsaEditorDockspace");
	ImGui::DockSpace(dockspace, ImGui::GetContentRegionAvail());
	if (!dockLayoutInitialized)
	{
		ImGui::DockBuilderRemoveNode(dockspace);
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace, dockspaceSize);
		ImGuiID inspectorDock = 0;
		ImGuiID leftDock = 0;
		ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Right, 0.25f, &inspectorDock, &leftDock);
		ImGuiID projectDock = 0;
		ImGuiID upperDock = 0;
		ImGui::DockBuilderSplitNode(leftDock, ImGuiDir_Down, 0.28f, &projectDock, &upperDock);
		ImGuiID hierarchyDock = 0;
		ImGuiID sceneDock = 0;
		ImGui::DockBuilderSplitNode(upperDock, ImGuiDir_Left, 0.23f, &hierarchyDock, &sceneDock);
		ImGui::DockBuilderDockWindow("Project", projectDock);
		ImGui::DockBuilderDockWindow("Console", projectDock);
		ImGui::DockBuilderDockWindow("Hierarchy", hierarchyDock);
		ImGui::DockBuilderDockWindow("Scene", sceneDock);
		ImGui::DockBuilderDockWindow("Game", sceneDock);
		ImGui::DockBuilderDockWindow("Inspector", inspectorDock);
		ImGui::DockBuilderFinish(dockspace);
		dockLayoutInitialized = true;
	}
	ImGui::End();
	if (workspace && showProjectPanel)
	{
		ProjectBrowserActions actions;
		actions.refresh = [this]()
		{
			RequestAssetImport();
			auto refreshed = workspace->Refresh();
			if (refreshed)
			{
				refreshed = projectBrowser.Refresh(*workspace);
			}
			if (refreshed && projectBrowser.SelectedAsset().scheme.empty() &&
			    selectionContext.kind == EditorSelectionKind::Asset)
			{
				selectionContext.Clear();
			}
			return refreshed;
		};
		actions.createFolder = [this](const String& name) { return CreateProjectFolder(name); };
		actions.createAsset = [this](ResourceKind kind, MaterialType materialType, const String& name)
		{ return CreateProjectAsset(kind, materialType, name); };
		actions.renameAsset = [this](const VirtualPath& source, const VirtualPath& destination)
		{ return RenameProjectAsset(source, destination); };
		actions.openAsset = [this](const ProjectBrowserEntry& asset) { return OpenProjectBrowserAsset(asset); };
		auto drawn = projectBrowserPanel.Draw(*workspace, projectBrowser, selectionContext, hierarchy, showProjectPanel,
		                                      play.Active(), workspaceError, actions);
		if (!drawn)
		{
			workspaceError = drawn.error;
		}
	}
	else if (showProjectPanel)
	{
		ImGui::Begin("Project", &showProjectPanel);
		ImGui::TextDisabled("Open a project to browse its Assets folder.");
		ImGui::End();
	}
	if (shaderImportView && workspace)
	{
		ImGui::Begin("Shader Import");
		ImGui::TextUnformatted(shaderImportView->SourcePath().ToString().c_str());
		ImGui::TextDisabled("%s", shaderImportView->SourceReadOnly() ? "Read-only source" : "Writable project source");
		ImGui::BeginDisabled(play.Active());
		if (ImGui::Button("Reimport Shader"))
		{
			RequestAssetImport(shaderImportView->SourcePath(), true);
		}
		ImGui::EndDisabled();
		if (shaderImportView->Imported())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.4f, 1.0f), "%s",
			                   shaderImportView->SourceReadOnly() ? "Compiled in memory (read-only)" : "Imported");
		}
		for (const String& dependency : shaderImportView->Dependencies())
		{
			ImGui::TextDisabled("Include: %s", dependency.c_str());
		}
		if (!shaderImportView->Diagnostic().empty())
		{
			ImGui::TextWrapped("%s", shaderImportView->Diagnostic().c_str());
		}
		ImGui::BeginChild("ShaderSource", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted(shaderImportView->SourceText().c_str());
		ImGui::EndChild();
		ImGui::End();
	}
	if (textureImportView && workspace && workspace->Database())
	{
		auto drawn =
		    textureImportView->Draw(*workspace->Database(), device, *gui, !play.Active() && !assetImportJob.valid(),
		                            [this](const VirtualPath& path) { RequestAssetImport(path, true); });
		if (!drawn)
		{
			inspectorError = drawn.error;
		}
	}
	if (modelImportView && workspace && workspace->Database())
	{
		std::function<Result<void>()> instantiate;
		if (!play.Active() && documents && documents->Current() &&
		    (documents->Current()->Kind() == ResourceKind::Scene ||
		     documents->Current()->Kind() == ResourceKind::Prefab))
		{
			auto draft = documents->Current()->Payload<SceneDraft>();
			if (draft)
			{
				instantiate = [this, draft]
				{
					auto added = modelImportView->Instantiate(*draft);
					if (added && documents && documents->Current())
					{
						documents->Current()->MarkDirty();
					}
					return added;
				};
			}
		}
		auto drawn =
		    modelImportView->Draw(*workspace->Database(), !play.Active() && !assetImportJob.valid(), instantiate,
		                          [this](const VirtualPath& path) { RequestAssetImport(path, true); });
		if (!drawn)
		{
			inspectorError = drawn.error;
		}
	}
	if (selectionContext.kind != EditorSelectionKind::Asset && documents && documents->Current() &&
	    documents->Current()->Kind() == ResourceKind::Texture)
	{
		auto inspected = DrawTextureInspector(*documents->Current(), inspectorError);
		if (!inspected)
		{
			workspaceError = inspected.error;
		}
	}
	if (selectionContext.kind != EditorSelectionKind::Asset && documents && documents->Current() &&
	    documents->Current()->Kind() == ResourceKind::Material)
	{
		ImGuiInspectorUI materialInspectorUi;
		auto inspected = DrawMaterialInspector(*documents->Current(), *documents, *workspace, inspectorError,
		                                       &project->materialInspectors, &materialInspectorUi);
		if (!inspected)
		{
			workspaceError = inspected.error;
		}
		auto draft = documents->Current()->Payload<MaterialDraft>();
		if (draft && materialPreview)
		{
			auto rendered = materialPreview->Render(draft->MaterialPtr());
			if (!rendered)
			{
				inspectorError = rendered.error;
			}
			ImGui::Begin("Material Preview");
			if (rendered)
			{
				ImGui::Image(materialPreview->ImageId(), ImVec2(256, 256));
			}
			ImGui::End();
		}
	}
	if (documents && documents->Current() &&
	    (documents->Current()->Kind() == ResourceKind::Scene || documents->Current()->Kind() == ResourceKind::Prefab))
	{
		if (displayedSceneId != documents->Current()->Id())
		{
			hierarchy.ClearSelection();
			if (selectionContext.kind == EditorSelectionKind::GameObject &&
			    selectionContext.documentId != documents->Current()->Id())
			{
				selectionContext.Clear();
			}
			displayedSceneId = documents->Current()->Id();
		}
		auto draft = documents->Current()->Payload<SceneDraft>();
		if (draft)
		{
			if (play.Active())
			{
				auto hierarchyResult = hierarchy.Draw(*play.GetScene(), &showHierarchyPanel);
				if (!hierarchyResult)
				{
					inspectorError = hierarchyResult.error;
				}
				if (hierarchy.SelectedId())
				{
					selectionContext.SelectGameObject(documents->Current()->Id(), hierarchy.SelectedId());
				}
				else if (selectionContext.kind == EditorSelectionKind::GameObject)
				{
					selectionContext.Clear();
				}
			}
			else
			{
				auto inspected = DrawSceneInspector(
				    *documents->Current(), hierarchy, *documents, *workspace, project->components,
				    project->componentEditors, inspectorError,
				    scriptBuildService ? &scriptBuildService->ScriptTypes() : nullptr, &showHierarchyPanel,
				    &showInspectorPanel, selectionContext.kind != EditorSelectionKind::Asset && showInspectorPanel);
				if (!inspected)
				{
					workspaceError = inspected.error;
				}
				if (selectionContext.kind != EditorSelectionKind::Asset)
				{
					if (hierarchy.SelectedId())
					{
						selectionContext.SelectGameObject(documents->Current()->Id(), hierarchy.SelectedId());
					}
					else
					{
						selectionContext.Clear();
					}
				}
			}
			VirtualFileSystem* settingsFiles = workspace ? &workspace->Index().Filesystem() : nullptr;
			const String projectKey = workspace ? ProjectSettingsKey(workspace->ProjectRoot()) : String("default");
			auto drawn = sceneViewPanel->Draw(*draft, *documents->Current(), hierarchy, play, settingsFiles, projectKey,
			                                  showScenePanel, showGamePanel, viewportFocused, inspectorError);
			if (!drawn)
			{
				inspectorError = drawn.error;
			}
			if (play.Active() && showInspectorPanel && selectionContext.kind != EditorSelectionKind::Asset)
			{
				DrawRuntimeSceneInspector(*play.GetScene(), hierarchy, &showInspectorPanel, inspectorError);
			}
		}
	}
	else
	{
		displayedSceneId = {};
		hierarchy.ClearSelection();
		if (selectionContext.kind == EditorSelectionKind::GameObject)
		{
			selectionContext.Clear();
		}
	}
	const bool sceneOpen =
	    documents && documents->Current() &&
	    (documents->Current()->Kind() == ResourceKind::Scene || documents->Current()->Kind() == ResourceKind::Prefab);
	if (!sceneOpen)
	{
		hierarchy.DrawEmpty(&showHierarchyPanel);
		if (sceneViewPanel)
		{
			sceneViewPanel->DrawEmpty(showScenePanel, showGamePanel);
		}
	}
	const bool assetInspectorOpen = documents && documents->Current() &&
	                                    (documents->Current()->Kind() == ResourceKind::Material ||
	                                     documents->Current()->Kind() == ResourceKind::Texture) ||
	                                modelImportView != nullptr;
	if (!sceneOpen && !assetInspectorOpen && selectionContext.kind != EditorSelectionKind::Asset)
	{
		if (showInspectorPanel)
		{
			ImGui::Begin("Inspector", &showInspectorPanel);
			ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
			ImGui::End();
		}
	}
	if (workspace)
	{
		assetInspectorPanel.Draw(*workspace, projectBrowser, selectionContext, showInspectorPanel,
		                         [this](const ProjectBrowserEntry& asset)
		                         {
			                         auto opened = OpenProjectBrowserAsset(asset);
			                         workspaceError = opened ? String{} : opened.error;
			                         return opened;
		                         });
	}
	consolePanel.Draw(showConsolePanel, inspectorError,
	                  [this](const LogRecord& record) -> Result<void>
	                  {
		                  Result<void> resourceResult;
		                  VirtualPath resourcePath;
		                  if (!record.context.resourcePath.empty())
		                  {
			                  auto parsedPath = VirtualPath::Parse(record.context.resourcePath);
			                  if (!parsedPath)
			                  {
				                  return Result<void>::Failure(parsedPath.error);
			                  }
			                  resourcePath = parsedPath.value;
			                  if (resourcePath.scheme == projectResourceScheme)
			                  {
				                  if (const auto* asset = projectBrowser.Find(resourcePath))
				                  {
					                  const Size separator =
					                      asset->folder ? String::npos : asset->path.relative.rfind('/');
					                  const String parent = asset->folder ? asset->path.relative
					                                        : separator == String::npos
					                                            ? String{}
					                                            : asset->path.relative.substr(0, separator);
					                  projectBrowser.NavigateTo(parent);
				                  }
				                  selectionContext.SelectAsset(resourcePath);
				                  resourceResult = OpenEditorAsset(resourcePath);
			                  }
			                  else
			                  {
				                  resourceResult = OpenEditorAsset(resourcePath);
			                  }
			                  if (!resourceResult || record.context.objectId.empty())
			                  {
				                  return resourceResult;
			                  }
		                  }
		                  if (!record.context.objectId.empty() && documents && documents->Current() &&
		                      (documents->Current()->Kind() == ResourceKind::Scene ||
		                       documents->Current()->Kind() == ResourceKind::Prefab))
		                  {
			                  auto id = ResourceId::Parse(record.context.objectId);
			                  auto draft = documents->Current()->Payload<SceneDraft>();
			                  if (!id || !draft)
			                  {
				                  return Result<void>::Failure(id ? "No scene document is open" : id.error);
			                  }
			                  if (!resourcePath.scheme.empty() && documents->Current()->Path() != resourcePath)
			                  {
				                  return resourceResult;
			                  }
			                  if (documents->Current()->Kind() == ResourceKind::Scene)
			                  {
				                  auto object = draft->GetScene().GameObjectById(id.value);
				                  if (!object)
				                  {
					                  return Result<void>::Failure(object.error);
				                  }
				                  auto selected = hierarchy.Select(draft->GetScene(), object.value);
				                  if (selected)
				                  {
					                  selectionContext.SelectGameObject(documents->Current()->Id(), id.value);
				                  }
				                  return selected;
			                  }
			                  auto prefab = draft->GetPrefabAsset();
			                  if (!prefab || !prefab->Root())
			                  {
				                  return Result<void>::Failure("Prefab document has no GameObject hierarchy");
			                  }
			                  std::function<GameObject::Ptr(const GameObject::Ptr&)> findObject =
			                      [&](const GameObject::Ptr& candidate)
			                  {
				                  if (candidate->Id() == id.value)
				                  {
					                  return candidate;
				                  }
				                  for (const GameObject::Ptr& child : candidate->Children())
				                  {
					                  auto found = findObject(child);
					                  if (found)
					                  {
						                  return found;
					                  }
				                  }
				                  return GameObject::Ptr{};
			                  };
			                  auto object = findObject(prefab->Root());
			                  if (!object)
			                  {
				                  return Result<void>::Failure("GameObject is not present in the Prefab");
			                  }
			                  auto selected = hierarchy.Select(*prefab, object);
			                  if (selected)
			                  {
				                  selectionContext.SelectGameObject(documents->Current()->Id(), id.value);
			                  }
			                  return selected;
		                  }
		                  return resourcePath.scheme.empty()
		                             ? Result<void>::Failure("Log entry has no navigable resource or object context")
		                             : resourceResult;
	                  });
	if (documents)
	{
		documents->DrawUnsavedPrompt();
	}
	auto panelStateSaved = SavePanelVisibility();
	if (!panelStateSaved)
	{
		workspaceError = panelStateSaved.error;
	}
	return {};
}

Result<void> EditorApplication::RegisterScriptComponentTypes(const Vector<ManagedExtensionDescriptor>* extensions,
                                                             UInt64 sessionId)
{
#if defined(URSA_ENABLE_DOTNET)
	if (!scriptBuildService || !project)
	{
		return {};
	}
	ComponentEditors candidateEditors = project->componentEditors;
	for (const auto& fallback : managedEditorFallbacks)
	{
		candidateEditors.ReplaceEditor(fallback.first, fallback.second);
	}
	ComponentRegistry candidateRegistry = project->components;
	for (const String& oldType : registeredScriptComponentTypes)
	{
		candidateEditors.UnregisterType(oldType);
	}
	Map<String, nlohmann::json> constructorDefaults;
	if (sessionId)
	{
		nlohmann::json defaultSnapshots = nlohmann::json::array();
		for (const auto& type : scriptBuildService->ScriptTypes())
		{
			if (type.group != ScriptAssemblyGroup::Game)
			{
				continue;
			}
			defaultSnapshots.push_back({{"Id", ToStdString(type.scriptType)},
			                            {"Payload",
			                             {{"FormatVersion", 1u},
			                              {"ScriptType", ToStdString(type.scriptType)},
			                              {"Version", type.version},
			                              {"Fields", nlohmann::json::object()}}}});
		}
		auto defaults = managedHost->PrepareScriptReload(sessionId, ToUrsaString(defaultSnapshots.dump()));
		if (!defaults)
		{
			return Result<void>::Failure(defaults.error);
		}
		try
		{
			for (const auto& item : nlohmann::json::parse(ToStdString(defaults.value)))
			{
				constructorDefaults[ToUrsaString(item.at("Id").get<std::string>())] = item.at("Payload").at("Fields");
			}
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure("Cannot read script constructor defaults: " + ToUrsaString(error.what()));
		}
	}
	Set<String> currentTypes;
	for (ScriptTypeDescriptor scriptType : scriptBuildService->ScriptTypes())
	{
		if (scriptType.group != ScriptAssemblyGroup::Game)
		{
			continue;
		}
		auto defaults = constructorDefaults.find(scriptType.scriptType);
		if (defaults != constructorDefaults.end())
		{
			for (auto& field : scriptType.fields)
			{
				const auto value = defaults->second.find(ToStdString(field.name));
				if (value != defaults->second.end())
				{
					field.defaultValueJson = ToUrsaString(value->dump());
				}
			}
		}
		Map<String, String> aliases;
		for (const ScriptFieldTypeDescriptor& field : scriptType.fields)
		{
			for (const String& formerName : field.formerNames)
			{
				auto found = aliases.find(formerName);
				if (found != aliases.end() && found->second != field.name)
				{
					return Result<void>::Failure("Former script field name maps to multiple current fields: " +
					                             formerName);
				}
				aliases[formerName] = field.name;
			}
		}
		auto replaced = candidateRegistry.ReplaceScriptFieldAliases(scriptType.scriptType, std::move(aliases));
		if (!replaced)
		{
			return replaced;
		}
		auto registered = RegisterScriptComponentType(candidateEditors, scriptType);
		if (!registered)
		{
			return registered;
		}
		currentTypes.insert(scriptType.clrTypeName);
	}
	Map<String, ComponentEditor> candidateFallbacks;
	if (extensions)
	{
		for (const auto& extension : *extensions)
		{
			if (extension.kind != "Inspector")
			{
				continue;
			}
			String target = extension.target;
			bool matchedScript = false;
			for (const auto& scriptType : scriptBuildService->ScriptTypes())
			{
				if (scriptType.scriptType == target && scriptType.group == ScriptAssemblyGroup::Game)
				{
					target = scriptType.clrTypeName;
					matchedScript = true;
					break;
				}
			}
			if (!matchedScript && (!candidateRegistry.Find(target) || !candidateEditors.FindType(target)))
			{
				return Result<void>::Failure("Custom Inspector target is not an exposed component: " +
				                             extension.target);
			}
			if (const auto* previous = candidateEditors.Find(target))
			{
				candidateFallbacks[target] = *previous;
			}
			else
			{
				candidateFallbacks[target] = {};
			}
			const String extensionId = extension.id;
			candidateEditors.ReplaceEditor(
			    target,
			    [this, extensionId, sessionId](Component& component, ComponentInspectorContext& selection,
			                                   InspectorUI&) -> Result<bool>
			    {
				    if (!managedHost || managedHost->IsSessionFaulted())
				    {
					    return Result<bool>::Failure("Managed Editor environment is unavailable");
				    }
				    ManagedEditorContext context;
				    context.document = documents ? documents->Current() : nullptr;
				    context.documents = documents.get();
				    context.workspace = workspace.get();
				    context.registry = &project->components;
				    context.target = &component;
				    context.selection = selection;
				    context.saveContext.scene = selection.scene;
				    context.saveContext.materialId = [this](const std::shared_ptr<MaterialInstance>& material)
				    { return documents->Session().IdentifyMaterial(material); };
				    context.openAsset = [this](const VirtualPath& path)
				    {
					    pendingManagedAsset = path;
					    return Result<void>{};
				    };
				    return InvokeManagedEditorExtension(*managedHost, sessionId, extensionId, context);
			    });
		}
	}
	managedEditorFallbacks = std::move(candidateFallbacks);
	project->componentEditors = std::move(candidateEditors);
	project->components = std::move(candidateRegistry);
	registeredScriptComponentTypes = std::move(currentTypes);
#else
	(void)extensions;
	(void)sessionId;
#endif
	return {};
}

Result<void> EditorApplication::Run(Ursa::UInt32 width, Ursa::UInt32 height, const String& title, bool visible,
                                    Ursa::UInt32 frameLimit)
{
	auto windowResult = Window::Create(width, height, title, visible);
	if (!windowResult)
	{
		return Result<void>::Failure(windowResult.error);
	}
	window = std::move(windowResult.value);

	Rhi::DeviceDesc deviceDesc;
	deviceDesc.window = window;
#ifndef NDEBUG
	deviceDesc.validation = true;
#endif
	auto deviceResult = Rhi::CreateDevice(deviceDesc);
	if (!deviceResult)
	{
		window.reset();
		return Result<void>::Failure(deviceResult.error);
	}
	device = std::move(deviceResult.value);

	auto swapchainResult = device->CreateSwapchain({});
	if (!swapchainResult)
	{
		device.reset();
		window.reset();
		return Result<void>::Failure(swapchainResult.error);
	}
	swapchain = std::move(swapchainResult.value);

	auto guiResult =
	    ImGuiBridge::Create(device, window, workspace ? PathToUrsaString(workspace->ProjectRoot()) : String{});
	if (!guiResult)
	{
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(guiResult.error);
	}
	gui = std::move(guiResult.value);
	dockLayoutInitialized = gui->HasSavedLayout();
	auto materialPreviewResult = MaterialPreview::Create(device, *gui);
	if (!materialPreviewResult)
	{
		gui.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(materialPreviewResult.error);
	}
	materialPreview = std::move(materialPreviewResult.value);
	auto scenePanelResult = SceneViewPanel::Create(device, *gui);
	if (!scenePanelResult)
	{
		materialPreview.reset();
		gui.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(scenePanelResult.error);
	}
	sceneViewPanel = std::move(scenePanelResult.value);
	if (workspace && workspace->BuiltinDatabase())
	{
		AssetImportOptions options;
		const auto root = workspace->ProjectRoot();
		auto progress = importProgress;
		auto initialImport = std::async(
		    std::launch::async,
		    [root, options, progress]()
		    {
			    auto project = AssetImportPipeline::OpenProject(root);
			    return project
			               ? AssetImportPipeline::Prepare(*project.value, options,
			                                              [progress](UInt64 done, UInt64 total, const VirtualPath& path)
			                                              {
				                                              std::lock_guard<std::mutex> lock(progress->mutex);
				                                              progress->done = done;
				                                              progress->total = total;
				                                              progress->path = path.ToString();
			                                              })
			               : Result<AssetImportReport>::Failure(project.error);
		    });
		while (initialImport.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
		{
			window->PollEvents();
			if (window->ShouldClose())
			{
				initialImport.wait();
				return {};
			}
			if (!window->FramebufferWidth() || !window->FramebufferHeight())
			{
				window->WaitEvents();
				continue;
			}
			auto drawn = gui->BeginFrame(1.f / 60.f, window->Width(), window->Height(), window->FramebufferWidth(),
			                             window->FramebufferHeight());
			if (drawn)
			{
				ImGui::Begin("Importing project assets");
				std::lock_guard<std::mutex> lock(progress->mutex);
				ImGui::ProgressBar(
				    progress->total ? static_cast<float>(progress->done) / static_cast<float>(progress->total) : 0.f);
				ImGui::TextUnformatted(progress->path.c_str());
				ImGui::TextDisabled("Preparing resources and script content in project Library...");
				ImGui::End();
				drawn = gui->EndFrame();
			}
			if (drawn)
			{
				drawn = PresentGuiFrame();
			}
			if (!drawn)
			{
				initialImport.wait();
				return drawn;
			}
		}
		auto prepared = initialImport.get();
		if (prepared)
		{
			auto refreshed = workspace->Refresh();
			if (refreshed)
			{
				refreshed = workspace->Database()->ReloadImports();
			}
			if (refreshed)
			{
				refreshed = workspace->BuiltinDatabase()->ReloadImports();
			}
			if (!refreshed)
			{
				return refreshed;
			}
		}
		if (!prepared)
		{
			URSA_LOG_ERROR("Assets", prepared.error);
			workspaceError = prepared.error;
		}
		else
		{
			auto assigned = workspace->SetRuntimeContentView(prepared.value.runtimeFiles);
			if (!assigned)
			{
				return assigned;
			}
			ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(workspace->ContentFiles()));
#if defined(URSA_ENABLE_DOTNET)
			if (managedHost)
			{
				managedHost->SetFilesystem(workspace->ContentFiles());
			}
#endif
			auto refreshed = documents->RefreshResources();
			if (!refreshed)
			{
				return refreshed;
			}
		}
	}

	if (workspace && workspace->BuiltinDatabase())
	{
		auto signature = AssetImportPipeline::SourceFingerprint(*workspace);
		if (signature)
		{
			observedAssetSources = signature.value;
		}
		auto focused = window->Subscribe(
		    [this](const WindowEvent& event)
		    {
			    if (autoRefreshAssets && event.type == WindowEventType::Focused && event.focused)
			    {
				    nextAssetScan = {};
			    }
		    });
		if (focused)
		{
			assetFocusSubscription = focused.value;
		}
	}

	project = std::make_unique<EditorProject>();
	auto result = RegisterBuiltinComponents(project->components);
	if (result)
	{
		result = RegisterBuiltinComponentEditors(project->componentEditors);
	}
	if (result && projectSetup)
	{
		result = projectSetup(*project);
	}
	if (result && scriptBuildService)
	{
		auto scriptTypes = RegisterScriptComponentTypes();
		if (!scriptTypes)
		{
			scriptBuildStatus = scriptTypes.error;
		}
	}
	if (result)
	{
		for (const auto& descriptor : project->componentEditors.Types())
		{
			if (registeredScriptComponentTypes.count(descriptor.type))
			{
				continue;
			}
			const auto* codec = project->components.Find(descriptor.type);
			if (!codec || codec->version != descriptor.version)
			{
				result = Result<void>::Failure("Component type descriptor has no matching persistence type/version: " +
				                               descriptor.type);
				break;
			}
		}
	}
	if (result)
	{
		result = OnStart();
	}
	auto previous = std::chrono::steady_clock::now();
	Ursa::UInt32 frames = 0;
	while (result && !window->ShouldClose())
	{
		window->PollEvents();
		if (window->ShouldClose() && documents && documents->Current() && documents->Current()->Dirty() &&
		    !exitRequested)
		{
			exitRequested = true;
			auto closing = documents->RequestClose();
			if (!closing)
			{
				result = closing;
				break;
			}
			window->CancelClose();
		}
		if (exitRequested && documents && !documents->PromptPending())
		{
			if (documents->Current())
			{
				exitRequested = false;
			}
			else
			{
				window->RequestClose();
			}
		}
		if (window->ShouldClose())
		{
			break;
		}
		if (window->FramebufferWidth() == 0 || window->FramebufferHeight() == 0)
		{
			window->WaitEvents();
			previous = std::chrono::steady_clock::now();
			continue;
		}
		Memory::TemporaryScope frameMemory;
		URSA_PROFILE_SCOPE("Editor.Frame");
		FinishAssetJobs();

		const auto now = std::chrono::steady_clock::now();
		const Ursa::Float32 deltaSeconds = std::chrono::duration<Ursa::Float32>(now - previous).count();
		previous = now;
		{
			URSA_PROFILE_SCOPE("Editor.BeginFrame");
			result = gui->BeginFrame(deltaSeconds, window->Width(), window->Height(), window->FramebufferWidth(),
			                         window->FramebufferHeight());
		}
		if (result)
		{
			URSA_PROFILE_SCOPE("Editor.OnFrame");
			result = OnFrame(deltaSeconds);
		}
		if (result)
		{
			URSA_PROFILE_SCOPE("Editor.BuildGui");
			result = OnGui();
		}
		if (result && play.Active())
		{
			URSA_PROFILE_SCOPE("Editor.PlayModeUpdate");
			const FrameInput input =
			    SelectPlayInput(FrameInput::Capture(*window), viewportFocused, ImGui::GetIO().WantTextInput);
			auto ticked = play.Tick(deltaSeconds, window.get(), input);
			if (!ticked)
			{
				inspectorError = ticked.error;
				play.Stop();
				if (documents && documents->Current())
				{
					auto draft = documents->Current()->Payload<SceneDraft>();
					auto selected = draft && editSelectionBeforePlay
					                    ? draft->GetScene().GameObjectById(editSelectionBeforePlay)
					                    : Result<GameObject::Ptr>::Failure("No pre-play selection");
					if (selected)
					{
						hierarchy.Select(draft->GetScene(), selected.value);
					}
					else
					{
						hierarchy.ClearSelection();
					}
				}
				selectionContext = selectionBeforePlay;
				selectionBeforePlay = {};
				editSelectionBeforePlay = {};
				documents->SetSaveBlocked(false);
				viewportFocused = false;
			}
		}
		if (result && sceneViewPanel && documents && documents->Current())
		{
			auto draft = documents->Current()->Payload<SceneDraft>();
			if (draft)
			{
				auto rendered = sceneViewPanel->RenderPending(*draft, play);
				if (!rendered)
				{
					inspectorError = rendered.error;
				}
			}
		}
		if (result)
		{
			URSA_PROFILE_SCOPE("Editor.EndFrame");
			result = gui->EndFrame();
		}
		if (!result)
		{
			break;
		}

		{
			URSA_PROFILE_SCOPE("Editor.RenderPreparation");
			result = PresentGuiFrame();
			if (result)
			{
				URSA_PROFILE_FRAME("Editor");
			}
			if (frameLimit && ++frames >= frameLimit)
			{
				break;
			}
		}
	}

	const auto idle = device->WaitIdle();
	FinishScriptJobs();
	play.Stop();
	if (documents)
	{
		documents->SetSaveBlocked(false);
	}
	OnStop();
	if (textureImportView && gui)
	{
		textureImportView->Close(*gui);
	}
	textureImportView.reset();
	modelImportView.reset();
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost && editingScriptSession)
	{
		auto closed = managedHost->CloseScriptSession(editingScriptSession);
		if (!closed)
		{
			URSA_LOG_ERROR("Script", closed.error);
		}
		editingScriptSession = 0;
	}
#endif
	scriptBuildService.reset();
	scriptAssetDatabase.reset();
	documents.reset();
	workspace.reset();
	projectBrowser = ProjectBrowserModel{};
	selectionContext.Clear();
	hierarchy.ClearSelection();
	sceneViewPanel.reset();
	materialPreview.reset();
	if (assetImportJob.valid())
	{
		assetImportJob.wait();
	}
	if (assetScanJob.valid())
	{
		assetScanJob.wait();
	}
	if (assetFocusSubscription)
	{
		window->Unsubscribe(assetFocusSubscription);
		assetFocusSubscription = 0;
	}
	gui.reset();
	project.reset();
	swapchain.reset();
	device.reset();
	window.reset();
	return result ? idle : result;
}

Result<void> EditorApplication::PresentGuiFrame()
{
	Result<void> result;
	auto acquired = swapchain->Acquire();
	if (!acquired)
	{
		result = Result<void>::Failure(acquired.error);
		return result;
	}
	if (!acquired.value.texture)
	{
		return {};
	}

	auto command = device->CreateCommandList();
	if (!command)
	{
		result = Result<void>::Failure(command.error);
		return result;
	}
	result = command.value->Begin();
	if (result)
	{
		result = command.value->Barrier(acquired.value.texture, acquired.value.initialState,
		                                Rhi::ResourceState::ColorAttachment);
	}
	Rhi::RenderTarget target;
	target.color = acquired.value.texture;
	if (result)
	{
		result = command.value->BeginRendering(target);
	}
	if (result)
	{
		result = gui->Record(command.value, swapchain->Format(), acquired.value.texture->desc.width,
		                     acquired.value.texture->desc.height);
	}
	if (result)
	{
		result = command.value->EndRendering();
	}
	if (result)
	{
		result = command.value->Barrier(acquired.value.texture, Rhi::ResourceState::ColorAttachment,
		                                Rhi::ResourceState::Present);
	}
	if (result)
	{
		result = command.value->End();
	}
	Rhi::FencePtr submittedFence;
	if (result)
	{
		auto submitted = device->Submit(command.value, swapchain);
		if (!submitted)
		{
			result = Result<void>::Failure(submitted.error);
		}
		else
		{
			submittedFence = std::move(submitted.value);
		}
	}
	if (result)
	{
		result = swapchain->Present();
	}
	if (result)
	{
		result = submittedFence->Wait();
	}
	return result;
}

void EditorApplication::RequestAssetImport(const VirtualPath& scope, bool force)
{
	requestedImportScope = assetImportRequested && requestedImportScope != scope ? VirtualPath{} : scope;
	forceAssetImport = forceAssetImport || force;
	assetImportRequested = true;
	assetRequestDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

void EditorApplication::FinishAssetJobs()
{
	if (!workspace || !workspace->BuiltinDatabase())
	{
		return;
	}
	if (!play.Active() && assetImportJob.valid() &&
	    assetImportJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto result = assetImportJob.get();
		if (!result)
		{
			assetImportStatus = result.error;
			workspaceError = result.error;
			URSA_LOG_ERROR("Assets", result.error);
			if (result.error.find("during import") != String::npos ||
			    result.error.find("refresh and retry") != String::npos)
			{
				RequestAssetImport();
			}
		}
		else
		{
			auto ready = device->WaitIdle();
			if (ready)
			{
				ready = workspace->Refresh();
			}
			if (ready)
			{
				ready = workspace->Database()->ReloadImports();
			}
			if (ready)
			{
				ready = workspace->BuiltinDatabase()->ReloadImports();
			}
			if (ready)
			{
				ready = workspace->SetRuntimeContentView(result.value.runtimeFiles);
			}
			if (ready)
			{
				ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(workspace->ContentFiles()));
#if defined(URSA_ENABLE_DOTNET)
				if (managedHost)
				{
					managedHost->SetFilesystem(workspace->ContentFiles());
				}
#endif
				ready = documents->RefreshResources();
				if (ready)
				{
					ready = gui->ReloadShaders();
				}
			}
			if (ready && sceneViewPanel)
			{
				ready = sceneViewPanel->ReloadResources();
			}
			if (ready && materialPreview)
			{
				auto preview = MaterialPreview::Create(device, *gui);
				if (preview)
				{
					materialPreview = std::move(preview.value);
				}
				else
				{
					ready = Result<void>::Failure(preview.error);
				}
			}
#if defined(URSA_ENABLE_DOTNET)
			if (managedHost)
			{
				managedHost->SetFilesystem(workspace->ContentFiles());
			}
#endif
			assetImportStatus = ready ? String("Assets are up to date") : ready.error;
			workspaceError = ready ? String{} : ready.error;
			if (!ready)
			{
				URSA_LOG_ERROR("Assets", ready.error);
			}
			workspace->Refresh();
			if (textureImportView)
			{
				textureImportView->Reimport(*workspace->Database(), device, *gui, true);
			}
			if (modelImportView)
			{
				modelImportView->Reimport(*workspace->Database(), true);
			}
			if (shaderImportView)
			{
				shaderImportView->Reimport(*workspace, false);
			}
			auto signature = AssetImportPipeline::SourceFingerprint(*workspace);
			if (signature)
			{
				observedAssetSources = signature.value;
			}
			auto refreshed = projectBrowser.Refresh(*workspace);
			if (!refreshed)
			{
				workspaceError = refreshed.error;
			}
		}
	}
	if (assetImportJob.valid())
	{
		std::lock_guard<std::mutex> lock(importProgress->mutex);
		assetImportStatus = "Importing " + ToUrsaString(std::to_string(importProgress->done)) + "/" +
		                    ToUrsaString(std::to_string(importProgress->total)) + " " + importProgress->path;
	}
	if (assetScanJob.valid() && assetScanJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto signature = assetScanJob.get();
		if (signature && signature.value != observedAssetSources)
		{
			observedAssetSources = signature.value;
			RequestAssetImport();
		}
		else if (!signature)
		{
			assetImportStatus = signature.error;
		}
	}
	if (assetImportRequested && !assetImportJob.valid() && !ScriptWorkPending() && !play.Active() &&
	    std::chrono::steady_clock::now() >= assetRequestDeadline)
	{
		AssetImportOptions options;
		options.scope = requestedImportScope;
		options.force = forceAssetImport;
		forceAssetImport = false;
		const auto root = workspace->ProjectRoot();
		assetImportRequested = false;
		assetImportStatus = "Importing assets...";
		auto progress = importProgress;
		assetImportJob = std::async(std::launch::async,
		                            [root, options, progress]()
		                            {
			                            auto project = AssetImportPipeline::OpenProject(root);
			                            return project
			                                       ? AssetImportPipeline::Prepare(
			                                             *project.value, options,
			                                             [progress](UInt64 done, UInt64 total, const VirtualPath& path)
			                                             {
				                                             std::lock_guard<std::mutex> lock(progress->mutex);
				                                             progress->done = done;
				                                             progress->total = total;
				                                             progress->path = path.ToString();
			                                             })
			                                       : Result<AssetImportReport>::Failure(project.error);
		                            });
	}
	const auto now = std::chrono::steady_clock::now();
	if (autoRefreshAssets && !assetImportJob.valid() && !assetScanJob.valid() && now >= nextAssetScan)
	{
		nextAssetScan = now + std::chrono::seconds(2);
		VirtualFileSystem sources = workspace->Index().Filesystem();
		assetScanJob =
		    std::async(std::launch::async, [sources]() { return AssetImportPipeline::SourceFingerprint(sources); });
	}
}
} // namespace Ursa::Editor
