#include <Core/Types.hpp>
#include <Application/EditorApplication.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Assets/TextureDraft.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/SceneDraft.hpp>
#if defined(URSA_ENABLE_DOTNET)
#include <Scripting/DotNetHost.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/ManagedScriptRuntime.hpp>
#endif
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Log.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/PakArchive.hpp>
#include <imgui_internal.h>
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
	auto started = host.value->Start(workspace->Index().Filesystem(), files,
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
		return textureImportView->Reimport(*workspace->Database(), device, *gui);
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
		return modelImportView->Reimport(*workspace->Database());
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
		auto roots = ResolveEngineAssetRoots();
		if (!roots)
		{
			return Result<void>::Failure(roots.error);
		}
		profile.engineDirectories = std::move(roots.value);
#if defined(URSA_ENABLE_DOTNET)
		profile.engineDirectories.push_back(std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH).parent_path());
#endif
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
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(files.value));
	documents = std::move(documentsResult.value);
	workspace = std::move(openedWorkspace);
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost)
	{
		managedHost->SetFilesystem(workspace->Index().Filesystem());
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

Result<void> EditorApplication::OnGui()
{
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
	const auto checkTime = std::chrono::steady_clock::now();
	if (scriptBuildService && !requestRun && !play.Active() && !scriptBuildJob.valid() && !scriptCheckJob.valid() &&
	    checkTime >= nextScriptCheck)
	{
		ScriptBuildService* service = scriptBuildService.get();
		scriptCheckJob = std::async(std::launch::async, [service] { return service->SourceFingerprint(); });
		nextScriptCheck = checkTime + std::chrono::seconds(2);
	}
	if (ImGui::BeginMainMenuBar())
	{
		ImGui::TextUnformatted("Ursa Editor");
		ImGui::Separator();
		const auto* current = documents ? documents->Current() : nullptr;
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
			if (!root.empty() && root != "Tools" && root != "Scripts" &&
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
		const bool canRun = current && current->Kind() == ResourceKind::Scene && !documents->PromptPending() &&
		                    !scriptBuildJob.valid() && !scriptCheckJob.valid();
		if (requestRun && !canRun)
		{
			requestRun = false;
		}
		ImGui::BeginDisabled(!play.Active() && !canRun);
		if (ImGui::Button(play.Active() ? "Stop" : "Run") || requestRun)
		{
			requestRun = false;
			runAfterScriptBuild = false;
			if (play.Active())
			{
				play.Stop();
				documents->SetSaveBlocked(false);
				viewportFocused = false;
			}
			else
			{
				bool scriptsReady = true;
#if !defined(URSA_ENABLE_DOTNET)
				if (current && current->Payload<SceneDraft>() &&
				    HasManagedScriptComponent(current->Payload<SceneDraft>()->GetScene()))
				{
					scriptsReady = false;
					scriptBuildStatus =
					    "当前构建未启用 .NET 脚本，场景中的脚本数据已保留；启用脚本支持后才能进入 Run。";
				}
#endif
				PlayContextSetup playSetup = project->playContextSetup;
				if (scriptBuildService)
				{
					auto currentBuild = scriptBuildService->IsBuildCurrent(ScriptAssemblyGroup::Game);
					scriptsReady = currentBuild && currentBuild.value;
					if (!currentBuild)
					{
						scriptBuildStatus = currentBuild.error;
					}
					else if (!currentBuild.value)
					{
						ScriptBuildService* service = scriptBuildService.get();
						scriptBuildJob = std::async(std::launch::async,
						                            [service] { return service->Build(ScriptAssemblyGroup::Editor); });
						runAfterScriptBuild = true;
						pendingRunDocument = current->Id();
						scriptBuildStatus = "正在编译最新脚本，成功后进入 Run……";
					}
#if defined(URSA_ENABLE_DOTNET)
					if (scriptsReady && scriptAssetDatabase)
					{
						Vector<ScriptSourceAsset> sources = scriptAssetDatabase->ScriptSources();
						const bool hasScriptComponent =
						    current && current->Kind() == ResourceKind::Scene && current->Payload<SceneDraft>() &&
						    HasManagedScriptComponent(current->Payload<SceneDraft>()->GetScene());
						if (sources.empty() && hasScriptComponent)
						{
							scriptsReady = false;
							scriptBuildStatus = "当前场景包含脚本组件，但项目没有可用的 C# 脚本源码。";
						}
						else if (!sources.empty())
						{
							const VirtualPath gameAssemblyPath =
							    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Manifests/Game.json")
							        .value;
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
											    session->scriptRuntime =
											        std::make_unique<Scripting::ManagedScriptRuntime>(
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
				auto draft = scriptsReady ? current->Payload<SceneDraft>() : nullptr;
				if (draft)
				{
					auto started =
					    play.Start(*draft, current->Id(), project->components, documents->Session(), playSetup);
					inspectorError = started ? String{} : started.error;
					if (started)
					{
						documents->SetSaveBlocked(true);
					}
				}
			}
		}
		ImGui::EndDisabled();
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
		ImGui::Separator();
		if (workspace)
		{
			const auto name = PathToUrsaString(workspace->ProjectRoot().filename());
			ImGui::Text("Project: %s", name.c_str());
		}
		ImGui::EndMainMenuBar();
	}
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
	const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
	if (!dockLayoutInitialized)
	{
		ImGui::DockBuilderRemoveNode(dockspace);
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
		ImGuiID projectDock = 0;
		ImGuiID upperDock = 0;
		ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Down, 0.25f, &projectDock, &upperDock);
		ImGuiID hierarchyDock = 0;
		ImGuiID workspaceDock = 0;
		ImGui::DockBuilderSplitNode(upperDock, ImGuiDir_Left, 0.20f, &hierarchyDock, &workspaceDock);
		ImGuiID inspectorDock = 0;
		ImGuiID viewportDock = 0;
		ImGui::DockBuilderSplitNode(workspaceDock, ImGuiDir_Right, 0.30f, &inspectorDock, &viewportDock);
		ImGui::DockBuilderDockWindow("Project", projectDock);
		ImGui::DockBuilderDockWindow("Hierarchy", hierarchyDock);
		ImGui::DockBuilderDockWindow("Viewport", viewportDock);
		ImGui::DockBuilderDockWindow("Material Preview", viewportDock);
		ImGui::DockBuilderDockWindow("Inspector", inspectorDock);
		ImGui::DockBuilderFinish(dockspace);
		dockLayoutInitialized = true;
	}
	ImGui::Begin("Project");
	ImGui::BeginDisabled(play.Active());
	if (workspace)
	{
		const auto root = PathToUrsaString(workspace->ProjectRoot());
		ImGui::TextWrapped("Assets: %s", root.c_str());
	}
	if (!workspaceError.empty())
	{
		ImGui::TextWrapped("%s", workspaceError.c_str());
	}
	if (workspace)
	{
		if (ImGui::Button("Refresh assets"))
		{
			auto refreshed = workspace->Refresh();
			workspaceError = refreshed ? String{} : refreshed.error;
		}
		if (documents)
		{
			ImGui::InputText("Asset path", assetNameInput.data(), assetNameInput.size());
			if (documents->Current() && ImGui::Button("Rename current asset"))
			{
				auto path = ProjectAssetPath(assetNameInput.data());
				auto renamed = path ? documents->RenameCurrent(path.value) : Result<void>::Failure(path.error);
				workspaceError = renamed ? String{} : renamed.error;
			}
			if (ImGui::Button("New texture"))
			{
				auto target = ProjectAssetPath(assetNameInput.data());
				auto created = target ? NewTextureDocument(target.value)
				                      : Result<std::unique_ptr<EditorDocument>>::Failure(target.error);
				if (created)
				{
					auto opened = documents->RequestOpen(std::move(created.value));
					workspaceError = opened ? String{} : opened.error;
				}
				else
				{
					workspaceError = created.error;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("New Unlit"))
			{
				auto target = ProjectAssetPath(assetNameInput.data());
				auto created = target ? NewMaterialDocument(target.value, MaterialType::Unlit, *workspace)
				                      : Result<std::unique_ptr<EditorDocument>>::Failure(target.error);
				if (created)
				{
					auto opened = documents->RequestOpen(std::move(created.value));
					workspaceError = opened ? String{} : opened.error;
				}
				else
				{
					workspaceError = created.error;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("New PBR"))
			{
				auto target = ProjectAssetPath(assetNameInput.data());
				auto created = target
				                   ? NewMaterialDocument(target.value, MaterialType::PbrMetallicRoughness, *workspace)
				                   : Result<std::unique_ptr<EditorDocument>>::Failure(target.error);
				if (created)
				{
					auto opened = documents->RequestOpen(std::move(created.value));
					workspaceError = opened ? String{} : opened.error;
				}
				else
				{
					workspaceError = created.error;
				}
			}
			ImGui::SameLine();
			const bool newScene = ImGui::Button("New Scene");
			ImGui::SameLine();
			const bool newPrefab = ImGui::Button("New Prefab");
			if (newScene || newPrefab)
			{
				const ResourceKind kind = newScene ? ResourceKind::Scene : ResourceKind::Prefab;
				auto target = ProjectAssetPath(assetNameInput.data());
				auto created = target ? NewSceneDocument(target.value, kind, project->components, documents->Session(),
				                                         project->editContextSetup)
				                      : Result<std::unique_ptr<EditorDocument>>::Failure(target.error);
				if (created)
				{
					auto opened = documents->RequestOpen(std::move(created.value));
					workspaceError = opened ? String{} : opened.error;
				}
				else
				{
					workspaceError = created.error;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Save") && documents->Current())
			{
				auto saved = documents->SaveCurrent();
				workspaceError = saved ? String{} : saved.error;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close document"))
			{
				auto closed = documents->RequestClose();
				workspaceError = closed ? String{} : closed.error;
			}
			if (documents->Current())
			{
				ImGui::Text("Open: %s%s", documents->Current()->Path().ToString().c_str(),
				            documents->Current()->Dirty() ? " *" : "");
			}
		}
		for (const auto& entry : workspace->Resources())
		{
			const bool writable = static_cast<bool>(workspace->Index().Filesystem().CheckWritable(entry.location.path));
			ImGui::PushID(entry.id.ToString().c_str());
			ImGui::TextUnformatted(writable ? "[Project]" : "[Engine]");
			ImGui::SameLine();
			const auto name = AssetDisplayPath(entry.location.path);
			if (ImGui::Selectable(name.c_str()) && documents)
			{
				if (entry.location.kind == ResourceKind::Texture)
				{
					auto opened = OpenTextureDocument(entry.location.path, workspace->Index());
					if (opened)
					{
						auto switched = documents->RequestOpen(std::move(opened.value));
						workspaceError = switched ? String{} : switched.error;
					}
					else
					{
						workspaceError = opened.error;
					}
				}
				else if (entry.location.kind == ResourceKind::Material)
				{
					auto opened = OpenMaterialDocument(entry.location.path, documents->Session(), *workspace);
					if (opened)
					{
						auto switched = documents->RequestOpen(std::move(opened.value));
						workspaceError = switched ? String{} : switched.error;
					}
					else
					{
						workspaceError = opened.error;
					}
				}
				else if (entry.location.kind == ResourceKind::Scene || entry.location.kind == ResourceKind::Prefab)
				{
					auto opened = OpenSceneDocument(entry.location.path, entry.location.kind, project->components,
					                                documents->Session(), project->editContextSetup);
					if (opened)
					{
						auto switched = documents->RequestOpen(std::move(opened.value));
						workspaceError = switched ? String{} : switched.error;
					}
					else
					{
						workspaceError = opened.error;
					}
				}
				else
				{
					workspaceError = "This asset type is not yet editable";
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%s", entry.id.ToString().c_str());
			ImGui::PopID();
		}
		for (const AssetRecord& asset : workspace->Assets())
		{
			if (!asset.metadata.folder &&
			    (std::filesystem::u8path(ToStdString(asset.path.relative)).extension() == ".ursashader" ||
			     ResourceKindForPath(asset.path)))
			{
				continue;
			}
			const String pathText = AssetDisplayPath(asset.path);
			const String itemId = asset.metadata.guid.ToString();
			ImGui::PushID(itemId.c_str());
			ImGui::TextUnformatted(asset.metadata.folder ? "[Project Folder]" : "[Project Source]");
			ImGui::SameLine();
			if (ImGui::Selectable(pathText.c_str()) && !asset.metadata.folder)
			{
				if (IsTextureSource(asset.path) && workspace->Database())
				{
					auto settings = TextureImporter::Settings(*workspace->Database(), asset.path);
					auto opened = settings ? TextureImportView::Open(asset.path, settings.value)
					                       : Result<std::unique_ptr<TextureImportView>>::Failure(settings.error);
					if (opened)
					{
						if (textureImportView && gui)
						{
							textureImportView->Close(*gui);
						}
						textureImportView = std::move(opened.value);
						auto imported = textureImportView->Reimport(*workspace->Database(), device, *gui);
						workspaceError = imported ? String{} : imported.error;
					}
					else
					{
						workspaceError = opened.error;
					}
				}
				else if (IsModelSource(asset.path) && workspace->Database())
				{
					auto settings = ModelImporter::Settings(*workspace->Database(), asset.path);
					auto opened = settings ? ModelImportView::Open(asset.path, settings.value)
					                       : Result<std::unique_ptr<ModelImportView>>::Failure(settings.error);
					if (opened)
					{
						modelImportView = std::move(opened.value);
						auto imported = modelImportView->Reimport(*workspace->Database());
						workspaceError = imported ? String{} : imported.error;
					}
					else
					{
						workspaceError = opened.error;
					}
				}
				else
				{
					workspaceError = "No importer is registered for " + pathText;
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%s", itemId.c_str());
			ImGui::PopID();
		}
		for (const String& scheme : {String(projectResourceScheme), String(engineResourceScheme)})
		{
			if (!workspace->Index().Filesystem().IsMounted(scheme))
			{
				continue;
			}
			auto files = workspace->Index().Filesystem().List(scheme);
			if (!files)
			{
				workspaceError = files.error;
				continue;
			}
			for (const VirtualFileEntry& file : files.value)
			{
				if (std::filesystem::u8path(ToStdString(file.path.relative)).extension() != ".ursashader")
				{
					continue;
				}
				const String pathText = AssetDisplayPath(file.path);
				ImGui::PushID(pathText.c_str());
				ImGui::TextUnformatted(file.path.scheme == projectResourceScheme ? "[Project Shader Source]"
				                                                                 : "[Engine Shader Source]");
				ImGui::SameLine();
				if (ImGui::Selectable(pathText.c_str()))
				{
					auto opened = ShaderImportView::Open(workspace->Index().Filesystem(), file.path);
					if (opened)
					{
						shaderImportView = std::move(opened.value);
						inspectorError.clear();
					}
					else
					{
						workspaceError = opened.error;
					}
				}
				ImGui::PopID();
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::End();
	if (shaderImportView && workspace)
	{
		ImGui::Begin("Shader Import");
		ImGui::TextUnformatted(shaderImportView->SourcePath().ToString().c_str());
		ImGui::TextDisabled("%s", shaderImportView->SourceReadOnly() ? "Read-only source" : "Writable project source");
		ImGui::BeginDisabled(play.Active());
		if (ImGui::Button("Reimport Shader"))
		{
			auto imported = shaderImportView->Reimport(*workspace);
			if (!imported)
			{
				inspectorError = imported.error;
			}
			else
			{
				inspectorError.clear();
			}
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
		auto drawn = textureImportView->Draw(*workspace->Database(), device, *gui, !play.Active());
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
		auto drawn = modelImportView->Draw(*workspace->Database(), !play.Active(), instantiate);
		if (!drawn)
		{
			inspectorError = drawn.error;
		}
	}
	if (documents && documents->Current() && documents->Current()->Kind() == ResourceKind::Texture)
	{
		auto inspected = DrawTextureInspector(*documents->Current(), inspectorError);
		if (!inspected)
		{
			workspaceError = inspected.error;
		}
	}
	if (documents && documents->Current() && documents->Current()->Kind() == ResourceKind::Material)
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
			displayedSceneId = documents->Current()->Id();
		}
		auto draft = documents->Current()->Payload<SceneDraft>();
		if (draft)
		{
			ImGui::Begin("Viewport");
			if (ImGui::Button("Translate"))
			{
				gizmoOperation = GizmoOperation::Translate;
			}
			ImGui::SameLine();
			if (ImGui::Button("Rotate"))
			{
				gizmoOperation = GizmoOperation::Rotate;
			}
			ImGui::SameLine();
			if (ImGui::Button("Scale"))
			{
				gizmoOperation = GizmoOperation::Scale;
			}
			ImGui::End();
			if (play.Active())
			{
				ImGui::BeginDisabled();
				auto hierarchyResult = hierarchy.Draw(*play.GetScene());
				ImGui::EndDisabled();
				if (!hierarchyResult)
				{
					inspectorError = hierarchyResult.error;
				}
				ImGui::Begin("Inspector");
				ImGui::TextUnformatted("Play mode: scene changes are temporary");
				ImGui::End();
			}
			else
			{
				auto inspected = DrawSceneInspector(*documents->Current(), hierarchy, *documents, *workspace,
				                                    project->components, project->componentEditors, inspectorError,
				                                    scriptBuildService ? &scriptBuildService->ScriptTypes() : nullptr);
				if (!inspected)
				{
					workspaceError = inspected.error;
				}
			}
			auto command = device->CreateCommandList();
			Result<void> previewResult = command ? command.value->Begin() : Result<void>::Failure(command.error);
			if (previewResult)
			{
				const auto initial =
				    scenePreviewRendered ? Rhi::ResourceState::ShaderRead : Rhi::ResourceState::Undefined;
				previewResult = play.Active()
				                    ? scenePreview->RecordGame(*play.GetScene(), command.value, scenePreviewColor,
				                                               initial, Rhi::ResourceState::ShaderRead)
				                    : scenePreview->Record(draft->GetScene(), command.value, scenePreviewColor, initial,
				                                           Rhi::ResourceState::ShaderRead);
			}
			if (previewResult)
			{
				previewResult = command.value->End();
			}
			if (previewResult)
			{
				auto submitted = device->Submit(command.value);
				previewResult = submitted ? submitted.value->Wait() : Result<void>::Failure(submitted.error);
			}
			if (previewResult)
			{
				scenePreviewRendered = true;
			}
			else
			{
				inspectorError = previewResult.error;
			}
			ImGui::Begin("Viewport");
			if (previewResult)
			{
				const ImVec2 available = ImGui::GetContentRegionAvail();
				const Ursa::Float32 imageSize = std::max(1.f, std::min(available.x, available.y));
				const ImVec2 position = ImGui::GetCursorScreenPos();
				ImGui::Image(scenePreviewId, ImVec2(imageSize, imageSize));
				viewportFocused = play.Active() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				                  ImGui::IsItemHovered();
				GameObject::Ptr gizmoTarget;
				ResourceId prefabObjectId;
				if (draft->Kind() == ResourceKind::Prefab && draft->GetPrefabAsset())
				{
					auto selectedAssetObject = hierarchy.SelectedGameObject(*draft->GetPrefabAsset());
					if (selectedAssetObject)
					{
						prefabObjectId = selectedAssetObject.value->Id();
						auto selectedPreviewObject = draft->PreviewGameObject(prefabObjectId);
						if (selectedPreviewObject)
						{
							gizmoTarget = selectedPreviewObject.value;
						}
					}
				}
				else
				{
					auto selected = hierarchy.SelectedGameObject(draft->GetScene());
					if (selected)
					{
						gizmoTarget = selected.value;
					}
				}
				if (gizmoTarget && !play.Active())
				{
					auto manipulated = DrawGizmo(draft->GetScene(), gizmoTarget, scenePreview->GetCamera(),
					                             {position.x, position.y, imageSize, imageSize}, gizmoOperation);
					if (manipulated && manipulated.value)
					{
						if (prefabObjectId)
						{
							auto synced = draft->SyncPrefabTransform(prefabObjectId);
							if (synced)
							{
								synced = draft->RefreshPrefabPreview();
							}
							if (!synced)
							{
								inspectorError = synced.error;
							}
						}
						documents->Current()->MarkDirty();
					}
					else if (!manipulated)
					{
						inspectorError = manipulated.error;
					}
				}
			}
			ImGui::End();
		}
	}
	else
	{
		displayedSceneId = {};
	}
	const bool sceneOpen =
	    documents && documents->Current() &&
	    (documents->Current()->Kind() == ResourceKind::Scene || documents->Current()->Kind() == ResourceKind::Prefab);
	if (!sceneOpen)
	{
		ImGui::Begin("Hierarchy");
		ImGui::TextDisabled("Open a scene or Prefab from Project.");
		ImGui::End();
		ImGui::Begin("Viewport");
		ImGui::TextDisabled("Open a scene to view and edit it.");
		ImGui::End();
	}
	const bool assetInspectorOpen = documents && documents->Current() &&
	                                    (documents->Current()->Kind() == ResourceKind::Material ||
	                                     documents->Current()->Kind() == ResourceKind::Texture) ||
	                                modelImportView != nullptr;
	if (!sceneOpen && !assetInspectorOpen)
	{
		ImGui::Begin("Inspector");
		ImGui::TextDisabled("Select a node in Hierarchy to edit its properties.");
		ImGui::End();
	}
	if (documents)
	{
		documents->DrawUnsavedPrompt();
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
	if (!assetNameInput[0])
	{
		std::snprintf(assetNameInput.data(), assetNameInput.size(), "%s", "texture.ursatex");
	}
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

	auto guiResult = ImGuiBridge::Create(device, window);
	if (!guiResult)
	{
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(guiResult.error);
	}
	gui = std::move(guiResult.value);
	dockLayoutInitialized = gui->HasSavedLayout();
	dockLayoutInitialized = false;
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
	auto scenePreviewResult = EditorScenePreview::Create(device);
	if (!scenePreviewResult)
	{
		materialPreview.reset();
		gui.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(scenePreviewResult.error);
	}
	scenePreview = std::move(scenePreviewResult.value);
	auto sceneColorResult = device->CreateTexture(
	    {512, 512, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled});
	auto sceneSamplerResult = device->CreateSampler({true, false});
	if (!sceneColorResult || !sceneSamplerResult)
	{
		const String error = !sceneColorResult ? sceneColorResult.error : sceneSamplerResult.error;
		scenePreview.reset();
		materialPreview.reset();
		gui.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(error);
	}
	scenePreviewColor = std::move(sceneColorResult.value);
	scenePreviewSampler = std::move(sceneSamplerResult.value);
	auto sceneImageResult = gui->RegisterTexture(scenePreviewColor, scenePreviewSampler);
	if (!sceneImageResult)
	{
		const String error = sceneImageResult.error;
		scenePreviewColor.reset();
		scenePreviewSampler.reset();
		scenePreview.reset();
		materialPreview.reset();
		gui.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		return Result<void>::Failure(error);
	}
	scenePreviewId = sceneImageResult.value;

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

		const auto now = std::chrono::steady_clock::now();
		const Ursa::Float32 deltaSeconds = std::chrono::duration<Ursa::Float32>(now - previous).count();
		previous = now;
		result = gui->BeginFrame(deltaSeconds, window->FramebufferWidth(), window->FramebufferHeight());
		if (result)
		{
			result = OnFrame(deltaSeconds);
		}
		if (result && play.Active())
		{
			const FrameInput input =
			    SelectPlayInput(FrameInput::Capture(*window), viewportFocused, ImGui::GetIO().WantTextInput);
			auto ticked = play.Tick(deltaSeconds, window.get(), input);
			if (!ticked)
			{
				inspectorError = ticked.error;
				play.Stop();
				documents->SetSaveBlocked(false);
				viewportFocused = false;
			}
		}
		if (result)
		{
			result = OnGui();
		}
		if (result)
		{
			result = gui->EndFrame();
		}
		if (!result)
		{
			break;
		}

		auto acquired = swapchain->Acquire();
		if (!acquired)
		{
			result = Result<void>::Failure(acquired.error);
			break;
		}
		if (!acquired.value.texture)
		{
			continue;
		}

		auto command = device->CreateCommandList();
		if (!command)
		{
			result = Result<void>::Failure(command.error);
			break;
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
		if (frameLimit && ++frames >= frameLimit)
		{
			break;
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
	if (scenePreviewId != ImTextureID_Invalid)
	{
		gui->UnregisterTexture(scenePreviewId);
	}
	scenePreviewId = ImTextureID_Invalid;
	scenePreviewColor.reset();
	scenePreviewSampler.reset();
	scenePreview.reset();
	materialPreview.reset();
	gui.reset();
	project.reset();
	swapchain.reset();
	device.reset();
	window.reset();
	return result ? idle : result;
}
} // namespace Ursa::Editor
