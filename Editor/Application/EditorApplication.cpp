#include <Ui/EditorIcons.hpp>
#include <Core/Types.hpp>
#include <Application/EditorApplication.hpp>
#include <EditorIcon.hpp>
#include <stb_image.h>
#include <Assets/AssetImportPipeline.hpp>
#include <Assets/ModelImporter.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Ui/ImGuiInspectorUI.hpp>
#include <Assets/TextureDraft.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/SceneDraft.hpp>
#include <Mesh/Primitive.hpp>
#include <Scene/Lighting.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Inspector/ReferencePicker.hpp>
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
#include <filesystem>
#include <fstream>
#include <limits>
#include <cstdio>

namespace Ursa::Editor
{
namespace
{
Float32 EditorToolbarHeight(Float32 width)
{
	return width < 180.f ? 112.f : width < 300.f ? 76.f : 44.f;
}

#if defined(URSA_ENABLE_DOTNET)
struct ManagedEditingSessionState
{
	std::unique_ptr<ResourceSession> resources;
	Scene assetScene;
	std::unique_ptr<Scripting::ManagedSceneApi> sceneApi;
};

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
		if (host && sessionId)
		{
			auto closed = host->CloseScriptSession(sessionId);
			if (!closed)
			{
				URSA_LOG_ERROR("Script", closed.error);
			}
		}
		sceneApi.reset();
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
	return ModelImporter::IsSupportedSource(path);
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
	Icons::SetPanelResetAction({});
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
	assetPreview.reset();
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost && editingScriptSession)
	{
		auto closed = managedHost->CloseScriptSession(editingScriptSession);
		if (!closed)
		{
			URSA_LOG_ERROR("Script", closed.error);
		}
	}
	editingScriptServices.reset();
#endif
}

void EditorApplication::FinishScriptJobs()
{
	if (scriptIdeJob.valid())
	{
		scriptIdeJob.wait();
		scriptIdeJob = {};
	}
	if (scriptEditorJob.valid())
	{
		scriptEditorJob.wait();
		scriptEditorJob = {};
	}
	if (scriptEditorDiscoveryJob.valid())
	{
		scriptEditorDiscoveryJob.wait();
		scriptEditorDiscoveryJob = {};
	}
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
	                      path("ursa:Ursa.Editor.dll"), path("ursa:Ursa.Rendering.dll"),
	                      path("ursa:Ursa.Bindings.dll")};
	auto started = host.value->Connect(workspace->ContentFiles(), files,
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

Result<bool> EditorApplication::DrawAssetExtension(ManagedEditorContext& context)
{
	if (!context.openAsset)
	{
		context.openAsset = [this](const VirtualPath& path)
		{
			pendingManagedAsset = path;
			return Result<void>{};
		};
	}
	const String target =
	    context.importerInspector ? "importer:" + context.importerInspector->Identity() : context.targetIdentity;
	const auto found =
	    std::find_if(managedExtensions.begin(), managedExtensions.end(), [&](const auto& extension)
	                 { return extension.kind == ManagedExtensionKind::Inspector && extension.target == target; });
	if (found == managedExtensions.end())
	{
		return Result<bool>::Success(false);
	}
#if defined(URSA_ENABLE_DOTNET)
	if (!managedHost || !editingScriptSession || managedHost->IsSessionFaulted())
	{
		return Result<bool>::Failure("The registered asset Inspector's editing session is unavailable");
	}
	ConfigureAssetPreview(context);
	if (assetPreview)
	{
		const auto drawn = assetPreview->DrawExtensionInspector(found->id, context);
		if (drawn)
		{
			return *drawn;
		}
	}
	const auto invoked = InvokeManagedEditorExtension(*managedHost, editingScriptSession, found->id, context);
	return invoked ? Result<bool>::Success(true) : Result<bool>::Failure(invoked.error);
#else
	return Result<bool>::Failure("The registered asset Inspector requires .NET support");
#endif
}

void EditorApplication::ConfigureAssetPreview(ManagedEditorContext& context)
{
	ConfigureReferencePreviewFactory(context);
#if defined(URSA_ENABLE_DOTNET)
	if (!assetPreview || !managedHost || !editingScriptSession || managedHost->IsSessionFaulted())
	{
		return;
	}
	const String target =
	    context.importerInspector ? "importer:" + context.importerInspector->Identity() : context.targetIdentity;
	const auto found = std::find_if(managedExtensions.begin(), managedExtensions.end(), [&](const auto& item)
	                                { return item.kind == ManagedExtensionKind::Inspector && item.target == target; });
	if (found == managedExtensions.end())
	{
		return;
	}
	const std::weak_ptr<Scripting::DotNetHost> weakHost = managedHost;
	const auto session = editingScriptSession;
	const auto id = found->id;
	assetPreview->ConfigureExtension(
	    id,
	    [weakHost, session, id](UInt64 instance, Scripting::ManagedPreviewPhase phase, const ViewportRect& rect,
	                            ManagedEditorContext& previewContext)
	    {
		    const auto host = weakHost.lock();
		    return host ? InvokeManagedEditorPreview(*host, session, id, instance, phase, rect, previewContext)
		                : Result<Scripting::ManagedPreviewResult>::Failure("Preview script environment is closed");
	    },
	    context);
#else
	(void)context;
#endif
}

void EditorApplication::ConfigureReferencePreviewFactory(ManagedEditorContext& context)
{
#if defined(URSA_ENABLE_DOTNET)
	context.createReferencePreview =
	    [this](const AssetPreviewTarget& target) -> Result<std::shared_ptr<AssetPreviewExtension>>
	{
		if (!managedHost || !editingScriptSession || managedHost->IsSessionFaulted() || !workspace)
		{
			return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(
			    "Managed Preview environment is unavailable");
		}
		String targetIdentity;
		if (target.custom && !target.customTypeId.empty())
		{
			targetIdentity = "asset:" + target.customTypeId;
		}
		else
		{
			switch (target.kind)
			{
				case AssetPreviewKind::Material:
					targetIdentity = "asset:ursa.Material";
					break;
				case AssetPreviewKind::Texture:
					targetIdentity = "asset:ursa.Texture";
					break;
				case AssetPreviewKind::Mesh:
					targetIdentity = "asset:ursa.Mesh";
					break;
				case AssetPreviewKind::Prefab:
					targetIdentity = "asset:ursa.Prefab";
					break;
				case AssetPreviewKind::Model:
					targetIdentity = "asset:ursa.Model";
					break;
				default:
					return Result<std::shared_ptr<AssetPreviewExtension>>::Success({});
			}
		}
		const auto descriptor =
		    std::find_if(managedExtensions.begin(), managedExtensions.end(), [&](const auto& item)
		                 { return item.kind == ManagedExtensionKind::Inspector && item.target == targetIdentity; });
		if (descriptor == managedExtensions.end())
		{
			return Result<std::shared_ptr<AssetPreviewExtension>>::Success({});
		}
		auto openedResources = ResourceSession::Open(workspace->ContentFiles(), workspace->AssetResolver());
		if (!openedResources)
		{
			return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(openedResources.error);
		}
		auto resources = std::shared_ptr<ResourceSession>(std::move(openedResources.value));
		ManagedEditorContext previewContext;
		previewContext.workspace = workspace.get();
		previewContext.registry = project ? &project->components : nullptr;
		previewContext.targetUsesDocument = false;
		previewContext.canEditTarget = false;
		previewContext.assetSelection = target.identity;
		previewContext.targetIdentity = targetIdentity;
		switch (target.kind)
		{
			case AssetPreviewKind::Material:
			{
				auto loaded = resources->LoadMaterial(target.identity.guid, target.identity.localFileId);
				if (!loaded)
				{
					return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(loaded.error);
				}
				previewContext.targetMaterial = std::move(loaded.value);
				break;
			}
			case AssetPreviewKind::Texture:
			{
				auto loaded = resources->LoadTexture(target.identity.guid, target.identity.localFileId);
				if (!loaded)
				{
					return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(loaded.error);
				}
				previewContext.targetTexture = std::move(loaded.value);
				break;
			}
			case AssetPreviewKind::Mesh:
			{
				auto loaded = resources->LoadMesh(target.identity.guid, target.identity.localFileId);
				if (!loaded)
				{
					return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(loaded.error);
				}
				previewContext.targetMesh = std::move(loaded.value);
				break;
			}
			default:
				break;
		}
		if (target.custom && !target.customTypeId.empty())
		{
			nlohmann::json payload{{"Fields", nlohmann::json::object()}, {"FieldTypes", nlohmann::json::object()}};
			if (PathFromUrsaString(target.path.relative).extension() == ".ursaasset")
			{
				auto asset =
				    LoadScriptableAssetSource(workspace->Index().Filesystem(), target.path, target.identity.guid);
				if (!asset)
				{
					return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(asset.error);
				}
				previewContext.assetSelection = AssetReference{asset.value.SourceGuid(), asset.value.LocalFileId()};
				previewContext.targetIdentity = "asset:" + asset.value.TypeId();
				payload = asset.value.Payload();
			}
			else
			{
				auto graph = LoadScriptableAssetGraph(*resources, target.identity);
				if (!graph)
				{
					return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(graph.error);
				}
				const auto found = std::find_if(graph.value.assets.begin(), graph.value.assets.end(),
				                                [&](const auto& item)
				                                {
					                                return item.SourceGuid() == graph.value.root.guid &&
					                                       item.LocalFileId() == graph.value.root.localFileId;
				                                });
				if (found == graph.value.assets.end())
				{
					return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(
					    "The custom Preview root asset is missing from its graph");
				}
				previewContext.assetSelection = graph.value.root;
				previewContext.targetIdentity = "asset:" + found->TypeId();
				payload = found->Payload();
			}
			SerializedTargetAdapter adapter;
			adapter.resources = resources.get();
			adapter.capture = [payload] { return Result<nlohmann::json>::Success(payload); };
			previewContext.serializedTarget = std::move(adapter);
		}
		const String extensionId = descriptor->id;
		const std::weak_ptr<Scripting::DotNetHost> weakHost = managedHost;
		const UInt64 scriptSession = editingScriptSession;
		auto extension = std::make_shared<AssetPreviewExtension>(
		    extensionId,
		    [weakHost, scriptSession, extensionId, resources](UInt64 instance, Scripting::ManagedPreviewPhase phase,
		                                                      const ViewportRect& rect, ManagedEditorContext& source)
		    {
			    const auto host = weakHost.lock();
			    return host ? InvokeManagedEditorPreview(*host, scriptSession, extensionId, instance, phase, rect,
			                                             source)
			                : Result<Scripting::ManagedPreviewResult>::Failure("Preview script environment is closed");
		    });
		extension->Bind(previewContext);
		auto query = extension->Invoke(Scripting::ManagedPreviewPhase::Query);
		if (!query)
		{
			return Result<std::shared_ptr<AssetPreviewExtension>>::Failure(query.error);
		}
		if (!query.value.hasPreview)
		{
			return Result<std::shared_ptr<AssetPreviewExtension>>::Success({});
		}
		return Result<std::shared_ptr<AssetPreviewExtension>>::Success(std::move(extension));
	};
#else
	(void)context;
#endif
}

bool EditorApplication::BeforeImporterSelection(const EditorSelectionContext& next)
{
	if (HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
	{
		pendingPickerSelection = next;
		RequestCancelReferencePickerSessions(gui ? gui->Context() : nullptr);
		return false;
	}
	const bool sourceModified = assetInspectorPanel.HasModifiedSource();
	const auto dirtyPath = sourceModified ? assetInspectorPanel.ModifiedSourcePath()
	                       : assetImporterInspector && assetImporterInspector->HasModified()
	                           ? assetImporterInspector->SourcePath()
	                           : VirtualPath{};
	const auto* current = selectionContext.PrimaryEntry();
	const auto* nextPrimary = next.PrimaryEntry();
	const bool keepsDirtyTarget = sourceModified
	                                  ? assetInspectorPanel.RetainsModifiedTargets(next)
	                                  : current && nextPrimary && current->kind == EditorSelectionKind::Asset &&
	                                        nextPrimary->SameIdentity(*current) && nextPrimary->assetPath == dirtyPath;
	if (dirtyPath.scheme.empty() || keepsDirtyTarget)
	{
		return true;
	}
	if (!pendingImporterSelection)
	{
		importerSelectionPopup = true;
	}
	pendingImporterSelection = next;
	pendingSourceAsset = assetInspectorPanel.HasModifiedSource();
	return false;
}

void EditorApplication::DrawImporterSelectionPrompt()
{
	if (importerSelectionPopup)
	{
		ImGui::OpenPopup(pendingSourceAsset ? "Unsaved Asset" : "Unapplied Import Settings");
		importerSelectionPopup = false;
	}
	if (!pendingImporterSelection)
	{
		return;
	}
	const auto request = [this](const VirtualPath& path) { RequestAssetImport(path, true); };
	const bool canSave = !play.Active() && !AssetWorkPending() && !ScriptWorkPending();
	const auto resolution = pendingSourceAsset ? assetInspectorPanel.DrawSourceSelectionPrompt(request, canSave)
	                                           : assetImporterInspector->DrawSelectionPrompt(request, canSave);
	if (resolution == ImporterSelectionResult::Cancelled)
	{
		pendingImporterSelection.reset();
		pendingSourceAsset = false;
		pendingImporterOpen = {};
		exitAfterInspectorPrompt = false;
	}
	const bool accept = resolution == ImporterSelectionResult::Accepted;
	if (accept && pendingImporterSelection)
	{
		const auto next = std::move(*pendingImporterSelection);
		pendingImporterSelection.reset();
		pendingSourceAsset = false;
		assetImporterInspector.reset();
		if (!selectionContext.SelectEntries(next.entries, next.primaryIndex, next.rangeAnchor))
		{
			workspaceError = "The pending selection is no longer valid after resolving the asset draft.";
			return;
		}
		if (next.kind == EditorSelectionKind::Asset)
		{
			projectBrowser.SelectAsset(next.assetPath);
			hierarchy.ClearSelection();
		}
		else if (next.kind == EditorSelectionKind::GameObject)
		{
			if (documents && documents->Current() && documents->Current()->Id() == next.documentId)
			{
				if (const auto draft = documents->Current()->Payload<SceneDraft>())
				{
					if (draft->Kind() == ResourceKind::Prefab && draft->GetPrefabAsset())
					{
						Vector<GameObject::Ptr> pending{draft->GetPrefabAsset()->Root()};
						while (!pending.empty())
						{
							auto object = std::move(pending.back());
							pending.pop_back();
							if (object->Id() == next.objectId)
							{
								hierarchy.Select(*draft->GetPrefabAsset(), object);
								break;
							}
							for (const auto& child : object->Children())
							{
								pending.push_back(child);
							}
						}
					}
					else if (const auto object = draft->GetScene().GameObjectById(next.objectId))
					{
						hierarchy.Select(draft->GetScene(), object.value);
					}
				}
			}
		}
		else
		{
			hierarchy.ClearSelection();
		}
		if (!pendingImporterOpen.scheme.empty())
		{
			pendingManagedAsset = std::move(pendingImporterOpen);
			pendingImporterOpen = {};
		}
		if (exitAfterInspectorPrompt)
		{
			exitAfterInspectorPrompt = false;
			exitRequested = true;
			const auto closing = documents->RequestClose();
			if (!closing)
			{
				workspaceError = closing.error;
			}
		}
	}
}

Result<void> EditorApplication::ReloadEditorScripts()
{
#if defined(URSA_ENABLE_DOTNET)
	if (HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
	{
		RequestCancelReferencePickerSessions(gui ? gui->Context() : nullptr,
		                                     [this]
		                                     {
			                                     auto reloaded = ReloadEditorScripts();
			                                     if (!reloaded)
			                                     {
				                                     scriptBuildStatus = reloaded.error;
			                                     }
		                                     });
		return {};
	}
	if (play.Active())
	{
		return Result<void>::Failure("Stop before reloading editing scripts");
	}
	auto ready = EnsureManagedHost();
	if (!ready)
	{
		return ready;
	}
	auto candidateServices = std::make_shared<ManagedEditingSessionState>();
	auto candidateResources = ResourceSession::Open(workspace->ContentFiles(), workspace->AssetResolver());
	if (!candidateResources)
	{
		return Result<void>::Failure(candidateResources.error);
	}
	candidateServices->resources = std::move(candidateResources.value);
	candidateServices->sceneApi =
	    std::make_unique<Scripting::ManagedSceneApi>(candidateServices->assetScene, candidateServices->resources.get());
	candidateServices->sceneApi->SetRenderDevice(device);
	auto candidate = managedHost->CreateScriptSession(true, candidateServices->sceneApi.get());
	if (!candidate)
	{
		return Result<void>::Failure(candidate.error);
	}
	const auto abandon = [&]()
	{
		auto closed = managedHost->CloseScriptSession(candidate.value);
		if (!closed)
		{
			URSA_LOG_ERROR("Script", closed.error);
		}
		if (editingScriptSession)
		{
			managedHost->SelectScriptSession(editingScriptSession);
		}
		return closed;
	};
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
	if (editingScriptSession)
	{
		auto migrated = managedHost->TransferScriptableAssets(editingScriptSession, candidate.value);
		if (!migrated)
		{
			abandon();
			return migrated;
		}
	}
	auto graphicsSettings = Rendering::ReadGraphicsSettings(workspace->Index().Filesystem());
	if (!graphicsSettings)
	{
		abandon();
		return Result<void>::Failure(graphicsSettings.error);
	}
	if (graphicsSettings.value.renderPipeline.guid)
	{
		if (!scriptBuildService)
		{
			abandon();
			return Result<void>::Failure("Project Graphics selects a pipeline but the C# type catalog is unavailable");
		}
		auto validPipeline =
		    Rendering::ValidatePipelineAsset(workspace->Index().Filesystem(), workspace->AssetResolver(),
		                                     scriptBuildService->ScriptTypes(), graphicsSettings.value.renderPipeline);
		if (!validPipeline)
		{
			abandon();
			return validPipeline;
		}
	}
	auto configuredPipeline =
	    managedHost->ConfigureRenderPipeline(candidate.value, graphicsSettings.value.renderPipeline);
	if (!configuredPipeline)
	{
		abandon();
		return Result<void>::Failure("Project Graphics pipeline could not be restored: " + configuredPipeline.error);
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
	Vector<ManagedExtensionDescriptor> candidateExtensions = std::move(extensionDirectory.value);
	auto registered = RegisterScriptComponentTypes(&candidateExtensions, candidate.value);
	if (!registered)
	{
		abandon();
		return registered;
	}
	const UInt64 previous = editingScriptSession;
	if (assetPreview)
	{
		const auto closed = assetPreview->CloseExtension();
		if (!closed)
		{
			abandon();
			return closed;
		}
	}
	CancelReferencePickerSessions(gui ? gui->Context() : nullptr);
	auto previousServices = std::move(editingScriptServices);
	editingScriptSession = candidate.value;
	if (assetPreview)
	{
		assetPreview->Reload(true);
	}
	editingScriptServices = std::move(candidateServices);
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
	previousServices.reset();
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
	if (assetInspectorPanel.HasModifiedSource())
	{
		auto next = selectionContext;
		next.beforeChange = {};
		next.SelectAsset(path);
		pendingImporterSelection = std::move(next);
		pendingSourceAsset = true;
		pendingImporterOpen = path;
		importerSelectionPopup = true;
		return {};
	}
	if (assetImporterInspector && assetImporterInspector->HasModified() &&
	    path != assetImporterInspector->SourcePath() && !selectionContext.SelectAsset(path))
	{
		pendingImporterOpen = path;
		return {};
	}
	auto kind = ResourceKindForPath(path);
	if (path.scheme == projectResourceScheme &&
	    ((kind && kind.value != ResourceKind::Scene) || IsTextureSource(path) || IsModelSource(path)))
	{
		projectBrowser.SelectAsset(path);
		selectionContext.SelectAsset(path);
		showInspectorPanel = true;
	}
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
	else if ((kind && kind.value == ResourceKind::Mesh) ||
	         PathFromUrsaString(path.relative).extension() == ".ursaasset")
	{
		projectBrowser.SelectAsset(path);
		selectionContext.SelectAsset(path);
		showInspectorPanel = true;
		return {};
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
		auto preview = textureImportView->Reimport(*workspace->Database(), workspace->Importer(), device, *gui, true);
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
		auto preview = modelImportView->Reimport(*workspace->Database(), workspace->Importer(), true);
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
	else if (path.scheme == projectResourceScheme && workspace->Database())
	{
		const auto metadata = workspace->Database()->Metadata(path);
		const auto importer = workspace->ImportRuleDescriptions()->FindImporter(
		    PathToUrsaString(PathFromUrsaString(path.relative).extension()));
		if ((metadata && metadata.value.importer.find("script:") == 0) || (importer && importer.value->scriptType))
		{
			projectBrowser.SelectAsset(path);
			selectionContext.SelectAsset(path);
			showInspectorPanel = true;
			return {};
		}
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
	ConfigureReferencePreviewFactory(context);
	if (selectionContext.kind == EditorSelectionKind::Asset)
	{
		const auto* asset = projectBrowser.Find(selectionContext.assetPath);
		if (asset && !asset->folder)
		{
			context.assetSelection = AssetReference{asset->guid, 0};
			context.assetSelectionName = asset->name;
		}
	}
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
		if ((extension.kind != ManagedExtensionKind::Menu && extension.kind != ManagedExtensionKind::Window) ||
		    extension.path.rfind(prefix, 0) != 0)
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
		if (Icons::MenuItem(remainder.c_str()))
		{
			if (extension.kind == ManagedExtensionKind::Window)
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
		if (Icons::BeginMenu(submenu.c_str()))
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
	if (assetInspectorPanel.HasModifiedSource())
	{
		return Result<void>::Failure("Save or discard source asset edits before opening another project");
	}
	if (assetImporterInspector && assetImporterInspector->HasModified())
	{
		return Result<void>::Failure("Apply or Revert the current importer settings before changing projects");
	}
	if (scriptIdeJob.valid() || scriptEditorJob.valid())
	{
		return Result<void>::Failure("Wait for the script IDE request before changing projects");
	}
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
		openedWorkspace->SetImportRuleDescriptions(prepared.value.rules);
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
		openedWorkspace->SetImportRuleDescriptions(prepared.value.rules);
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
	// Release private preview graphs before their project and script services.
	const bool hadPreview = assetPreview != nullptr;
	assetPreview.reset();
	if (textureImportView && gui)
	{
		textureImportView->Close(*gui);
	}
	textureImportView.reset();
	modelImportView.reset();
	assetImporterInspector.reset();
	pendingImporterSelection.reset();
	assetInspectorPanel.Reset();
	shaderImportView.reset();
#if defined(URSA_ENABLE_DOTNET)
	if (managedHost && editingScriptSession)
	{
		auto closed = managedHost->CloseScriptSession(editingScriptSession);
		editingScriptSession = 0;
		editingScriptServices.reset();
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
	if (hadPreview)
	{
		assetPreview = std::make_unique<AssetPreviewPanel>(device, *gui);
	}
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
	selectionContext.Clear(true);
	selectionContext.beforeChange = [this](const EditorSelectionContext& next)
	{ return BeforeImporterSelection(next); };
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
	ideGenerationRequested = false;
	ideOpenRequested = false;
	ideSource = {};
	scriptDiagnostics.clear();
	scriptIdeStatus.clear();
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
				ideGenerationRequested = true;
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
	consolePanel.NotifyBuildStarted();
	scriptBuildJob = std::async(std::launch::async, [service] { return service->Build(ScriptAssemblyGroup::Editor); });
	scriptBuildStatus = "正在编译脚本……";
	return {};
#else
	return Result<void>::Failure("This build does not enable .NET scripting");
#endif
}

String EditorApplication::MainWindowTitle() const
{
	String title;
	if (workspace)
	{
		title = PathToUrsaString(workspace->ProjectRoot().filename()) + " - ";
	}
	const auto* current = documents ? documents->Current() : nullptr;
	String documentName = current ? current->Path().FileName() : String("Untitled");
	const auto extension = documentName.find_last_of('.');
	if (extension != String::npos)
	{
		documentName.resize(extension);
	}
	title += documentName.empty() ? "Untitled" : documentName;
	if (current && current->Dirty())
	{
		title += "*";
	}
	return title + " - " + editorProductName + " (" URSA_EDITOR_VERSION ")";
}

Result<void> EditorApplication::DecodeWindowIcon(const UInt8* encoded, Size size)
{
	if (!encoded || !size || size > static_cast<Size>(std::numeric_limits<int>::max()))
	{
		return Result<void>::Failure("Window icon image is empty or too large");
	}
	int width = 0;
	int height = 0;
	int channels = 0;
	std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
	    stbi_load_from_memory(encoded, static_cast<int>(size), &width, &height, &channels, STBI_rgb_alpha),
	    stbi_image_free);
	if (!pixels)
	{
		return Result<void>::Failure("Cannot decode window icon image");
	}
	Vector<UInt8> decoded(pixels.get(), pixels.get() + static_cast<Size>(width) * height * 4);
	if (window)
	{
		auto applied = window->SetIcon(static_cast<UInt32>(width), static_cast<UInt32>(height), decoded);
		if (!applied)
		{
			return applied;
		}
	}
	windowIconWidth = static_cast<UInt32>(width);
	windowIconHeight = static_cast<UInt32>(height);
	windowIconPixels = std::move(decoded);
	return {};
}

Result<void> EditorApplication::SetWindowIcon(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	const auto size = input.tellg();
	if (!input || size <= 0 || size > std::numeric_limits<int>::max())
	{
		return Result<void>::Failure("Cannot read window icon: " + PathToUrsaString(path));
	}
	Vector<UInt8> encoded(static_cast<Size>(size));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size)))
	{
		return Result<void>::Failure("Cannot read window icon: " + PathToUrsaString(path));
	}
	return DecodeWindowIcon(encoded.data(), encoded.size());
}

void EditorApplication::ResetEditorLayout()
{
	if (!gui)
	{
		return;
	}
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

bool EditorApplication::CanCopyGameObjectSelection() const
{
	const EditorDocument* current = documents ? documents->Current() : nullptr;
	return !play.Active() && current && current->Kind() == ResourceKind::Scene &&
	       !selectionContext.entries.empty() && selectionContext.activeDocumentId == current->Id() &&
	       std::all_of(selectionContext.entries.begin(), selectionContext.entries.end(),
	                   [current](const EditorSelectionEntry& entry)
	                   {
		                   return entry.kind == EditorSelectionKind::GameObject && entry.documentId == current->Id() &&
		                          !entry.playSession && !entry.IsMissing();
	                   });
}

bool EditorApplication::CanPasteGameObjectSelection() const
{
	const EditorDocument* current = documents ? documents->Current() : nullptr;
	if (play.Active() || !current || current->Kind() != ResourceKind::Scene || !gameObjectClipboardSnapshot)
	{
		return false;
	}
	return selectionContext.entries.empty() ||
	       (selectionContext.activeDocumentId == current->Id() &&
	        std::all_of(selectionContext.entries.begin(), selectionContext.entries.end(),
	                    [current](const EditorSelectionEntry& entry)
	                    {
		                    return entry.kind == EditorSelectionKind::GameObject &&
		                           entry.documentId == current->Id() && !entry.playSession && !entry.IsMissing();
	                    }));
}

bool EditorApplication::CanPasteGameObjectSelectionAsChild() const
{
	const auto* parent = selectionContext.PrimaryEntry();
	const EditorDocument* current = documents ? documents->Current() : nullptr;
	return CanPasteGameObjectSelection() && current && selectionContext.entries.size() == 1 && parent &&
	       parent->kind == EditorSelectionKind::GameObject && parent->documentId == current->Id() &&
	       !parent->playSession && !parent->IsMissing();
}

Result<void> EditorApplication::CopyGameObjectSelection()
{
	if (!CanCopyGameObjectSelection() || !project || !documents)
	{
		return Result<void>::Failure("Copy requires selected GameObjects in an editable Scene");
	}
	EditorDocument* current = documents->Current();
	auto draft = current ? current->Payload<SceneDraft>() : nullptr;
	if (!draft)
	{
		return Result<void>::Failure("The active Scene draft is unavailable");
	}
	Vector<ResourceId> copiedIds;
	copiedIds.reserve(selectionContext.entries.size());
	for (const EditorSelectionEntry& entry : selectionContext.entries)
	{
		copiedIds.push_back(entry.objectId);
	}
	const auto* primary = selectionContext.PrimaryEntry();
	Vector<Mat4> copiedWorldTransforms;
	auto snapshot = CaptureGameObjectSelection(draft->GetScene(), copiedIds,
	                                          primary ? primary->objectId : ResourceId{}, project->components,
	                                          *documents, *draft, &copiedWorldTransforms);
	if (!snapshot)
	{
		return Result<void>::Failure(snapshot.error);
	}
	gameObjectClipboardSnapshot = std::move(snapshot.value);
	gameObjectClipboardWorldTransforms = std::move(copiedWorldTransforms);
	gameObjectClipboardPrimary = primary ? primary->objectId : ResourceId{};
	return {};
}

Result<void> EditorApplication::CutGameObjectSelection()
{
	if (!CanCopyGameObjectSelection() || !project || !documents)
	{
		return Result<void>::Failure("Cut requires selected GameObjects in an editable Scene");
	}
	auto copied = CopyGameObjectSelection();
	if (!copied)
	{
		return copied;
	}
	EditorDocument* current = documents->Current();
	auto draft = current ? current->Payload<SceneDraft>() : nullptr;
	if (!draft)
	{
		return Result<void>::Failure("The active Scene draft is unavailable");
	}
	Vector<ResourceId> ids;
	ids.reserve(selectionContext.entries.size());
	for (const EditorSelectionEntry& entry : selectionContext.entries)
	{
		ids.push_back(entry.objectId);
	}
	auto deleted = DeleteGameObjectSelection(draft->GetScene(), ids, project->components, *draft, &selectionContext);
	if (deleted)
	{
		current->MarkDirty();
	}
	return deleted;
}

Result<void> EditorApplication::PasteGameObjectSelection(bool asChild)
{
	if (!(asChild ? CanPasteGameObjectSelectionAsChild() : CanPasteGameObjectSelection()) || !project || !documents)
	{
		return Result<void>::Failure(asChild ? "Paste as Child requires one selected GameObject in the current Scene"
		                                     : "Paste requires a copied GameObject selection");
	}
	EditorDocument* current = documents->Current();
	auto draft = current ? current->Payload<SceneDraft>() : nullptr;
	if (!draft)
	{
		return Result<void>::Failure("The active Scene cannot accept the copied GameObjects");
	}
	ResourceId parentOverride;
	if (asChild)
	{
		parentOverride = selectionContext.PrimaryEntry()->objectId;
	}
	auto pasted = DuplicateGameObjectSelection(draft->GetScene(), {}, gameObjectClipboardPrimary, project->components,
	                                           *documents, *draft, &selectionContext, parentOverride,
	                                           gameObjectClipboardSnapshot.get(), &gameObjectClipboardWorldTransforms);
	if (pasted)
	{
		current->MarkDirty();
	}
	return pasted;
}

void EditorApplication::DrawMainMenuBar()
{
	Icons::SetPanelResetAction([this] { ResetEditorLayout(); });
	const auto title = MainWindowTitle();
	if (window && window->Title() != title)
	{
		window->SetTitle(title);
	}
	const ImGuiIO& windowShortcutInput = ImGui::GetIO();
	const bool editorShortcutsAllowed =
	    !(play.Active() && sceneViewPanel && sceneViewPanel->DisableEditorShortcutsInPlay() &&
	      (viewportFocused ||
	       (showGamePanel && sceneViewPanel->WillFocusGameViewOnPlayStart())));
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	    !windowShortcutInput.KeyAlt)
	{
		const auto openAndFocus = [](bool& visible, const char* windowName)
		{
			visible = true;
			ImGui::SetWindowFocus(windowName);
		};
		if (windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			openAndFocus(showConsolePanel, "Console");
		}
		else if (!windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_1, false))
		{
			openAndFocus(showScenePanel, "Scene");
		}
		else if (!windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_2, false))
		{
			openAndFocus(showGamePanel, "Game");
		}
		else if (!windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_3, false))
		{
			openAndFocus(showInspectorPanel, "Inspector");
		}
		else if (!windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_4, false))
		{
			openAndFocus(showHierarchyPanel, "Hierarchy");
		}
		else if (!windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_5, false))
		{
			openAndFocus(showProjectPanel, "Project");
		}
	}
	EditorDocument* current = documents ? documents->Current() : nullptr;
	const auto menuCommandContext =
	    EditorCommandContext::Capture(selectionContext, current ? current->Id() : selectionContext.activeDocumentId,
	                                  current ? current->Revision() : selectionContext.revision);
	auto selectChildren = [this, current]() -> Result<void>
	{
		if (play.Active())
		{
			auto* scene = play.GetScene();
			return scene ? hierarchy.SelectChildren(*scene, selectionContext, selectionContext.documentId, true)
			             : Result<void>::Failure("There is no active Play scene");
		}
		auto draft = current ? current->Payload<SceneDraft>() : nullptr;
		if (!draft)
		{
			return Result<void>::Failure("Open a Scene or Prefab to select its children");
		}
		if (current->Kind() == ResourceKind::Scene)
		{
			return hierarchy.SelectChildren(draft->GetScene(), selectionContext, current->Id(), false);
		}
		if (current->Kind() == ResourceKind::Prefab && draft->GetPrefabAsset())
		{
			return hierarchy.SelectChildren(*draft->GetPrefabAsset(), selectionContext, current->Id());
		}
		return Result<void>::Failure("The active document does not contain a selectable Hierarchy");
	};
	auto invertSelection = [this, current]() -> Result<void>
	{
		if (play.Active())
		{
			auto* scene = play.GetScene();
			return scene ? hierarchy.InvertSelection(*scene, selectionContext, selectionContext.documentId, true)
			             : Result<void>::Failure("There is no active Play scene");
		}
		auto draft = current ? current->Payload<SceneDraft>() : nullptr;
		if (!draft)
		{
			return Result<void>::Failure("Open a Scene or Prefab to invert the Hierarchy selection");
		}
		if (current->Kind() == ResourceKind::Scene)
		{
			return hierarchy.InvertSelection(draft->GetScene(), selectionContext, current->Id(), false);
		}
		if (current->Kind() == ResourceKind::Prefab && draft->GetPrefabAsset())
		{
			return hierarchy.InvertSelection(*draft->GetPrefabAsset(), selectionContext, current->Id());
		}
		return Result<void>::Failure("The active document does not contain a selectable Hierarchy");
	};
	auto transformSelectionToView = [this, current](bool alignRotation) -> Result<void>
	{
		if (!sceneViewPanel)
		{
			return Result<void>::Failure("The Scene view is unavailable");
		}
		Scene* scene = nullptr;
		ResourceId documentId;
		const bool playSession = play.Active();
		if (playSession)
		{
			scene = play.GetScene();
			documentId = selectionContext.documentId;
		}
		else if (current && current->Kind() == ResourceKind::Scene)
		{
			auto draft = current->Payload<SceneDraft>();
			if (draft)
			{
				scene = &draft->GetScene();
				documentId = current->Id();
			}
		}
		if (!scene)
		{
			return Result<void>::Failure("Open a Scene to transform its selection to the Scene view");
		}
		auto transformed = sceneViewPanel->TransformSelectionToView(*scene, selectionContext, documentId, playSession,
		                                                            alignRotation);
		if (transformed && !playSession && current)
		{
			current->MarkDirty();
		}
		return transformed;
	};
	const bool hasGameObjectSelection = !selectionContext.entries.empty() &&
	                                    std::all_of(selectionContext.entries.begin(), selectionContext.entries.end(),
	                                                [](const auto& entry)
	                                                { return entry.kind == EditorSelectionKind::GameObject; });
	const bool canCopyGameObjects = CanCopyGameObjectSelection();
	const bool canPasteGameObjects = CanPasteGameObjectSelection();
	const bool canPasteGameObjectsAsChild = CanPasteGameObjectSelectionAsChild();
	auto copyGameObjects = [this] { return CopyGameObjectSelection(); };
	auto cutGameObjects = [this] { return CutGameObjectSelection(); };
	auto pasteGameObjects = [this](bool asChild) { return PasteGameObjectSelection(asChild); };
	const bool hierarchyClipboardInputAllowed =
	    editorShortcutsAllowed && !windowShortcutInput.WantTextInput && !ImGui::IsAnyItemActive() &&
	    (windowShortcutInput.KeyCtrl || windowShortcutInput.KeySuper) && !windowShortcutInput.KeyAlt;
	if (hierarchyClipboardInputAllowed && !windowShortcutInput.KeyShift && canCopyGameObjects &&
	    ImGui::IsKeyPressed(ImGuiKey_X, false))
	{
		auto cut = cutGameObjects();
		workspaceError = cut ? String{} : cut.error;
	}
	if (hierarchyClipboardInputAllowed && !windowShortcutInput.KeyShift && canCopyGameObjects &&
	    ImGui::IsKeyPressed(ImGuiKey_C, false))
	{
		auto copied = copyGameObjects();
		workspaceError = copied ? String{} : copied.error;
	}
	if (hierarchyClipboardInputAllowed && ImGui::IsKeyPressed(ImGuiKey_V, false) && canPasteGameObjects)
	{
		const bool asChild = windowShortcutInput.KeyShift;
		if (!asChild || (selectionContext.entries.size() == 1 && selectionContext.PrimaryEntry() &&
		                 selectionContext.PrimaryEntry()->kind == EditorSelectionKind::GameObject &&
		                 selectionContext.PrimaryEntry()->documentId == current->Id() &&
		                 !selectionContext.PrimaryEntry()->playSession &&
		                 !selectionContext.PrimaryEntry()->IsMissing()))
		{
			auto pasted = pasteGameObjects(asChild);
			workspaceError = pasted ? String{} : pasted.error;
		}
	}
	auto createNewScene = [this, current]() -> Result<void>
	{
		if (!documents || !workspace || !project || play.Active())
		{
			return Result<void>::Failure("Create a Scene after opening a project and stopping Play mode");
		}
		auto& filesystem = workspace->Database()->Filesystem();
		for (UInt32 suffix = 0; suffix < 10000; ++suffix)
		{
			const String filename = suffix == 0 ? String("NewScene.ursascene")
			                                    : String("NewScene ") + ToUrsaString(std::to_string(suffix)) +
			                                          ".ursascene";
			auto path = ProjectAssetPath(filename.c_str());
			if (!path)
			{
				return Result<void>::Failure(path.error);
			}
			if (current && current->Path() == path.value)
			{
				continue;
			}
			auto exists = filesystem.Exists(path.value);
			if (!exists)
			{
				return Result<void>::Failure(exists.error);
			}
			auto sidecarExists = filesystem.Exists(VirtualPath{path.value.scheme, path.value.relative + ".meta"});
			if (!sidecarExists)
			{
				return Result<void>::Failure(sidecarExists.error);
			}
			if (exists.value || sidecarExists.value)
			{
				continue;
			}
			auto created = NewSceneDocument(path.value, ResourceKind::Scene, project->components,
			                                documents->Session(), project->editContextSetup);
			if (!created)
			{
				return Result<void>::Failure(created.error);
			}
			return documents->RequestOpen(std::move(created.value));
		}
		return Result<void>::Failure("Could not find an unused Scene path for the new document");
	};
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	    !windowShortcutInput.KeyAlt &&
	    !windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		auto created = createNewScene();
		workspaceError = created ? String{} : created.error;
	}
	auto createGameObject = [this, current](const EditorCommandContext& context, bool asChild) -> Result<void>
	{
		if (!current || current->Kind() != ResourceKind::Scene || play.Active())
		{
			return Result<void>::Failure("Open an editable Scene to create a GameObject");
		}
		auto draft = current->Payload<SceneDraft>();
		if (!draft)
		{
			return Result<void>::Failure("The active Scene draft is unavailable");
		}
		GameObject::Ptr parent;
		if (asChild)
		{
			const auto* primary = context.selection.PrimaryEntry();
			if (!primary || primary->kind != EditorSelectionKind::GameObject || primary->documentId != current->Id() ||
			    primary->playSession || primary->IsMissing())
			{
				return Result<void>::Failure("Create Empty Child requires a selected GameObject");
			}
			auto selected = draft->GetScene().GameObjectById(primary->objectId);
			if (!selected)
			{
				return Result<void>::Failure(selected.error);
			}
			parent = selected.value;
		}
		auto created = draft->GetScene().CreateGameObject("GameObject", parent);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		auto identity = draft->GetScene().PersistentId(created.value);
		if (!identity)
		{
			draft->GetScene().DestroyGameObject(created.value);
			return Result<void>::Failure(identity.error);
		}
		hierarchy.Select(draft->GetScene(), created.value);
		selectionContext.SelectGameObject(current->Id(), identity.value);
		current->MarkDirty();
		return {};
	};
	auto createComponentGameObject =
	    [this, current](const char* objectName, LocalTransform transform,
	                    const std::function<Result<ComponentId>(Scene&, const GameObject::Ptr&)>& addComponent)
	    -> Result<void>
	{
		if (!documents || !current || current->Kind() != ResourceKind::Scene || play.Active())
		{
			return Result<void>::Failure("Open an editable Scene to create this GameObject");
		}
		auto draft = current->Payload<SceneDraft>();
		if (!draft)
		{
			return Result<void>::Failure("The active Scene draft is unavailable");
		}
		auto& scene = draft->GetScene();
		auto object = scene.CreateGameObject(objectName);
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		auto transformed = scene.SetLocalTransform(object.value, transform);
		if (!transformed)
		{
			scene.DestroyGameObject(object.value);
			return Result<void>::Failure(transformed.error);
		}
		auto component = addComponent(scene, object.value);
		if (!component)
		{
			scene.DestroyGameObject(object.value);
			return Result<void>::Failure(component.error);
		}
		auto identity = scene.PersistentId(object.value);
		if (!identity)
		{
			scene.DestroyGameObject(object.value);
			return Result<void>::Failure(identity.error);
		}
		hierarchy.Select(scene, object.value);
		selectionContext.SelectGameObject(current->Id(), identity.value);
		current->MarkDirty();
		return {};
	};
	auto createEmptyParent = [this, current](const EditorCommandContext& context) -> Result<void>
	{
		if (!current || current->Kind() != ResourceKind::Scene || play.Active())
		{
			return Result<void>::Failure("Create Empty Parent requires an editable Scene");
		}
		auto draft = current->Payload<SceneDraft>();
		const auto* primary = context.selection.PrimaryEntry();
		if (!draft || context.selection.entries.size() != 1 || !primary ||
		    primary->kind != EditorSelectionKind::GameObject || primary->documentId != current->Id() ||
		    primary->playSession || primary->IsMissing())
		{
			return Result<void>::Failure("Create Empty Parent requires one selected GameObject");
		}
		auto& scene = draft->GetScene();
		auto child = scene.GameObjectById(primary->objectId);
		if (!child)
		{
			return Result<void>::Failure(child.error);
		}
		auto oldParent = scene.ParentOf(child.value);
		auto oldLocal = scene.GetLocalTransform(child.value);
		if (!oldParent || !oldLocal)
		{
			return Result<void>::Failure(!oldParent ? oldParent.error : oldLocal.error);
		}
		const Vector<GameObject::Ptr> siblings =
		    oldParent.value ? scene.ChildrenOf(oldParent.value).value : scene.RootGameObjects();
		const auto oldPosition = std::find(siblings.begin(), siblings.end(), child.value);
		if (oldPosition == siblings.end())
		{
			return Result<void>::Failure("The selected GameObject is missing from its sibling list");
		}
		const auto oldIndex = static_cast<Ursa::Size>(std::distance(siblings.begin(), oldPosition));
		auto parent = scene.CreateGameObject("GameObject", oldParent.value);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		const auto rollback = [&]()
		{
			scene.SetParent(child.value, oldParent.value);
			scene.SetLocalTransform(child.value, oldLocal.value);
			scene.ReorderGameObject(child.value, oldIndex);
			scene.DestroyGameObject(parent.value);
		};
		auto parentTransform = scene.SetLocalTransform(parent.value, oldLocal.value);
		if (!parentTransform)
		{
			scene.DestroyGameObject(parent.value);
			return Result<void>::Failure(parentTransform.error);
		}
		auto reparented = scene.SetParent(child.value, parent.value);
		if (!reparented)
		{
			scene.DestroyGameObject(parent.value);
			return Result<void>::Failure(reparented.error);
		}
		auto resetLocal = scene.SetLocalTransform(child.value, LocalTransform{});
		if (!resetLocal)
		{
			rollback();
			return Result<void>::Failure(resetLocal.error);
		}
		auto reordered = scene.ReorderGameObject(parent.value, oldIndex);
		if (!reordered)
		{
			rollback();
			return Result<void>::Failure(reordered.error);
		}
		auto identity = scene.PersistentId(parent.value);
		if (!identity)
		{
			rollback();
			return Result<void>::Failure(identity.error);
		}
		hierarchy.Select(scene, parent.value);
		selectionContext.SelectGameObject(current->Id(), identity.value);
		current->MarkDirty();
		return {};
	};
	auto reorderGameObjects = [this, current](const EditorCommandContext& context, bool first) -> Result<void>
	{
		if (!current || current->Kind() != ResourceKind::Scene || play.Active() || context.selection.entries.empty())
		{
			return Result<void>::Failure("Sibling ordering requires selected GameObjects in an editable Scene");
		}
		auto draft = current->Payload<SceneDraft>();
		if (!draft)
		{
			return Result<void>::Failure("The active Scene draft is unavailable");
		}
		auto& scene = draft->GetScene();
		struct SiblingGroup
		{
			GameObject::Ptr parent;
			Vector<GameObject::Ptr> original;
			Vector<GameObject::Ptr> selected;
		};
		Vector<SiblingGroup> groups;
		for (const auto& entry : context.selection.entries)
		{
			if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != current->Id() || entry.playSession ||
			    entry.IsMissing())
			{
				return Result<void>::Failure("Sibling ordering requires GameObjects in the current Scene");
			}
			auto object = scene.GameObjectById(entry.objectId);
			auto parent = object ? scene.ParentOf(object.value)
			                    : Result<GameObject::Ptr>::Failure("The selected GameObject no longer exists");
			if (!object || !parent)
			{
				return Result<void>::Failure(!object ? object.error : parent.error);
			}
			auto group = std::find_if(groups.begin(), groups.end(), [&](const SiblingGroup& candidate)
			                          { return candidate.parent == parent.value; });
			if (group == groups.end())
			{
				Vector<GameObject::Ptr> siblings;
				if (parent.value)
				{
					auto children = scene.ChildrenOf(parent.value);
					if (!children)
					{
						return Result<void>::Failure(children.error);
					}
					siblings = std::move(children.value);
				}
				else
				{
					siblings = scene.RootGameObjects();
				}
				groups.push_back({parent.value, siblings, {}});
				group = std::prev(groups.end());
			}
			group->selected.push_back(object.value);
		}
		Vector<Vector<GameObject::Ptr>> reorderedGroups;
		for (const auto& group : groups)
		{
			Vector<GameObject::Ptr> ordered;
			ordered.reserve(group.original.size());
			if (first)
			{
				for (const auto& sibling : group.original)
				{
					if (std::find(group.selected.begin(), group.selected.end(), sibling) != group.selected.end())
					{
						ordered.push_back(sibling);
					}
				}
			}
			for (const auto& sibling : group.original)
			{
				const bool selected =
				    std::find(group.selected.begin(), group.selected.end(), sibling) != group.selected.end();
				if (!selected)
				{
					ordered.push_back(sibling);
				}
			}
			if (!first)
			{
				for (const auto& sibling : group.original)
				{
					if (std::find(group.selected.begin(), group.selected.end(), sibling) != group.selected.end())
					{
						ordered.push_back(sibling);
					}
				}
			}
			reorderedGroups.push_back(std::move(ordered));
		}
		for (Ursa::Size groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
		{
			for (Ursa::Size siblingIndex = 0; siblingIndex < reorderedGroups[groupIndex].size(); ++siblingIndex)
			{
				auto reordered = scene.ReorderGameObject(reorderedGroups[groupIndex][siblingIndex], siblingIndex);
				if (!reordered)
				{
					for (const auto& restoreGroup : groups)
					{
						for (Ursa::Size index = 0; index < restoreGroup.original.size(); ++index)
						{
							scene.ReorderGameObject(restoreGroup.original[index], index);
						}
					}
					return Result<void>::Failure(reordered.error);
				}
			}
		}
		current->MarkDirty();
		return {};
	};
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	    windowShortcutInput.KeyShift &&
	    !windowShortcutInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		auto created = createGameObject(menuCommandContext, false);
		workspaceError = created ? String{} : created.error;
	}
	else if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	         windowShortcutInput.KeyShift &&
	         !windowShortcutInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_G, false))
	{
		auto created = createEmptyParent(menuCommandContext);
		workspaceError = created ? String{} : created.error;
	}
	else if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	         !windowShortcutInput.KeyAlt &&
	         !windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Equal, false))
	{
		auto reordered = reorderGameObjects(menuCommandContext, true);
		workspaceError = reordered ? String{} : reordered.error;
	}
	else if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	         !windowShortcutInput.KeyAlt &&
	         !windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Minus, false))
	{
		auto reordered = reorderGameObjects(menuCommandContext, false);
		workspaceError = reordered ? String{} : reordered.error;
	}
	else if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyAlt &&
	         windowShortcutInput.KeyShift &&
	         !windowShortcutInput.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
	{
		auto created = createGameObject(menuCommandContext, true);
		workspaceError = created ? String{} : created.error;
	}
	auto toggleGameObjectActive = [this, current]() -> Result<void>
	{
		if (!current || current->Kind() != ResourceKind::Scene || play.Active())
		{
			return Result<void>::Failure("Toggle Active State requires an editable Scene");
		}
		auto draft = current->Payload<SceneDraft>();
		if (!draft || selectionContext.entries.empty())
		{
			return Result<void>::Failure("Select one or more GameObjects to toggle their active state");
		}
		Vector<std::pair<GameObject::Ptr, bool>> targets;
		for (const auto& entry : selectionContext.entries)
		{
			if (entry.kind != EditorSelectionKind::GameObject || entry.documentId != current->Id() || entry.playSession ||
			    entry.IsMissing())
			{
				return Result<void>::Failure("Toggle Active State requires GameObjects in the current Scene");
			}
			auto object = draft->GetScene().GameObjectById(entry.objectId);
			if (!object)
			{
				return Result<void>::Failure(object.error);
			}
			auto active = object.value->ActiveSelf();
			if (!active)
			{
				return Result<void>::Failure(active.error);
			}
			targets.emplace_back(object.value, active.value);
		}
		Ursa::Size applied = 0;
		for (; applied < targets.size(); ++applied)
		{
			auto changed = draft->GetScene().SetActive(targets[applied].first, !targets[applied].second);
			if (!changed)
			{
				while (applied > 0)
				{
					--applied;
					draft->GetScene().SetActive(targets[applied].first, targets[applied].second);
				}
				return Result<void>::Failure(changed.error);
			}
		}
		current->MarkDirty();
		return {};
	};
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyAlt &&
	    windowShortcutInput.KeyShift &&
	    !windowShortcutInput.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
	{
		auto toggled = toggleGameObjectActive();
		workspaceError = toggled ? String{} : toggled.error;
	}
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyShift &&
	    !windowShortcutInput.KeyCtrl &&
	    !windowShortcutInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_D, false))
	{
		workspaceError = selectionContext.Clear() ? String{} : String("Selection could not be cleared yet");
	}
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyShift &&
	    !windowShortcutInput.KeyCtrl &&
	    !windowShortcutInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_C, false))
	{
		if (!selectionContext.entries.empty() &&
		    std::all_of(selectionContext.entries.begin(), selectionContext.entries.end(),
	                [](const auto& entry) { return entry.kind == EditorSelectionKind::GameObject; }))
		{
			auto selected = selectChildren();
			workspaceError = selected ? String{} : selected.error;
		}
	}
	if (editorShortcutsAllowed && !windowShortcutInput.WantTextInput && windowShortcutInput.KeyCtrl &&
	    !windowShortcutInput.KeyShift &&
	    !windowShortcutInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_I, false))
	{
		const bool gameObjectSelection = std::all_of(
		    selectionContext.entries.begin(), selectionContext.entries.end(),
		    [](const auto& entry) { return entry.kind == EditorSelectionKind::GameObject; });
		if (gameObjectSelection &&
		    (play.Active() || (current && (current->Kind() == ResourceKind::Scene ||
		                                   current->Kind() == ResourceKind::Prefab))))
		{
			auto inverted = invertSelection();
			workspaceError = inverted ? String{} : inverted.error;
		}
	}
	if (editorShortcutsAllowed && hasGameObjectSelection && !windowShortcutInput.WantTextInput &&
	    windowShortcutInput.KeyCtrl &&
	    windowShortcutInput.KeyShift &&
	    !windowShortcutInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		auto transformed = transformSelectionToView(true);
		workspaceError = transformed ? String{} : transformed.error;
	}
	else if (editorShortcutsAllowed && hasGameObjectSelection && !windowShortcutInput.WantTextInput &&
	         windowShortcutInput.KeyCtrl &&
	         windowShortcutInput.KeyAlt &&
	         !windowShortcutInput.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		auto transformed = transformSelectionToView(false);
		workspaceError = transformed ? String{} : transformed.error;
	}
	if (ImGui::BeginMainMenuBar())
	{
		ImGui::TextUnformatted("Ursa Editor");
		ImGui::Separator();
		EditorCommands fileCommands;
		fileCommands.Register({"file.save", "Save", "Ctrl+S",
		                       [this, current](const EditorCommandContext&)
		                       {
			                       return !play.Active() && (current || assetInspectorPanel.HasModifiedSource())
			                                  ? Result<void>{}
			                                  : Result<void>::Failure("There is no writable document or source asset");
		                       },
		                       [this](const EditorCommandContext&)
		                       {
			                       const auto sourceSaved = assetInspectorPanel.SaveSelectedSource(selectionContext);
			                       auto saved = !sourceSaved        ? Result<void>::Failure(sourceSaved.error)
			                                    : sourceSaved.value ? Result<void>{}
			                                    : documents         ? documents->SaveCurrent()
			                                                : Result<void>::Failure("There is no active document");
			                       if (saved)
			                       {
				                       RequestAssetImport();
			                       }
			                       return saved;
		                       }});
		fileCommands.Register({"assets.refresh", "Refresh", "Ctrl+R",
		                       [this](const EditorCommandContext&)
		                       {
			                       return workspace && !play.Active()
			                                  ? Result<void>{}
			                                  : Result<void>::Failure("No editable project is open");
		                       },
		                       [this](const EditorCommandContext&)
		                       {
			                       RequestAssetImport();
			                       auto refreshed = workspace->Refresh();
			                       if (refreshed)
			                       {
				                       refreshed = projectBrowser.Refresh(*workspace);
			                       }
			                       return refreshed;
		                       }});
		fileCommands.Register({"project.rename", "Rename", "F2",
		                       [this](const EditorCommandContext& context)
		                       {
			                       const auto* primary = context.selection.PrimaryEntry();
			                       return workspace && !play.Active() && context.selection.entries.size() == 1 &&
			                                      primary && primary->kind == EditorSelectionKind::Asset &&
			                                      !primary->IsMissing() &&
			                                      primary->assetRole == EditorAssetSelectionRole::Source
			                                  ? Result<void>{}
			                                  : Result<void>::Failure("Rename requires one writable source asset");
		                       },
		                       [this](const EditorCommandContext&)
		                       {
			                       projectBrowserPanel.RequestRenameSelected();
			                       return Result<void>{};
		                       }});
		if (Icons::BeginMenu("File"))
		{
			const bool canCreateScene = workspace && project && documents && !play.Active();
			if (Icons::MenuItem("New Scene", "Ctrl+N", false, canCreateScene))
			{
				auto created = createNewScene();
				workspaceError = created ? String{} : created.error;
			}
			ImGui::Separator();
			const auto canSave = fileCommands.CanExecute("file.save", menuCommandContext, menuCommandContext);
			ImGui::BeginDisabled(!canSave);
			if (Icons::MenuItem("Save", "Ctrl+S"))
			{
				auto saved = fileCommands.Execute("file.save", menuCommandContext, menuCommandContext);
				workspaceError = saved ? String{} : saved.error;
			}
			ImGui::BeginDisabled(!current);
			if (Icons::MenuItem("Close Document"))
			{
				if (HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
				{
					RequestCancelReferencePickerSessions(gui ? gui->Context() : nullptr,
					                                     [this] { closeDocumentAfterPickerCancellation = true; });
				}
				else
				{
					auto closed = documents->RequestClose();
					workspaceError = closed ? String{} : closed.error;
				}
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			ImGui::Separator();
			if (Icons::MenuItem("Exit"))
			{
				if (assetInspectorPanel.HasModifiedSource())
				{
					exitAfterInspectorPrompt = true;
					EditorSelectionContext next;
					BeforeImporterSelection(next);
				}
				else
				{
					exitRequested = true;
					if (HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
					{
						RequestCancelReferencePickerSessions(gui ? gui->Context() : nullptr,
						                                     [this] { closeDocumentAfterPickerCancellation = true; });
					}
					else
					{
						const auto closing = documents->RequestClose();
						if (!closing)
						{
							workspaceError = closing.error;
						}
					}
				}
			}
			DrawManagedMenus("File");
			ImGui::EndMenu();
		}
		if (Icons::BeginMenu("Edit"))
		{
		if (Icons::MenuItem("Cut", "Ctrl+X", false, canCopyGameObjects))
		{
			auto cut = cutGameObjects();
			workspaceError = cut ? String{} : cut.error;
		}
			if (Icons::MenuItem("Copy", "Ctrl+C", false, canCopyGameObjects))
			{
				auto copied = copyGameObjects();
				workspaceError = copied ? String{} : copied.error;
			}
			if (Icons::MenuItem("Paste", "Ctrl+V", false, canPasteGameObjects))
			{
				auto pasted = pasteGameObjects(false);
				workspaceError = pasted ? String{} : pasted.error;
			}
			if (Icons::MenuItem("Paste as Child", "Ctrl+Shift+V", false, canPasteGameObjectsAsChild))
			{
				auto pasted = pasteGameObjects(true);
				workspaceError = pasted ? String{} : pasted.error;
			}
			ImGui::Separator();
			const bool gameObjectSelection = std::all_of(
			    selectionContext.entries.begin(), selectionContext.entries.end(),
			    [](const auto& entry) { return entry.kind == EditorSelectionKind::GameObject; });
			const bool canInvertSelection = gameObjectSelection &&
			                                (play.Active() || (current && (current->Kind() == ResourceKind::Scene ||
			                                                               current->Kind() == ResourceKind::Prefab)));
			if (Icons::MenuItem("Invert Selection", "Ctrl+I", false, canInvertSelection))
			{
				auto inverted = invertSelection();
				workspaceError = inverted ? String{} : inverted.error;
			}
			const bool canSelectChildren = !selectionContext.entries.empty() &&
			                               (play.Active() || (current && (current->Kind() == ResourceKind::Scene ||
			                                                              current->Kind() == ResourceKind::Prefab))) &&
			                               std::all_of(selectionContext.entries.begin(), selectionContext.entries.end(),
			                                           [](const auto& entry)
			                                           { return entry.kind == EditorSelectionKind::GameObject; });
			if (Icons::MenuItem("Select Children", "Shift+C", false, canSelectChildren))
			{
				auto selected = selectChildren();
				workspaceError = selected ? String{} : selected.error;
			}
			if (Icons::MenuItem("Deselect All", "Shift+D", false, !selectionContext.entries.empty()))
			{
				workspaceError = selectionContext.Clear() ? String{} : String("Selection could not be cleared yet");
			}
			ImGui::Separator();
			ImGui::BeginDisabled();
			Icons::MenuItem("Undo", "Ctrl+Z");
			Icons::MenuItem("Redo", "Ctrl+Y");
			ImGui::EndDisabled();
			if (Icons::MenuItem("External Tools", nullptr, showExternalTools))
			{
				showExternalTools = !showExternalTools;
			}
			DrawManagedMenus("Edit");
			ImGui::EndMenu();
		}
		if (Icons::BeginMenu("Assets"))
		{
			ImGui::BeginDisabled(!workspace || play.Active());
			Icons::MenuItem("Auto Refresh", nullptr, &autoRefreshAssets);
			if (Icons::MenuItem("Reimport All"))
			{
				RequestAssetImport({}, true);
			}
			const auto canRefresh = fileCommands.CanExecute("assets.refresh", menuCommandContext, menuCommandContext);
			if (Icons::MenuItem("Refresh", "Ctrl+R", false, static_cast<bool>(canRefresh)))
			{
				auto refreshed = fileCommands.Execute("assets.refresh", menuCommandContext, menuCommandContext);
				workspaceError = refreshed ? String{} : refreshed.error;
			}
			if (Icons::BeginMenu("Create"))
			{
				if (Icons::MenuItem("Folder"))
				{
					auto created = CreateProjectFolder();
					workspaceError = created ? String{} : created.error;
				}
				if (Icons::MenuItem("Scene"))
				{
					auto created = CreateProjectAsset(ResourceKind::Scene);
					workspaceError = created ? String{} : created.error;
				}
				if (Icons::MenuItem("Prefab"))
				{
					auto created = CreateProjectAsset(ResourceKind::Prefab);
					workspaceError = created ? String{} : created.error;
				}
				if (Icons::MenuItem("Material"))
				{
					auto created = CreateProjectAsset(ResourceKind::Material);
					workspaceError = created ? String{} : created.error;
				}
				if (Icons::MenuItem("PBR Material"))
				{
					auto created = CreateProjectAsset(ResourceKind::Material, MaterialType::PbrMetallicRoughness);
					workspaceError = created ? String{} : created.error;
				}
				if (Icons::MenuItem("Texture"))
				{
					auto created = CreateProjectAsset(ResourceKind::Texture);
					workspaceError = created ? String{} : created.error;
				}
				if (scriptBuildService)
				{
					Vector<const ScriptTypeDescriptor*> assetTypes;
					for (const ScriptTypeDescriptor& type : scriptBuildService->ScriptTypes())
					{
						if (type.group == ScriptAssemblyGroup::Game && type.kind == ScriptTypeKind::ScriptableObject &&
						    !type.createAssetMenuName.empty())
						{
							assetTypes.push_back(&type);
						}
					}
					std::sort(assetTypes.begin(), assetTypes.end(),
					          [](const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right)
					          {
						          return left->createAssetMenuOrder != right->createAssetMenuOrder
						                     ? left->createAssetMenuOrder < right->createAssetMenuOrder
						                     : left->createAssetMenuName < right->createAssetMenuName;
					          });
					for (const ScriptTypeDescriptor* type : assetTypes)
					{
						if (Icons::MenuItem(type->createAssetMenuName.c_str()))
						{
							auto created = CreateProjectScriptableAsset(*type, type->createAssetFileName);
							workspaceError = created ? String{} : created.error;
						}
					}
				}
				ImGui::EndMenu();
			}
			const auto canRename = fileCommands.CanExecute("project.rename", menuCommandContext, menuCommandContext);
			ImGui::BeginDisabled(!canRename);
			if (Icons::MenuItem("Rename Selected", "F2"))
			{
				auto renamed = fileCommands.Execute("project.rename", menuCommandContext, menuCommandContext);
				workspaceError = renamed ? String{} : renamed.error;
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			DrawManagedMenus("Assets");
			ImGui::EndMenu();
		}
		if (Icons::BeginMenu("GameObject"))
		{
			const bool canTransformToView = sceneViewPanel && hasGameObjectSelection &&
			                                (play.Active() || (current && current->Kind() == ResourceKind::Scene));
			if (Icons::MenuItem("Align With View", "Ctrl+Shift+F", false, canTransformToView))
			{
				auto transformed = transformSelectionToView(true);
				workspaceError = transformed ? String{} : transformed.error;
			}
			if (Icons::MenuItem("Move To View", "Ctrl+Alt+F", false, canTransformToView))
			{
				auto transformed = transformSelectionToView(false);
				workspaceError = transformed ? String{} : transformed.error;
			}
			ImGui::Separator();
			EditorCommands commands;
			commands.Register({"game-object.create-empty",
				"Create Empty",
				"Ctrl+Shift+N",
			                   [current, playing = play.Active()](const EditorCommandContext& context)
			                   {
				                   return current && current->Kind() == ResourceKind::Scene && !playing &&
				                                  context.activeDocumentId == current->Id()
				                              ? Result<void>{}
				                              : Result<void>::Failure("Open an editable Scene to create a GameObject");
			                   },
			                   [createGameObject](const EditorCommandContext& context)
			                   { return createGameObject(context, false); }});
			commands.Register({"game-object.create-cube", "Cube", {},
			                   [current, playing = play.Active()](const EditorCommandContext& context)
			                   {
				                   return current && current->Kind() == ResourceKind::Scene && !playing &&
				                                  context.activeDocumentId == current->Id()
				                              ? Result<void>{}
				                              : Result<void>::Failure("Open an editable Scene to create a Cube");
			                   },
			                   [this, current](const EditorCommandContext&)
			                   {
				                   if (!documents || !current)
				                   {
					                   return Result<void>::Failure("The active document service is unavailable");
				                   }
				                   auto draft = current->Payload<SceneDraft>();
				                   if (!draft)
				                   {
					                   return Result<void>::Failure("The active Scene draft is unavailable");
				                   }
				                   auto created = CreatePrimitive(draft->GetScene(), documents->Session(), PrimitiveType::Cube);
				                   if (!created)
				                   {
					                   return Result<void>::Failure(created.error);
				                   }
				                   auto identity = draft->GetScene().PersistentId(created.value);
				                   if (!identity)
				                   {
					                   draft->GetScene().DestroyGameObject(created.value);
					                   return Result<void>::Failure(identity.error);
				                   }
				                   hierarchy.Select(draft->GetScene(), created.value);
				                   selectionContext.SelectGameObject(current->Id(), identity.value);
				                   current->MarkDirty();
					                   return Result<void>{};
			                   }});
			const auto canCreateSceneObject = [current, playing = play.Active()](const EditorCommandContext& context)
			{
				return current && current->Kind() == ResourceKind::Scene && !playing &&
				               context.activeDocumentId == current->Id()
				           ? Result<void>{}
				           : Result<void>::Failure("Open an editable Scene to create a GameObject");
			};
			commands.Register({"game-object.create-camera", "Camera", {}, canCreateSceneObject,
			                   [createComponentGameObject](const EditorCommandContext&)
			                   {
				                   LocalTransform transform;
				                   transform.position = {0.f, 1.f, -10.f};
				                   return createComponentGameObject(
				                       "Camera", transform,
				                       [](Scene& scene, const GameObject::Ptr& object)
				                       { return scene.AddComponent<CameraComponent>(object); });
			                   }});
			commands.Register({"game-object.create-directional-light", "Directional Light", {}, canCreateSceneObject,
			                   [createComponentGameObject](const EditorCommandContext&)
			                   {
				                   constexpr Float32 halfPiOver180 = 0.008726646259971648f;
				                   const Float32 halfPitch = 50.f * halfPiOver180;
				                   const Float32 halfYaw = -30.f * halfPiOver180;
				                   const Float32 pitchSin = std::sin(halfPitch);
				                   const Float32 pitchCos = std::cos(halfPitch);
				                   const Float32 yawSin = std::sin(halfYaw);
				                   const Float32 yawCos = std::cos(halfYaw);
				                   LocalTransform transform;
				                   transform.rotation = {yawCos * pitchSin, yawSin * pitchCos, -yawSin * pitchSin,
				                                         yawCos * pitchCos};
				                   return createComponentGameObject(
				                       "Directional Light", transform,
				                       [](Scene& scene, const GameObject::Ptr& object)
				                       { return scene.AddComponent<DirectionalLightComponent>(object); });
			                   }});
			commands.Register({"game-object.create-point-light", "Point Light", {}, canCreateSceneObject,
			                   [createComponentGameObject](const EditorCommandContext&)
			                   {
				                   return createComponentGameObject(
				                       "Point Light", {},
				                       [](Scene& scene, const GameObject::Ptr& object)
				                       { return scene.AddComponent<PointLightComponent>(object); });
			                   }});
			const auto canEditGameObjectSelection = [current, playing = play.Active()](const EditorCommandContext& context)
			{
				const bool valid = !context.selection.entries.empty() &&
				                   std::all_of(context.selection.entries.begin(), context.selection.entries.end(),
				                               [current](const EditorSelectionEntry& entry)
				                               {
					                               return current && entry.kind == EditorSelectionKind::GameObject &&
					                                              entry.documentId == current->Id() && !entry.playSession &&
					                                              !entry.IsMissing();
				                               });
				return current && current->Kind() == ResourceKind::Scene && !playing && valid
				           ? Result<void>{}
				           : Result<void>::Failure("Select editable GameObjects in the current Scene");
			};
			commands.Register({"game-object.toggle-active", "Toggle Active State", "Alt+Shift+A",
			                   canEditGameObjectSelection,
			                   [toggleGameObjectActive](const EditorCommandContext&) { return toggleGameObjectActive(); }});
			commands.Register({"game-object.create-empty-child", "Create Empty Child", "Alt+Shift+N",
			                   canEditGameObjectSelection,
			                   [createGameObject](const EditorCommandContext& context)
			                   { return createGameObject(context, true); }});
			const auto canCreateEmptyParent = [canEditGameObjectSelection](const EditorCommandContext& context)
			{
				auto editable = canEditGameObjectSelection(context);
				if (!editable)
				{
					return editable;
				}
				return context.selection.entries.size() == 1
				           ? Result<void>{}
				           : Result<void>::Failure("Create Empty Parent requires one selected GameObject");
			};
			commands.Register({"game-object.create-empty-parent", "Create Empty Parent", "Ctrl+Shift+G",
			                   canCreateEmptyParent,
			                   [createEmptyParent](const EditorCommandContext& context)
			                   { return createEmptyParent(context); }});
			commands.Register({"game-object.set-first-sibling", "Set as First Sibling", "Ctrl+=",
			                   canEditGameObjectSelection,
			                   [reorderGameObjects](const EditorCommandContext& context)
			                   { return reorderGameObjects(context, true); }});
			commands.Register({"game-object.set-last-sibling", "Set as Last Sibling", "Ctrl+-",
			                   canEditGameObjectSelection,
			                   [reorderGameObjects](const EditorCommandContext& context)
			                   { return reorderGameObjects(context, false); }});
			commands.Register({"game-object.duplicate", "Duplicate", "Ctrl+D", canEditGameObjectSelection,
			                   [this, current](const EditorCommandContext& context)
			                   {
					if (!documents || !project || !current)
				                   {
					                   return Result<void>::Failure("The active document service is unavailable");
				                   }
				                   auto draft = current->Payload<SceneDraft>();
				                   if (!draft)
				                   {
					                   return Result<void>::Failure("The active Scene draft is unavailable");
				                   }
				                   Vector<ResourceId> ids;
				                   for (const auto& entry : context.selection.entries)
				                   {
					                   ids.push_back(entry.objectId);
				                   }
				                   const auto* primary = context.selection.PrimaryEntry();
				                   auto duplicated = DuplicateGameObjectSelection(
					                       draft->GetScene(), ids, primary ? primary->objectId : ResourceId{},
					                       project->components, *documents, *draft, &selectionContext);
				                   if (duplicated)
				                   {
					                   current->MarkDirty();
				                   }
				                   return duplicated;
		                   }});
			commands.Register({"game-object.delete", "Delete", "Delete", canEditGameObjectSelection,
			                   [this, current](const EditorCommandContext& context)
			                   {
				                   if (!project || !current)
				                   {
					                   return Result<void>::Failure("There is no active Scene document");
				                   }
				                   auto draft = current->Payload<SceneDraft>();
				                   if (!draft)
				                   {
					                   return Result<void>::Failure("The active Scene draft is unavailable");
				                   }
				                   Vector<ResourceId> ids;
				                   for (const auto& entry : context.selection.entries)
				                   {
					                   ids.push_back(entry.objectId);
				                   }
				                   auto removed = DeleteGameObjectSelection(
				                       draft->GetScene(), ids, project->components, *draft, &selectionContext);
				                   if (removed)
					                   {
					                   current->MarkDirty();
					                   }
				                   return removed;
		                   }});
			const auto commandContext =
			    current ? EditorCommandContext::Capture(selectionContext, current->Id(), current->Revision())
			            : EditorCommandContext{};
			const auto canCreateObject =
			    commands.CanExecute("game-object.create-empty", commandContext, commandContext);
			ImGui::BeginDisabled(!canCreateObject);
			if (Icons::MenuItem("Create Empty", "Ctrl+Shift+N"))
			{
				const auto executed = commands.Execute("game-object.create-empty", commandContext, commandContext);
				workspaceError = executed ? String{} : executed.error;
			}
			ImGui::EndDisabled();
			if (Icons::BeginMenu("3D Object"))
			{
				const auto canCreateCube = commands.CanExecute("game-object.create-cube", commandContext, commandContext);
				if (Icons::MenuItem("Cube", nullptr, false, static_cast<bool>(canCreateCube)))
				{
					const auto created = commands.Execute("game-object.create-cube", commandContext, commandContext);
					workspaceError = created ? String{} : created.error;
				}
				ImGui::EndMenu();
			}
			const auto canCreateCamera =
			    commands.CanExecute("game-object.create-camera", commandContext, commandContext);
			if (Icons::MenuItem("Camera", nullptr, false, static_cast<bool>(canCreateCamera)))
			{
				const auto created = commands.Execute("game-object.create-camera", commandContext, commandContext);
				workspaceError = created ? String{} : created.error;
			}
			if (Icons::BeginMenu("Light"))
			{
				const auto canCreateDirectional =
				    commands.CanExecute("game-object.create-directional-light", commandContext, commandContext);
				if (Icons::MenuItem("Directional Light", nullptr, false, static_cast<bool>(canCreateDirectional)))
				{
					const auto created =
					    commands.Execute("game-object.create-directional-light", commandContext, commandContext);
					workspaceError = created ? String{} : created.error;
				}
				const auto canCreatePoint =
				    commands.CanExecute("game-object.create-point-light", commandContext, commandContext);
				if (Icons::MenuItem("Point Light", nullptr, false, static_cast<bool>(canCreatePoint)))
				{
					const auto created = commands.Execute("game-object.create-point-light", commandContext, commandContext);
					workspaceError = created ? String{} : created.error;
				}
				ImGui::EndMenu();
			}
			const auto canCreateChild =
			    commands.CanExecute("game-object.create-empty-child", commandContext, commandContext);
			if (Icons::MenuItem("Create Empty Child", "Alt+Shift+N", false, static_cast<bool>(canCreateChild)))
			{
				const auto created = commands.Execute("game-object.create-empty-child", commandContext, commandContext);
				workspaceError = created ? String{} : created.error;
			}
			const auto canCreateParent =
			    commands.CanExecute("game-object.create-empty-parent", commandContext, commandContext);
			if (Icons::MenuItem("Create Empty Parent", "Ctrl+Shift+G", false,
			                    static_cast<bool>(canCreateParent)))
			{
				const auto created = commands.Execute("game-object.create-empty-parent", commandContext, commandContext);
				workspaceError = created ? String{} : created.error;
			}
			const auto canDuplicate = commands.CanExecute("game-object.duplicate", commandContext, commandContext);
			if (Icons::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(canDuplicate)))
			{
				const auto duplicated = commands.Execute("game-object.duplicate", commandContext, commandContext);
				workspaceError = duplicated ? String{} : duplicated.error;
			}
			const auto canDelete = commands.CanExecute("game-object.delete", commandContext, commandContext);
			if (Icons::MenuItem("Delete", "Delete", false, static_cast<bool>(canDelete)))
			{
				const auto removed = commands.Execute("game-object.delete", commandContext, commandContext);
				workspaceError = removed ? String{} : removed.error;
			}
			const auto canToggleActive =
			    commands.CanExecute("game-object.toggle-active", commandContext, commandContext);
			if (Icons::MenuItem("Toggle Active State", "Alt+Shift+A", false, static_cast<bool>(canToggleActive)))
			{
				const auto toggled = commands.Execute("game-object.toggle-active", commandContext, commandContext);
				workspaceError = toggled ? String{} : toggled.error;
			}
			const auto canSetFirst =
			    commands.CanExecute("game-object.set-first-sibling", commandContext, commandContext);
			if (Icons::MenuItem("Set as First Sibling", "Ctrl+=", false, static_cast<bool>(canSetFirst)))
			{
				const auto reordered = commands.Execute("game-object.set-first-sibling", commandContext, commandContext);
				workspaceError = reordered ? String{} : reordered.error;
			}
			const auto canSetLast =
			    commands.CanExecute("game-object.set-last-sibling", commandContext, commandContext);
			if (Icons::MenuItem("Set as Last Sibling", "Ctrl+-", false, static_cast<bool>(canSetLast)))
			{
				const auto reordered = commands.Execute("game-object.set-last-sibling", commandContext, commandContext);
				workspaceError = reordered ? String{} : reordered.error;
			}
			DrawManagedMenus("GameObject");
			ImGui::EndMenu();
		}
		if (Icons::BeginMenu("Component"))
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
					if (!Icons::BeginMenu(category.c_str()))
					{
						category.clear();
						continue;
					}
				}
				auto available = selectedObject ? project->componentEditors.CanCreate(type.type, context)
				                                : Result<void>::Failure("Select a GameObject first");
				ImGui::BeginDisabled(!available || play.Active());
				if (Icons::MenuItem(type.displayName.c_str()))
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
		if (Icons::BeginMenu("Window"))
		{
			if (Icons::MenuItem("Project Graphics Settings", nullptr, showGraphicsSettings))
			{
				showGraphicsSettings = !showGraphicsSettings;
			}
			if (Icons::MenuItem("Tags and Layers", nullptr, showTagLayerSettings))
			{
				showTagLayerSettings = !showTagLayerSettings;
			}
			ImGui::Separator();
			if (Icons::MenuItem("Reset Layout"))
			{
				ResetEditorLayout();
			}
			ImGui::Separator();
			const auto openPanel = [](bool& visible, const char* windowName)
			{
				visible = true;
				ImGui::SetWindowFocus(windowName);
			};
			if (Icons::MenuItem("Project", "Ctrl+5"))
			{
				openPanel(showProjectPanel, "Project");
			}
			if (Icons::MenuItem("Hierarchy", "Ctrl+4"))
			{
				openPanel(showHierarchyPanel, "Hierarchy");
			}
			if (Icons::MenuItem("Inspector", "Ctrl+3"))
			{
				openPanel(showInspectorPanel, "Inspector");
			}
			if (Icons::MenuItem("Scene", "Ctrl+1"))
			{
				openPanel(showScenePanel, "Scene");
			}
			if (Icons::MenuItem("Game", "Ctrl+2"))
			{
				openPanel(showGamePanel, "Game");
			}
			if (Icons::MenuItem("Console", "Ctrl+Shift+C"))
			{
				openPanel(showConsolePanel, "Console");
			}
			DrawManagedMenus("Window");
			ImGui::EndMenu();
		}
		if (Icons::BeginMenu("Help"))
		{
			Icons::MenuItem("Ursa Editor Documentation", nullptr, false, false);
			Icons::MenuItem("About Ursa Editor", nullptr, false, false);
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
		if (Icons::BeginMenu("Tools"))
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
					if (!Icons::BeginMenu(category.c_str()))
					{
						category.clear();
						continue;
					}
				}
				auto available = project->tools.CanExecute(tool.id, context);
				ImGui::BeginDisabled(play.Active() || !available);
				if (Icons::MenuItem(tool.displayName.c_str()))
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
			    (extension.kind == ManagedExtensionKind::Menu || extension.kind == ManagedExtensionKind::Window))
			{
				managedMenuRoots.insert(root);
			}
		}
		for (const String& root : managedMenuRoots)
		{
			if (Icons::BeginMenu(root.c_str()))
			{
				DrawManagedMenus(root);
				ImGui::EndMenu();
			}
		}
		if (scriptBuildService && Icons::BeginMenu("Scripts"))
		{
			const bool compiling = scriptBuildJob.valid() || scriptCheckJob.valid() || scriptIdeJob.valid();
			ImGui::BeginDisabled(compiling || play.Active());
			if (Icons::MenuItem("Compile Scripts"))
			{
				CompileScripts();
			}
			ImGui::EndDisabled();
			ImGui::BeginDisabled(scriptIdeJob.valid() || scriptEditorJob.valid() || ideGenerationRequested);
			if (Icons::MenuItem("Open C# Project"))
			{
				auto opened = OpenCSharpProject();
				scriptIdeStatus = opened ? String("等待打开 C# 工程……") : opened.error;
			}
			if (Icons::MenuItem("Regenerate C# Project"))
			{
				auto generated = OpenCSharpProject({}, 1, 1, false);
				scriptIdeStatus = generated ? String("等待重新生成 C# 工程……") : generated.error;
				showExternalTools = true;
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
		if (editorShortcutsAllowed && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
		{
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
			{
				auto saved = fileCommands.Execute("file.save", menuCommandContext, menuCommandContext);
				workspaceError = saved ? String{} : saved.error;
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false))
			{
				auto refreshed = fileCommands.Execute("assets.refresh", menuCommandContext, menuCommandContext);
				workspaceError = refreshed ? String{} : refreshed.error;
			}
		}
	}
	DrawPlayToolbar(current);
}

Result<void> EditorApplication::OpenProjectBrowserAsset(const ProjectBrowserEntry& asset)
{
	if (!workspace || !documents || asset.folder)
	{
		return Result<void>::Failure("Project asset cannot be opened in the current workspace");
	}
	if (!selectionContext.SelectAsset(asset.path))
	{
		pendingImporterOpen = asset.path;
		return {};
	}
	projectBrowser.SelectAsset(asset.path);
	if (PathFromUrsaString(asset.path.relative).extension() == ".cs")
	{
		return OpenCSharpProject(asset.path);
	}
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
	if (PathFromUrsaString(asset.path.relative).extension() == ".ursaasset")
	{
		return {};
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
		auto preview = textureImportView->Reimport(*workspace->Database(), workspace->Importer(), device, *gui, true);
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
		auto preview = modelImportView->Reimport(*workspace->Database(), workspace->Importer(), true);
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
	Result<void> renamed = workspace->Database()->Move(source, destination);
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
	if (documents)
	{
		refreshed = documents->FollowMovedSource();
		if (!refreshed)
		{
			return refreshed;
		}
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

Result<void> EditorApplication::ImportExternalProjectFiles(const Vector<String>& paths, const String& destinationFolder)
{
	if (!workspace || !workspace->Database() || play.Active() || paths.empty())
	{
		return Result<void>::Failure("Drop files into Project after opening a project and stopping Play mode");
	}
	const std::filesystem::path assetsRoot = workspace->AssetsRoot().lexically_normal();
	const std::filesystem::path destination = (assetsRoot / ToStdString(destinationFolder)).lexically_normal();
	const std::filesystem::path relativeDestination = destination.lexically_relative(assetsRoot);
	if (relativeDestination.empty() || relativeDestination.is_absolute() ||
	    *relativeDestination.begin() == std::filesystem::path(".."))
	{
		return Result<void>::Failure("The Project drop destination is outside the Assets folder");
	}
	std::error_code error;
	std::filesystem::create_directories(destination, error);
	if (error)
	{
		return Result<void>::Failure("Could not create the Project drop folder: " + ToUrsaString(error.message()));
	}
	const auto canonicalAssetsRoot = std::filesystem::weakly_canonical(assetsRoot, error);
	const auto canonicalDestination = std::filesystem::weakly_canonical(destination, error);
	if (error || canonicalDestination.lexically_relative(canonicalAssetsRoot).empty() ||
	    *canonicalDestination.lexically_relative(canonicalAssetsRoot).begin() == std::filesystem::path(".."))
	{
		return Result<void>::Failure("The Project drop destination resolves outside the Assets folder");
	}
	auto uniquePath = [](const std::filesystem::path& requested)
	{
		if (!std::filesystem::exists(requested))
		{
			return requested;
		}
		const auto parent = requested.parent_path();
		const auto extension = requested.has_extension() ? requested.extension() : std::filesystem::path{};
		const auto stem = requested.has_extension() ? requested.stem() : requested.filename();
		for (UInt32 suffix = 1;; ++suffix)
		{
			const auto candidate = parent / (stem.string() + " (" + std::to_string(suffix) + ")" + extension.string());
			if (!std::filesystem::exists(candidate))
			{
				return candidate;
			}
		}
	};
	for (const String& droppedPath : paths)
	{
		const std::filesystem::path source = std::filesystem::path(ToStdString(droppedPath)).lexically_normal();
		const auto sourceStatus = std::filesystem::symlink_status(source, error);
		if (error || std::filesystem::is_symlink(sourceStatus))
		{
			return Result<void>::Failure("Could not read dropped path: " + droppedPath);
		}
		if (std::filesystem::is_regular_file(sourceStatus))
		{
			const auto target = uniquePath(destination / source.filename());
			std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error);
			if (error)
			{
				return Result<void>::Failure("Could not import " + droppedPath + ": " + ToUrsaString(error.message()));
			}
			continue;
		}
		if (!std::filesystem::is_directory(sourceStatus))
		{
			continue;
		}
		const auto targetRoot = uniquePath(destination / source.filename());
		std::filesystem::create_directories(targetRoot, error);
		if (error)
		{
			return Result<void>::Failure("Could not create imported folder: " + ToUrsaString(error.message()));
		}
		for (std::filesystem::recursive_directory_iterator iterator(source,
		                                                           std::filesystem::directory_options::skip_permission_denied,
		                                                           error),
		     end;
		     iterator != end; iterator.increment(error))
		{
			if (error)
			{
				error.clear();
				continue;
			}
			const auto status = iterator->symlink_status(error);
			if (error || std::filesystem::is_symlink(status))
			{
				iterator.disable_recursion_pending();
				error.clear();
				continue;
			}
			const auto relative = std::filesystem::relative(iterator->path(), source, error);
			if (error)
			{
				return Result<void>::Failure("Could not resolve imported folder contents: " + ToUrsaString(error.message()));
			}
			const auto target = targetRoot / relative;
			if (std::filesystem::is_directory(status))
			{
				std::filesystem::create_directories(target, error);
			}
			else if (std::filesystem::is_regular_file(status))
			{
				std::filesystem::create_directories(target.parent_path(), error);
				if (!error)
				{
					std::filesystem::copy_file(iterator->path(), target, std::filesystem::copy_options::none, error);
				}
			}
			if (error)
			{
				return Result<void>::Failure("Could not import folder contents: " + ToUrsaString(error.message()));
			}
		}
	}
	RequestAssetImport();
	auto refreshed = workspace->Refresh();
	if (!refreshed)
	{
		return refreshed;
	}
	refreshed = projectBrowser.Refresh(*workspace);
	if (refreshed)
	{
		projectBrowser.ReconcileSelection(selectionContext);
	}
	return refreshed;
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

Result<void> EditorApplication::ExportScenePrefabs(const EditorDragSession& session, const String& destinationFolder)
{
	if (!documents || !documents->Current() || !workspace || !workspace->Database() || play.Active() ||
	    session.sources.empty())
	{
		return Result<void>::Failure("Export Scene objects to Prefabs after opening a Scene and stopping Play mode");
	}
	auto* document = documents->Current();
	auto draft = document->Payload<SceneDraft>();
	if (!draft || draft->Kind() != ResourceKind::Scene)
	{
		return Result<void>::Failure("Only objects from an editable Scene can be exported as Prefabs");
	}
	Scene& scene = draft->GetScene();
	Vector<GameObject::Ptr> selected;
	Set<String> selectedIds;
	for (const auto& source : session.sources)
	{
		const auto& identity = source.identity;
		if (identity.kind != EditorSelectionKind::GameObject || identity.documentId != document->Id() ||
		    identity.IsMissing())
		{
			return Result<void>::Failure("Prefab export requires GameObjects from the active Scene");
		}
		auto object = scene.GameObjectById(identity.objectId);
		if (!object)
		{
			return Result<void>::Failure(object.error);
		}
		if (selectedIds.insert(identity.objectId.ToString()).second)
		{
			selected.push_back(object.value);
		}
	}
	Vector<GameObject::Ptr> roots;
	for (const auto& object : selected)
	{
		auto parent = scene.ParentOf(object);
		if (!parent)
		{
			return Result<void>::Failure(parent.error);
		}
		bool covered = false;
		while (parent.value)
		{
			if (selectedIds.count(parent.value->Id().ToString()))
			{
				covered = true;
				break;
			}
			parent = scene.ParentOf(parent.value);
			if (!parent)
			{
				return Result<void>::Failure(parent.error);
			}
		}
		if (!covered)
		{
			roots.push_back(object);
		}
	}
	if (roots.empty())
	{
		return Result<void>::Failure("The Scene selection contains no outermost GameObjects to export");
	}

	struct PendingPrefab
	{
		VirtualPath path;
		GameObject::Ptr root;
		ResourceId id;
		PrefabAsset asset;
	};

	Vector<PendingPrefab> pending;
	Set<String> names;
	auto& filesystem = workspace->Database()->Filesystem();
	const auto cleanup = [&]
	{
		for (const auto& item : pending)
		{
			(void)filesystem.Remove(item.path);
			(void)filesystem.Remove(VirtualPath{item.path.scheme, item.path.relative + ".meta"});
		}
		(void)workspace->Refresh();
		(void)projectBrowser.Refresh(*workspace);
	};
	for (const auto& root : roots)
	{
		auto nameResult = root->Name();
		if (!nameResult)
		{
			return Result<void>::Failure(nameResult.error);
		}
		String base = nameResult.value;
		for (auto& ch : base)
		{
			if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
			    ch == '|')
			{
				ch = '_';
			}
		}
		if (base.empty() || base == "." || base == "..")
		{
			base = "GameObject";
		}
		for (UInt32 suffix = 1;; ++suffix)
		{
			const String stem = suffix == 1 ? base : base + " " + ToUrsaString(std::to_string(suffix));
			const String relative =
			    destinationFolder.empty() ? stem + ".ursaprefab" : destinationFolder + "/" + stem + ".ursaprefab";
			if (names.count(relative))
			{
				continue;
			}
			auto path = ProjectAssetPath(relative.c_str());
			if (!path)
			{
				return Result<void>::Failure(path.error);
			}
			auto exists = filesystem.Exists(path.value);
			if (!exists)
			{
				return Result<void>::Failure(exists.error);
			}
			if (exists.value)
			{
				continue;
			}
			auto sidecarExists = filesystem.Exists(VirtualPath{path.value.scheme, path.value.relative + ".meta"});
			if (!sidecarExists)
			{
				return Result<void>::Failure(sidecarExists.error);
			}
			if (sidecarExists.value)
			{
				return Result<void>::Failure("Cannot export over an orphaned Project metadata file: " + relative);
			}
			auto writable = filesystem.CheckWritable(path.value);
			if (!writable)
			{
				return Result<void>::Failure(writable.error);
			}
			names.insert(relative);
			pending.push_back({path.value, root, {}, {}});
			break;
		}
	}
	for (const auto& item : pending)
	{
		auto placeholder = filesystem.WriteAtomic(item.path, {});
		if (!placeholder)
		{
			cleanup();
			return placeholder;
		}
	}
	auto refreshed = workspace->Refresh();
	if (!refreshed)
	{
		cleanup();
		return refreshed;
	}
	for (auto& item : pending)
	{
		auto metadata = workspace->Database()->Metadata(item.path);
		if (!metadata)
		{
			cleanup();
			return Result<void>::Failure(metadata.error);
		}
		item.id = metadata.value.guid;
		ComponentLoadContext context(documents->Session());
		if (draft->ContextSetup())
		{
			auto setup = draft->ContextSetup()(context);
			if (!setup)
			{
				cleanup();
				return setup;
			}
		}
		const MaterialIdResolver materialId = [this](const std::shared_ptr<MaterialInstance>& material)
		{ return documents->Session().IdentifyMaterial(material); };
		auto captured = CapturePrefabAsset(item.id, scene, item.root, project->components, context, materialId);
		if (!captured)
		{
			cleanup();
			return Result<void>::Failure("Cannot export '" + item.path.relative + "': " + captured.error);
		}
		Scene detached;
		auto detachedRoot = captured.value.Instantiate(detached, project->components, context, materialId);
		if (!detachedRoot)
		{
			cleanup();
			return Result<void>::Failure("Cannot remap '" + item.path.relative + "': " + detachedRoot.error);
		}
		auto remapped =
		    CapturePrefabAsset(item.id, detached, detachedRoot.value, project->components, context, materialId);
		if (!remapped)
		{
			cleanup();
			return Result<void>::Failure("Cannot preserve references in '" + item.path.relative +
			                             "': " + remapped.error);
		}
		item.asset = std::move(remapped.value);
	}
	for (const auto& item : pending)
	{
		auto saved = SavePrefabAssetResource(filesystem, item.path, item.asset, project->components,
		                                     [this](const std::shared_ptr<MaterialInstance>& material)
		                                     { return documents->Session().IdentifyMaterial(material); });
		if (!saved)
		{
			cleanup();
			return Result<void>::Failure("Cannot write '" + item.path.relative + "': " + saved.error);
		}
	}
	refreshed = workspace->Refresh();
	if (!refreshed)
	{
		cleanup();
		return refreshed;
	}
	auto browserRefresh = projectBrowser.Refresh(*workspace);
	if (!browserRefresh)
	{
		cleanup();
		return browserRefresh;
	}
	auto first = workspace->Database()->Metadata(pending.front().path);
	if (first)
	{
		projectBrowser.SelectAsset(pending.front().path);
		selectionContext.SelectAsset(pending.front().path, 0, {}, first.value.guid);
		hierarchy.ClearSelection();
	}
	return {};
}

Result<void> EditorApplication::CreateProjectScriptableAsset(const ScriptTypeDescriptor& type,
                                                             const String& requestedName)
{
	if (!workspace || !workspace->Database() || play.Active() || type.group != ScriptAssemblyGroup::Game ||
	    type.kind != ScriptTypeKind::ScriptableObject || type.sourceGuid == ResourceId{} || type.localFileId == 0 ||
	    type.scriptType.empty() || type.version == 0 || type.createAssetMenuName.empty() || requestedName.empty())
	{
		return Result<void>::Failure("The selected CreateAssetMenu type is no longer valid");
	}
	if (std::any_of(type.fields.begin(), type.fields.end(),
	                [](const ScriptFieldTypeDescriptor& field)
	                {
		                return field.kind == "Unsupported" ||
		                       ((field.kind == "Array" || field.kind == "List") && field.elementKind == "Unsupported");
	                }))
	{
		return Result<void>::Failure("CreateAssetMenu type contains fields that cannot be serialized: " +
		                             type.clrTypeName);
	}
	String fileName = requestedName;
	if (PathFromUrsaString(fileName).extension() != ".ursaasset")
	{
		fileName += ".ursaasset";
	}
	const String relativePath =
	    projectBrowser.CurrentFolder().empty() ? fileName : projectBrowser.CurrentFolder() + "/" + fileName;
	auto path = ProjectAssetPath(relativePath.c_str());
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	VirtualFileSystem& filesystem = workspace->Database()->Filesystem();
	auto exists = filesystem.Exists(path.value);
	if (!exists)
	{
		return Result<void>::Failure(exists.error);
	}
	if (exists.value)
	{
		return Result<void>::Failure("Project asset already exists: " + path.value.ToString());
	}
	if (!filesystem.CheckWritable(path.value))
	{
		return Result<void>::Failure("Project asset folder is read-only: " + path.value.ToString());
	}
	const auto removeIncompleteAsset = [&]()
	{
		(void)filesystem.Remove(path.value);
		(void)filesystem.Remove(VirtualPath::Parse(path.value.ToString() + ".meta").value);
		(void)workspace->Database()->Refresh();
	};
	auto placeholder = filesystem.WriteAtomic(path.value, {});
	if (!placeholder)
	{
		return placeholder;
	}
	auto refreshed = workspace->Database()->Refresh();
	if (!refreshed)
	{
		removeIncompleteAsset();
		return refreshed;
	}
	auto metadata = workspace->Database()->Metadata(path.value);
	if (!metadata)
	{
		removeIncompleteAsset();
		return Result<void>::Failure(metadata.error);
	}
	auto asset = CreateScriptableAssetWithDefaults(metadata.value.guid, type);
	if (!asset)
	{
		removeIncompleteAsset();
		return Result<void>::Failure(asset.error);
	}
	auto saved = SaveScriptableAssetSource(filesystem, path.value, asset.value);
	if (!saved)
	{
		removeIncompleteAsset();
		return saved;
	}
	refreshed = workspace->Database()->Refresh();
	if (!refreshed)
	{
		removeIncompleteAsset();
		return refreshed;
	}
	RequestAssetImport(path.value, true);
	auto browserRefresh = projectBrowser.Refresh(*workspace);
	if (!browserRefresh)
	{
		return browserRefresh;
	}
	projectBrowser.SelectAsset(path.value);
	selectionContext.SelectAsset(path.value);
	return {};
}

Result<void> EditorApplication::ApplyGraphicsSettings(AssetReference pipeline)
{
	if (!workspace)
	{
		return Result<void>::Failure("Open a project before changing Graphics settings");
	}
	VirtualFileSystem& files = workspace->Index().Filesystem();
	const auto settingsPath = VirtualPath::Parse(Rendering::graphicsSettingsSource);
	if (!settingsPath)
	{
		return Result<void>::Failure(settingsPath.error);
	}
	if (!files.CheckWritable(settingsPath.value))
	{
		return Result<void>::Failure("Project Graphics settings are read-only");
	}
	auto previous = Rendering::ReadGraphicsSettings(files);
	if (!previous)
	{
		return Result<void>::Failure(previous.error);
	}
	if (pipeline.guid)
	{
		if (!scriptBuildService)
		{
			return Result<void>::Failure("Project C# type catalog is unavailable");
		}
		auto valid = Rendering::ValidatePipelineAsset(files, workspace->AssetResolver(),
		                                              scriptBuildService->ScriptTypes(), pipeline);
		if (!valid)
		{
			return valid;
		}
	}
#if defined(URSA_ENABLE_DOTNET)
	if (!managedHost || !editingScriptSession || !scriptBuildService)
	{
		return Result<void>::Failure("Reload the project scripts before applying Graphics settings");
	}
	auto configured = managedHost->ConfigureRenderPipeline(editingScriptSession, pipeline);
	if (!configured)
	{
		String label = "Default Forward";
		if (pipeline.guid)
		{
			auto path = workspace->Database()->SourcePath(pipeline.guid);
			label = path ? path.value.ToString() : pipeline.guid.ToString();
		}
		return Result<void>::Failure("Render pipeline candidate " + label + " failed: " + configured.error);
	}
#else
	if (pipeline.guid)
	{
		return Result<void>::Failure("This Editor build cannot run project C# render pipelines");
	}
#endif
	auto saved = Rendering::SaveGraphicsSettings(files, {pipeline},
	                                             [&](AssetReference candidate)
	                                             {
		                                             return Rendering::ValidatePipelineAsset(
		                                                 files, workspace->AssetResolver(),
		                                                 scriptBuildService->ScriptTypes(), candidate);
	                                             });
	if (!saved)
	{
#if defined(URSA_ENABLE_DOTNET)
		auto restored = managedHost->ConfigureRenderPipeline(editingScriptSession, previous.value.renderPipeline);
		if (!restored)
		{
			return Result<void>::Failure(saved.error + "; previous pipeline could not be restored: " + restored.error);
		}
#endif
		return saved;
	}
	graphicsSettingsCandidate = pipeline;
	if (assetPreview)
	{
		assetPreview->Reload(true);
	}
	RequestAssetImport();
	return {};
}

Result<void> EditorApplication::RenderManagedViewport(Scene& renderScene,
                                                      const std::shared_ptr<Rendering::RenderTextureResource>& target,
                                                      std::optional<Rendering::CameraType> cameraFilter,
                                                      std::optional<Rendering::CameraData> cameraOverride,
                                                      Vector<ResourceId> excludedObjects)
{
#if defined(URSA_ENABLE_DOTNET)
	if (!target || !target->description.width || !target->description.height)
	{
		return Result<void>::Failure("Managed viewport needs a valid output target");
	}
	std::shared_ptr<Scripting::DotNetHost> host;
	Scripting::ManagedSceneApi* stableApi = nullptr;
	UInt64 session = 0;
	if (play.Active())
	{
		auto services = std::static_pointer_cast<ManagedPlaySessionState>(play.ServiceOwner());
		if (!services || !services->host || !services->sceneApi || !play.Resources())
		{
			return Result<void>::Failure("Run render services are unavailable");
		}
		host = services->host;
		stableApi = services->sceneApi.get();
		session = services->sessionId;
	}
	else
	{
		if (!managedHost || !editingScriptSession || !editingScriptServices)
		{
			return Result<void>::Failure("Editing render services are unavailable");
		}
		auto services = std::static_pointer_cast<ManagedEditingSessionState>(editingScriptServices);
		if (!services || !services->sceneApi || !services->resources)
		{
			return Result<void>::Failure("Editing render resources are unavailable");
		}
		host = managedHost;
		stableApi = services->sceneApi.get();
		session = editingScriptSession;
	}
	auto bound = host->BindScriptSession(session, *stableApi);
	if (!bound)
	{
		return bound;
	}
	stableApi->SetRenderSceneOverride(&renderScene);
	stableApi->SetRenderOutput(target);
	stableApi->SetRenderCameraSelection(cameraFilter, std::move(cameraOverride));
	stableApi->SetRenderObjectExclusions(std::move(excludedObjects));
	auto rendered =
	    host->RenderScene(session, Float32(target->description.width) / Float32(target->description.height));
	stableApi->SetRenderObjectExclusions({});
	stableApi->SetRenderSceneOverride(nullptr);
	stableApi->SetRenderCameraSelection({});
	stableApi->SetRenderOutput(nullptr);
	return rendered;
#else
	(void)renderScene;
	(void)target;
	(void)cameraFilter;
	(void)cameraOverride;
	(void)excludedObjects;
	return Result<void>::Failure("Project pipeline viewports require the .NET rendering host");
#endif
}

void EditorApplication::DrawGraphicsSettings()
{
	if (!showGraphicsSettings)
	{
		return;
	}
	if (!ImGui::Begin("Project Graphics Settings", &showGraphicsSettings))
	{
		ImGui::End();
		return;
	}
	if (!workspace)
	{
		ImGui::TextDisabled("Open a project to configure its render pipeline.");
		ImGui::End();
		return;
	}
	const String projectKey = PathToUrsaString(workspace->ProjectRoot());
	if (graphicsSettingsProject != projectKey)
	{
		graphicsSettingsProject = projectKey;
		graphicsSettingsCandidate = {};
		graphicsSettingsDiagnostic.clear();
		auto settings = Rendering::ReadGraphicsSettings(workspace->Index().Filesystem());
		if (settings)
		{
			graphicsSettingsCandidate = settings.value.renderPipeline;
			if (graphicsSettingsCandidate.guid && scriptBuildService)
			{
				auto valid =
				    Rendering::ValidatePipelineAsset(workspace->Index().Filesystem(), workspace->AssetResolver(),
				                                     scriptBuildService->ScriptTypes(), graphicsSettingsCandidate);
				if (!valid)
				{
					graphicsSettingsDiagnostic = valid.error;
				}
			}
		}
		else
		{
			graphicsSettingsDiagnostic = settings.error;
		}
	}
	ImGui::TextUnformatted("Default Render Pipeline");
	String selectedLabel = "Default Forward";
	if (graphicsSettingsCandidate.guid)
	{
		selectedLabel = "Missing or invalid pipeline " + graphicsSettingsCandidate.guid.ToString() + ":" +
		                ToUrsaString(std::to_string(graphicsSettingsCandidate.localFileId));
		if (workspace->Database())
		{
			auto path = workspace->Database()->SourcePath(graphicsSettingsCandidate.guid);
			if (path)
			{
				selectedLabel = path.value.FileName();
			}
		}
	}
	if (ImGui::BeginCombo("##DefaultRenderPipeline", selectedLabel.c_str()))
	{
		if (ImGui::Selectable("Default Forward", !graphicsSettingsCandidate.guid))
		{
			graphicsSettingsCandidate = {};
		}
		if (workspace->Database() && scriptBuildService)
		{
			for (const AssetRecord& record : workspace->Database()->Assets())
			{
				if (record.metadata.folder || PathFromUrsaString(record.path.relative).extension() != ".ursaasset")
				{
					continue;
				}
				auto asset =
				    LoadScriptableAssetSource(workspace->Index().Filesystem(), record.path, record.metadata.guid);
				if (!asset)
				{
					continue;
				}
				auto type =
				    std::find_if(scriptBuildService->ScriptTypes().begin(), scriptBuildService->ScriptTypes().end(),
				                 [&](const ScriptTypeDescriptor& descriptor)
				                 { return descriptor.scriptType == asset.value.TypeId(); });
				if (type == scriptBuildService->ScriptTypes().end() || !type->isRenderPipeline)
				{
					continue;
				}
				AssetReference reference{record.metadata.guid, asset.value.LocalFileId()};
				auto valid =
				    Rendering::ValidatePipelineAsset(workspace->Index().Filesystem(), workspace->AssetResolver(),
				                                     scriptBuildService->ScriptTypes(), reference);
				if (!valid)
				{
					continue;
				}
				const String label = record.path.ToString() + "##" + record.metadata.guid.ToString();
				if (ImGui::Selectable(label.c_str(),
				                      reference.guid == graphicsSettingsCandidate.guid &&
				                          reference.localFileId == graphicsSettingsCandidate.localFileId))
				{
					graphicsSettingsCandidate = reference;
				}
			}
		}
		ImGui::EndCombo();
	}
	ImGui::BeginDisabled(AssetWorkPending() || !workspace->Database());
	if (Icons::Button("Apply Graphics Settings"))
	{
		auto applied = ApplyGraphicsSettings(graphicsSettingsCandidate);
		graphicsSettingsDiagnostic = applied ? String{} : applied.error;
	}
	ImGui::EndDisabled();
	if (AssetWorkPending())
	{
		ImGui::TextDisabled("Wait for asset import to finish before applying the current selection.");
	}
	if (!graphicsSettingsDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", graphicsSettingsDiagnostic.c_str());
	}
	ImGui::End();
}

void EditorApplication::DrawTagLayerSettings()
{
	if (!workspace)
	{
		if (showTagLayerSettings)
		{
			if (ImGui::Begin("Tags and Layers", &showTagLayerSettings))
			{
				ImGui::TextDisabled("Open a project to configure its tags and layers.");
			}
			ImGui::End();
		}
		return;
	}
	const String projectKey = PathToUrsaString(workspace->ProjectRoot());
	if (tagLayerSettingsProject != projectKey)
	{
		tagLayerSettingsProject = projectKey;
		tagLayerSettings = ProjectTagLayerSettings::Defaults();
		tagLayerSettingsDiagnostic.clear();
		auto loaded = ProjectTagLayerSettings::Read(workspace->Index().Filesystem());
		if (loaded)
		{
			tagLayerSettings = std::move(loaded.value);
		}
		else
		{
			tagLayerSettingsDiagnostic = loaded.error;
		}
		selectedTagIndex = 0;
		tagInput.fill('\0');
		tagEditInput.fill('\0');
		for (Size index = 0; index < tagLayerSettings.layers.size(); ++index)
		{
			const auto& name = tagLayerSettings.layers[index];
			std::memcpy(layerEditInputs[index].data(), name.data(), std::min<Size>(name.size(), 64));
			layerEditInputs[index][std::min<Size>(name.size(), 64)] = '\0';
		}
	}
	if (!showTagLayerSettings)
	{
		return;
	}
	if (!ImGui::Begin("Tags and Layers", &showTagLayerSettings))
	{
		ImGui::End();
		return;
	}
	const auto saveCandidate = [&](const ProjectTagLayerSettings& candidate) -> Result<void>
	{
		auto saved = candidate.Save(workspace->Index().Filesystem());
		if (saved)
		{
			tagLayerSettings = candidate;
			tagLayerSettingsDiagnostic.clear();
		}
		else
		{
			tagLayerSettingsDiagnostic = saved.error;
		}
		return saved;
	};
	if (ImGui::BeginTabBar("##TagsLayersTabs"))
	{
		const ImGuiTabItemFlags tagTabFlags = requestedTagLayerSettingsTab == 0 ? ImGuiTabItemFlags_SetSelected : 0;
		if (ImGui::BeginTabItem("Tags", nullptr, tagTabFlags))
		{
			if (requestedTagLayerSettingsTab == 0)
			{
				requestedTagLayerSettingsTab = -1;
			}
			ImGui::TextDisabled("Tags assigned to GameObjects in this project.");
			ImGui::BeginChild("##TagList", ImVec2(0.f, 145.f), ImGuiChildFlags_Borders);
			for (Size index = 0; index < tagLayerSettings.tags.size(); ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				if (ImGui::Selectable(tagLayerSettings.tags[index].c_str(),
				                      selectedTagIndex == static_cast<int>(index)))
				{
					selectedTagIndex = static_cast<int>(index);
					std::memset(tagEditInput.data(), 0, tagEditInput.size());
					std::memcpy(tagEditInput.data(), tagLayerSettings.tags[index].data(),
					            std::min<Size>(tagLayerSettings.tags[index].size(), tagEditInput.size() - 1));
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::BeginDisabled(selectedTagIndex <= 0 ||
			                     static_cast<Size>(selectedTagIndex) >= tagLayerSettings.tags.size());
			ImGui::SetNextItemWidth(220.f);
			ImGui::InputText("Selected tag", tagEditInput.data(), tagEditInput.size());
			if (ImGui::Button("Rename") && selectedTagIndex > 0 &&
			    static_cast<Size>(selectedTagIndex) < tagLayerSettings.tags.size())
			{
				auto candidate = tagLayerSettings;
				candidate.tags[static_cast<Size>(selectedTagIndex)] = ToUrsaString(tagEditInput.data());
				saveCandidate(candidate);
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove") && selectedTagIndex > 0 &&
			    static_cast<Size>(selectedTagIndex) < tagLayerSettings.tags.size())
			{
				auto candidate = tagLayerSettings;
				candidate.tags.erase(candidate.tags.begin() + selectedTagIndex);
				selectedTagIndex = 0;
				saveCandidate(candidate);
			}
			ImGui::EndDisabled();
			ImGui::SetNextItemWidth(220.f);
			ImGui::InputText("##NewTag", tagInput.data(), tagInput.size());
			ImGui::SameLine();
			if (ImGui::Button("Add Tag"))
			{
				auto candidate = tagLayerSettings;
				candidate.tags.push_back(ToUrsaString(tagInput.data()));
				if (saveCandidate(candidate))
				{
					tagInput.fill('\0');
					selectedTagIndex = static_cast<int>(candidate.tags.size() - 1);
					std::memcpy(tagEditInput.data(), candidate.tags.back().data(), candidate.tags.back().size());
				}
			}
			ImGui::EndTabItem();
		}
		const ImGuiTabItemFlags layerTabFlags = requestedTagLayerSettingsTab == 1 ? ImGuiTabItemFlags_SetSelected : 0;
		if (ImGui::BeginTabItem("Layers", nullptr, layerTabFlags))
		{
			if (requestedTagLayerSettingsTab == 1)
			{
				requestedTagLayerSettingsTab = -1;
			}
			ImGui::TextDisabled("Layer indices are stable and range from 0 to 31.");
			if (ImGui::BeginTable("##LayerTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
			{
				ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 58.f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				for (Size index = 0; index < tagLayerSettings.layers.size(); ++index)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%zu", index);
					ImGui::TableSetColumnIndex(1);
					ImGui::PushID(static_cast<int>(index));
					ImGui::SetNextItemWidth(-FLT_MIN);
					const bool fixedDefault = index == 0;
					ImGui::BeginDisabled(fixedDefault);
					const bool edited =
					    ImGui::InputText("##LayerName", layerEditInputs[index].data(), layerEditInputs[index].size());
					ImGui::EndDisabled();
					if (!fixedDefault && edited && ImGui::IsItemDeactivatedAfterEdit())
					{
						auto candidate = tagLayerSettings;
						candidate.layers[index] = ToUrsaString(layerEditInputs[index].data());
						if (!saveCandidate(candidate))
						{
							const auto& previous = tagLayerSettings.layers[index];
							std::memset(layerEditInputs[index].data(), 0, layerEditInputs[index].size());
							std::memcpy(layerEditInputs[index].data(), previous.data(), previous.size());
						}
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	if (!tagLayerSettingsDiagnostic.empty())
	{
		ImGui::TextWrapped("%s", tagLayerSettingsDiagnostic.c_str());
	}
	ImGui::End();
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
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, EditorToolbarHeight(viewport->WorkSize.x)), ImGuiCond_Always);
	constexpr ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
	                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
	                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::Begin("##EditorToolbar", nullptr, toolbarFlags);
	ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - 110.0f) * 0.5f));
	const bool canRun = selectedDocument && selectedDocument->Kind() == ResourceKind::Scene &&
	                    !documents->PromptPending() && !scriptIdeJob.valid() && !ideGenerationRequested;
	if (!play.Active() && (requestRun || runAfterScriptBuild) &&
	    (!canRun || (pendingRunDocument && selectedDocument->Id() != pendingRunDocument)))
	{
		requestRun = false;
		runAfterScriptBuild = false;
		pendingRunDocument = {};
	}
	const bool awaitingPlay = !play.Active() && (requestRun || runAfterScriptBuild);
	const bool playingOrRequested = play.Active() || awaitingPlay;
	const ImGuiIO& playInput = ImGui::GetIO();
	const bool playShortcutInputAllowed = !playInput.WantTextInput && !ImGui::IsAnyItemActive();
	const bool shortcutCtrl = playInput.KeyCtrl || playInput.KeySuper;
	const bool playToggleShortcut = playShortcutInputAllowed && shortcutCtrl && !playInput.KeyShift &&
	                                !playInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_P, false) &&
	                                (play.Active() || canRun || awaitingPlay);
	ImGui::BeginDisabled(!play.Active() && !canRun);
	const bool runClicked =
	    Icons::Button("Run", playingOrRequested ? Icons::Icon::Stop : Icons::Icon::Play,
	                  play.Active() ? "Stop"
	                  : awaitingPlay ? "Cancel Play"
	                                 : "Play",
	                  playingOrRequested) ||
	    playToggleShortcut;
	const bool runAction = runClicked || requestRun;
	if (runClicked && awaitingPlay)
	{
		requestRun = false;
		runAfterScriptBuild = false;
		pendingRunDocument = {};
	}
	else if (runAction && HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
	{
		requestRun = false;
		RequestCancelReferencePickerSessions(gui ? gui->Context() : nullptr, [this] { requestRun = true; });
	}
	else if (runAction && !play.Active() && scriptBuildJob.valid())
	{
		requestRun = false;
		runAfterScriptBuild = true;
		pendingRunDocument = selectedDocument->Id();
	}
	else if (runAction && !play.Active() && scriptCheckJob.valid())
	{
		// Keep the periodic source scan from dimming Play, but serialize a requested start behind it.
		requestRun = true;
		if (!pendingRunDocument)
		{
			pendingRunDocument = selectedDocument->Id();
		}
	}
	else if (runAction)
	{
		requestRun = false;
		pendingRunDocument = {};
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
			focusedGameViewport = nullptr;
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
					consolePanel.NotifyBuildStarted();
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
								VirtualPath gameContent = gameAssemblyPath;
								playSetup = [this, userPlaySetup, managed, gameContent](
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
									auto graphicsSettings =
									    Rendering::ReadGraphicsSettings(workspace->Index().Filesystem());
									if (!graphicsSettings)
									{
										return Result<std::shared_ptr<void>>::Failure(graphicsSettings.error);
									}
									if (graphicsSettings.value.renderPipeline.guid)
									{
										if (!scriptBuildService)
										{
											return Result<std::shared_ptr<void>>::Failure(
											    "Project Graphics selects a pipeline but the C# type catalog is "
											    "unavailable");
										}
										auto validPipeline = Rendering::ValidatePipelineAsset(
										    workspace->Index().Filesystem(), workspace->AssetResolver(),
										    scriptBuildService->ScriptTypes(), graphicsSettings.value.renderPipeline);
										if (!validPipeline)
										{
											return Result<std::shared_ptr<void>>::Failure(validPipeline.error);
										}
									}
									context.SetSceneReadyCallback(
									    [this, session, pipeline = graphicsSettings.value.renderPipeline](
									        Scene& scene, ComponentLoadContext& loadContext) -> Result<void>
									    {
										    // Accept game title requests during Play while retaining the editor document title.
										    session->sceneApi = std::make_unique<Scripting::ManagedSceneApi>(
										        scene, &loadContext.Resources(), [](const String&) {});
										    session->sceneApi->SetRenderDevice(device);
										    auto bound = session->host->BindScriptSession(session->sessionId,
										                                                  *session->sceneApi);
										    if (!bound)
										    {
											    return bound;
										    }
										    auto configured =
										        session->host->ConfigureRenderPipeline(session->sessionId, pipeline);
										    if (!configured)
										    {
											    return Result<void>::Failure(
											        "Project Graphics pipeline could not be configured for Run: " +
											        configured.error);
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
					auto runtimeSelection = selectionContext.entries;
					for (auto& entry : runtimeSelection)
					{
						if (entry.kind == EditorSelectionKind::GameObject && entry.documentId == selectedDocument->Id())
						{
							entry.playSession = true;
						}
					}
					selectionContext.ReconcileEntries(std::move(runtimeSelection));
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
	ImGui::BeginDisabled(!play.Active());
	{
		Icons::SameLine();
		const bool pauseShortcut = playShortcutInputAllowed && shortcutCtrl && playInput.KeyShift &&
		                           !playInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_P, false) && play.Active();
		if (Icons::Button("Pause", Icons::Icon::Pause, play.Paused() ? "Resume" : "Pause", play.Paused()) ||
		    pauseShortcut)
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
		Icons::SameLine();
		ImGui::BeginDisabled(!play.Paused());
		const bool stepShortcut = playShortcutInputAllowed && shortcutCtrl && !playInput.KeyShift &&
		                          playInput.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_P, false) && play.Paused();
		if (Icons::Button("Step", Icons::Icon::Step, "Step") || stepShortcut)
		{
			play.Step();
		}
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();
	if (!scriptBuildStatus.empty())
	{
		Icons::SameLine();
		const bool compiling = scriptBuildJob.valid();
		const bool buildFailed =
		    !scriptDiagnostics.empty() &&
		    std::any_of(scriptDiagnostics.begin(), scriptDiagnostics.end(), [](const ScriptDiagnostic& diagnostic)
		                { return diagnostic.severity == "error" || diagnostic.severity == "Error"; });
		if (Icons::Button("ScriptBuildStatus",
		                  compiling     ? Icons::Icon::Build
		                  : buildFailed ? Icons::Icon::Error
		                                : Icons::Icon::Info,
		                  "Script build diagnostics"))
		{
			ImGui::OpenPopup("Script Build Diagnostics");
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", scriptBuildStatus.c_str());
		}
		if (ImGui::BeginPopup("Script Build Diagnostics"))
		{
			ImGui::SetNextWindowSize({700.0f, 360.0f}, ImGuiCond_Appearing);
			for (Size index = 0; index < scriptDiagnostics.size(); ++index)
			{
				const auto& diagnostic = scriptDiagnostics[index];
				ImGui::PushID(static_cast<Int32>(index));
				const String label = diagnostic.severity + " " + diagnostic.code + " " + diagnostic.source.ToString() +
				                     ":" + ToUrsaString(std::to_string(diagnostic.line)) + ":" +
				                     ToUrsaString(std::to_string(diagnostic.column));
				if (!diagnostic.source.scheme.empty() && diagnostic.line && diagnostic.column)
				{
					if (ImGui::Selectable(label.c_str()))
					{
						auto opened = OpenScriptDiagnostic(index);
						if (!opened)
						{
							scriptIdeStatus = opened.error;
							showExternalTools = true;
						}
					}
				}
				else
				{
					ImGui::TextUnformatted(label.c_str());
				}
				ImGui::TextWrapped("%s", diagnostic.message.c_str());
				ImGui::PopID();
			}
			if (scriptDiagnostics.empty() || ImGui::CollapsingHeader("Build status and tool output"))
			{
				ImGui::TextUnformatted(scriptBuildStatus.c_str());
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

Result<void> EditorApplication::OnGui()
{
	RequestCancelReferencePickerForExternalClick(gui ? gui->Context() : nullptr);
	if (assetPreview)
	{
		auto begun = assetPreview->BeginFrame();
		if (!begun)
		{
			return begun;
		}
	}
	PumpScriptIde();
	if (closeDocumentAfterPickerCancellation)
	{
		closeDocumentAfterPickerCancellation = false;
		if (documents)
		{
			auto closed = documents->RequestClose();
			workspaceError = closed ? String{} : closed.error;
		}
	}
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
		scriptDiagnostics = scriptBuildService ? scriptBuildService->LastDiagnostics() : Vector<ScriptDiagnostic>{};
		consolePanel.AddCompilerDiagnostics(scriptDiagnostics);
		if (!result)
		{
			scriptBuildStatus = result.error;
			runAfterScriptBuild = false;
			requestRun = false;
			pendingRunDocument = {};
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
					requestRun = false;
					pendingRunDocument = {};
				}
				else if (runAfterScriptBuild)
				{
					requestRun = documents && documents->Current() && documents->Current()->Id() == pendingRunDocument;
					runAfterScriptBuild = false;
					if (!requestRun)
					{
						pendingRunDocument = {};
					}
				}
			}
			if (!result.value.succeeded)
			{
				runAfterScriptBuild = false;
				requestRun = false;
				pendingRunDocument = {};
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
		else if (checked.value != observedScriptSources && !play.Active() && !scriptBuildJob.valid())
		{
			observedScriptSources = std::move(checked.value);
			ScriptBuildService* service = scriptBuildService.get();
			consolePanel.NotifyRecompileStarted();
			scriptBuildJob =
			    std::async(std::launch::async, [service] { return service->Build(ScriptAssemblyGroup::Editor); });
			if (requestRun && documents && documents->Current() &&
			    (!pendingRunDocument || documents->Current()->Id() == pendingRunDocument))
			{
				runAfterScriptBuild = true;
				pendingRunDocument = documents->Current()->Id();
				requestRun = false;
			}
			scriptBuildStatus = "检测到脚本变化，正在后台编译……";
		}
	}
	if (selectionContext.kind == EditorSelectionKind::Asset && documents && documents->Current() &&
	    documents->Current()->Kind() == ResourceKind::Scene &&
	    documents->Current()->Path() == selectionContext.assetPath)
	{
		selectionContext.Clear();
	}
	const auto checkTime = std::chrono::steady_clock::now();
	if (startupPlay && !scriptBuildJob.valid() && !scriptCheckJob.valid() && !scriptIdeJob.valid())
	{
		requestRun = true;
	}
	if (scriptBuildService && !startupPlay && !requestRun && !scriptBuildJob.valid() && !scriptCheckJob.valid() &&
	    !scriptIdeJob.valid() && !assetScanJob.valid() &&
	    (!assetImportJob.valid() || assetImportJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) &&
	    checkTime >= nextScriptCheck)
	{
		ScriptBuildService* service = scriptBuildService.get();
		scriptCheckJob = std::async(std::launch::async,
		                            [service]() -> Result<String>
		                            {
			                            auto ide = service->GenerateIdeProjects();
			                            return ide ? service->SourceFingerprint() : Result<String>::Failure(ide.error);
		                            });
		nextScriptCheck = checkTime + std::chrono::seconds(2);
	}
	const auto drawDockspace = [this]
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const Float32 toolbarHeight = EditorToolbarHeight(viewport->WorkSize.x);
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
			ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Right, 0.20f, &inspectorDock, &leftDock);
			ImGuiID hierarchyProjectDock = 0;
			ImGuiID sceneDock = 0;
			ImGui::DockBuilderSplitNode(leftDock, ImGuiDir_Right, 0.275f, &hierarchyProjectDock, &sceneDock);
			ImGuiID consoleDock = 0;
			ImGui::DockBuilderSplitNode(sceneDock, ImGuiDir_Down, 0.30f, &consoleDock, &sceneDock);
			ImGuiID projectDock = 0;
			ImGuiID hierarchyDock = 0;
			ImGui::DockBuilderSplitNode(hierarchyProjectDock, ImGuiDir_Down, 0.515f, &projectDock, &hierarchyDock);
			ImGui::DockBuilderDockWindow("Project", projectDock);
			ImGui::DockBuilderDockWindow("Console", consoleDock);
			ImGui::DockBuilderDockWindow("Hierarchy", hierarchyDock);
			ImGui::DockBuilderDockWindow("Scene", sceneDock);
			ImGui::DockBuilderDockWindow("Game", sceneDock);
			ImGui::DockBuilderDockWindow("Inspector", inspectorDock);
			ImGui::DockBuilderFinish(dockspace);
			dockLayoutInitialized = true;
		}
		ImGui::End();
	};
	DrawMainMenuBar();
	drawDockspace();
	DrawExternalTools();
	DrawGraphicsSettings();
	DrawTagLayerSettings();
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
		if (extension.kind != ManagedExtensionKind::Window || !managedOpenWindows.count(extension.id))
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
	if (!workspace || !showProjectPanel)
	{
		pendingProjectBrowserFileDrops.clear();
	}
	if (workspace && showProjectPanel)
	{
		Vector<std::pair<EditorDragSession, String>> pendingScenePrefabExports;
		ProjectBrowserActions actions;
		actions.refresh = [this]()
		{
			RequestAssetImport();
			auto refreshed = workspace->Refresh();
			if (refreshed)
			{
				refreshed = projectBrowser.Refresh(*workspace);
			}
			if (refreshed)
			{
				projectBrowser.ReconcileSelection(selectionContext);
			}
			return refreshed;
		};
		actions.createFolder = [this](const String& name) { return CreateProjectFolder(name); };
		actions.createAsset = [this](ResourceKind kind, MaterialType materialType, const String& name)
		{ return CreateProjectAsset(kind, materialType, name); };
		actions.scriptableAssetTypes = scriptBuildService ? &scriptBuildService->ScriptTypes() : nullptr;
		actions.createScriptableAsset = [this](const ScriptTypeDescriptor& type, const String& name)
		{ return CreateProjectScriptableAsset(type, name); };
		actions.renameAsset = [this](const VirtualPath& source, const VirtualPath& destination)
		{ return RenameProjectAsset(source, destination); };
		actions.takeExternalFileDrops = [this]()
		{
			Vector<ProjectBrowserFileDrop> drops = std::move(pendingProjectBrowserFileDrops);
			pendingProjectBrowserFileDrops.clear();
			return drops;
		};
		actions.importExternalFiles = [this](const Vector<String>& paths, const String& destinationFolder)
		{ return ImportExternalProjectFiles(paths, destinationFolder); };
		actions.reimportAssets = [this](const Vector<VirtualPath>& sources)
		{
			if (!workspace || play.Active() || sources.empty())
			{
				return Result<void>::Failure("Reimport project assets after opening a project and stopping Play mode");
			}
			RequestAssetReimport(sources);
			return Result<void>{};
		};
		actions.duplicateAssets = [this](const Vector<VirtualPath>& sources)
		{
			if (!workspace || !workspace->Database() || play.Active() || sources.empty())
			{
				return Result<Vector<VirtualPath>>::Failure("Duplicate project assets after opening a project and stopping Play mode");
			}
			auto* database = workspace->Database();
			Vector<VirtualPath> destinations;
			Set<String> reserved;
			for (const VirtualPath& source : sources)
			{
				auto metadata = database->Metadata(source);
				if (!metadata || source.scheme != database->SourceScheme())
				{
					return Result<Vector<VirtualPath>>::Failure(metadata ? "Only project source assets can be duplicated"
					                                                    : metadata.error);
				}
				auto writable = database->Filesystem().CheckWritable(source);
				if (!writable)
				{
					return Result<Vector<VirtualPath>>::Failure(writable.error);
				}
				const Size slash = source.relative.rfind('/');
				const String parent = slash == String::npos ? String{} : source.relative.substr(0, slash + 1);
				const String filename = source.relative.substr(slash == String::npos ? 0 : slash + 1);
				Size dot = filename.rfind('.');
				if (metadata.value.folder || dot == 0 || dot == String::npos)
				{
					dot = filename.size();
				}
				const String stem = filename.substr(0, dot);
				const String extension = filename.substr(dot);
				VirtualPath destination;
				bool found = false;
				for (UInt32 suffix = 1; suffix < 10000; ++suffix)
				{
					const String suffixText = std::to_string(suffix).c_str();
					auto candidate = VirtualPath::Parse(source.scheme + ":" + parent + stem + " " + suffixText + extension);
					if (!candidate)
					{
						return Result<Vector<VirtualPath>>::Failure(candidate.error);
					}
					const String identity = candidate.value.ToString();
					if (reserved.find(identity) != reserved.end())
					{
						continue;
					}
					auto exists = database->Filesystem().Exists(candidate.value);
					auto sidecar = VirtualPath::Parse(candidate.value.ToString() + ".meta");
					auto sidecarExists = sidecar ? database->Filesystem().Exists(sidecar.value)
					                            : Result<bool>::Failure(sidecar.error);
					if (!exists || !sidecarExists)
					{
						return Result<Vector<VirtualPath>>::Failure(!exists ? exists.error : sidecarExists.error);
					}
					if (!exists.value && !sidecarExists.value)
					{
						destination = std::move(candidate.value);
						reserved.insert(identity);
						found = true;
						break;
					}
				}
				if (!found)
				{
					return Result<Vector<VirtualPath>>::Failure("Could not find a free duplicate name for " + source.ToString());
				}
				auto copied = database->Copy(source, destination);
				if (!copied)
				{
					return Result<Vector<VirtualPath>>::Failure(copied.error);
				}
				destinations.push_back(std::move(destination));
			}
			auto refreshed = workspace->Refresh();
			if (!refreshed)
			{
				return Result<Vector<VirtualPath>>::Failure(refreshed.error);
			}
			RequestAssetImport();
			return Result<Vector<VirtualPath>>::Success(std::move(destinations));
		};
		actions.pasteAssets = [this](const Vector<VirtualPath>& sources, const String& destinationFolder, bool move)
		{
			if (!workspace || !workspace->Database() || play.Active() || sources.empty())
			{
				return Result<Vector<VirtualPath>>::Failure("Paste project assets after opening a project and stopping Play mode");
			}
			auto* database = workspace->Database();
			if (!destinationFolder.empty())
			{
				auto folder = VirtualPath::Parse(database->SourceScheme() + ":" + destinationFolder);
				auto metadata = folder ? database->Metadata(folder.value)
				                      : Result<AssetMetadata>::Failure(folder.error);
				if (!metadata || !metadata.value.folder)
				{
					return Result<Vector<VirtualPath>>::Failure(
					    metadata ? "Paste target is not a project folder" : metadata.error);
				}
			}
			Vector<VirtualPath> destinations;
			Vector<AssetMoveRequest> moves;
			Set<String> reserved;
			for (const VirtualPath& source : sources)
			{
				auto metadata = database->Metadata(source);
				if (!metadata || source.scheme != database->SourceScheme())
				{
					return Result<Vector<VirtualPath>>::Failure(metadata ? "Only project source assets can be pasted"
					                                                 : metadata.error);
				}
				auto writable = database->Filesystem().CheckWritable(source);
				if (!writable)
				{
					return Result<Vector<VirtualPath>>::Failure(writable.error);
				}
				if (move && metadata.value.folder &&
				    (destinationFolder == source.relative ||
				     (destinationFolder.size() > source.relative.size() &&
				      destinationFolder.compare(0, source.relative.size(), source.relative) == 0 &&
				      destinationFolder[source.relative.size()] == '/')))
				{
					return Result<Vector<VirtualPath>>::Failure("Cannot move a folder into itself or one of its descendants");
				}
				const Size slash = source.relative.rfind('/');
				const String filename = source.relative.substr(slash == String::npos ? 0 : slash + 1);
				const String parent = destinationFolder.empty() ? String{} : destinationFolder + "/";
				String candidateName = filename;
				VirtualPath destination;
				bool found = false;
				for (UInt32 suffix = 0; suffix < 10000; ++suffix)
				{
					if (!move && suffix > 0)
					{
						Size dot = filename.rfind('.');
						if (metadata.value.folder || dot == 0 || dot == String::npos)
						{
							dot = filename.size();
						}
						candidateName = filename.substr(0, dot) + " " + std::to_string(suffix).c_str() + filename.substr(dot);
					}
					const String relative = parent + candidateName;
					if (move && relative == source.relative)
					{
						return Result<Vector<VirtualPath>>::Failure("Paste target is the asset's current location");
					}
					if (reserved.find(relative) != reserved.end())
					{
						if (move)
						{
							return Result<Vector<VirtualPath>>::Failure("Multiple cut assets have the same name in the target folder");
						}
						continue;
					}
					auto parsed = VirtualPath::Parse(source.scheme + ":" + relative);
					if (!parsed)
					{
						return Result<Vector<VirtualPath>>::Failure(parsed.error);
					}
					auto exists = database->Filesystem().Exists(parsed.value);
					auto sidecar = VirtualPath::Parse(parsed.value.ToString() + ".meta");
					auto sidecarExists = sidecar ? database->Filesystem().Exists(sidecar.value)
					                            : Result<bool>::Failure(sidecar.error);
					if (!exists || !sidecarExists)
					{
						return Result<Vector<VirtualPath>>::Failure(!exists ? exists.error : sidecarExists.error);
					}
					if (move ? (!exists.value && !sidecarExists.value) : (!exists.value && !sidecarExists.value))
					{
						destination = std::move(parsed.value);
						reserved.insert(relative);
						found = true;
						break;
					}
					if (move)
					{
						return Result<Vector<VirtualPath>>::Failure("Move target already exists: " + relative);
					}
				}
				if (!found)
				{
					return Result<Vector<VirtualPath>>::Failure("Could not find an available paste name for " + source.ToString());
				}
				destinations.push_back(destination);
				if (move)
				{
					moves.push_back({source, destination});
				}
			}
			if (move)
			{
				auto moved = database->MoveBatch(moves);
				if (!moved)
				{
					return Result<Vector<VirtualPath>>::Failure(moved.error);
				}
			}
			else
			{
				Vector<VirtualPath> copied;
				for (Size index = 0; index < sources.size(); ++index)
				{
					auto result = database->Copy(sources[index], destinations[index]);
					if (!result)
					{
						if (!copied.empty())
						{
							auto rollback = database->RemoveBatch(copied);
							return Result<Vector<VirtualPath>>::Failure(
							    result.error + (rollback ? String{} : " (rollback failed: " + rollback.error + ")"));
						}
						return Result<Vector<VirtualPath>>::Failure(result.error);
					}
					copied.push_back(destinations[index]);
				}
			}
			auto refreshed = workspace->Refresh();
			if (!refreshed)
			{
				return Result<Vector<VirtualPath>>::Failure(refreshed.error);
			}
			if (move && documents)
			{
				refreshed = documents->FollowMovedSource();
				if (!refreshed)
				{
					return Result<Vector<VirtualPath>>::Failure(refreshed.error);
				}
			}
			RequestAssetImport();
			return Result<Vector<VirtualPath>>::Success(std::move(destinations));
		};
		actions.moveAssets = [this](const Vector<AssetMoveRequest>& moves)
		{
			if (!workspace || !workspace->Database() || play.Active())
			{
				return Result<void>::Failure("Move project assets after opening a project and stopping Play mode");
			}
			auto moved = workspace->Database()->MoveBatch(moves);
			if (!moved)
			{
				return moved;
			}
			auto refreshed = workspace->Refresh();
			if (!refreshed)
			{
				return refreshed;
			}
			if (documents)
			{
				refreshed = documents->FollowMovedSource();
				if (!refreshed)
				{
					return refreshed;
				}
			}
			RequestAssetImport();
			return Result<void>{};
		};
		actions.deleteAssets = [this](const Vector<VirtualPath>& paths)
		{
			if (!workspace || !workspace->Database() || play.Active())
			{
				return Result<void>::Failure("Delete project assets after opening a project and stopping Play mode");
			}
			if (documents && documents->PromptPending())
			{
				return Result<void>::Failure("Resolve the current document prompt before deleting Project assets");
			}
			const auto* current = documents ? documents->Current() : nullptr;
			bool containsCurrentDocument = false;
			if (current)
			{
				for (const VirtualPath& path : paths)
				{
					auto metadata = workspace->Database()->Metadata(path);
					if (current->Path() == path ||
					    (metadata && metadata.value.folder && current->Path().scheme == path.scheme &&
					     current->Path().relative.size() > path.relative.size() &&
					     current->Path().relative.compare(0, path.relative.size(), path.relative) == 0 &&
					     current->Path().relative[path.relative.size()] == '/'))
					{
						containsCurrentDocument = true;
						break;
					}
				}
			}
			if (containsCurrentDocument && documents->Current()->Dirty())
			{
				auto requested = documents->RequestClose();
				return Result<void>::Failure(
				    requested ? "Choose Save, Discard, or Cancel for the open document, then confirm Delete"
				              : requested.error);
			}
			auto removed = workspace->Database()->RemoveBatch(paths);
			if (!removed)
			{
				return removed;
			}
			if (containsCurrentDocument && documents->Current())
			{
				auto closed = documents->RequestClose();
				if (!closed)
				{
					return closed;
				}
			}
			auto refreshed = workspace->Refresh();
			if (!refreshed)
			{
				return refreshed;
			}
			RequestAssetImport();
			refreshed = projectBrowser.Refresh(*workspace);
			if (!refreshed)
			{
				return refreshed;
			}
			selectionContext.Clear();
			hierarchy.ClearSelection();
			return Result<void>{};
		};
		actions.openAsset = [this](const ProjectBrowserEntry& asset) { return OpenProjectBrowserAsset(asset); };
		actions.exportScenePrefabs =
		    [&pendingScenePrefabExports](const EditorDragSession& session, const String& folder)
		{
			pendingScenePrefabExports.emplace_back(session, folder);
			return Result<void>{};
		};
		auto drawn = projectBrowserPanel.Draw(*workspace, projectBrowser, selectionContext, hierarchy, showProjectPanel,
		                                      play.Active(), workspaceError, actions);
		if (!drawn)
		{
			workspaceError = drawn.error;
		}
		else
		{
			for (const auto& [session, folder] : pendingScenePrefabExports)
			{
				auto exported = ExportScenePrefabs(session, folder);
				if (!exported)
				{
					workspaceError = exported.error;
				}
			}
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
		if (Icons::Button("Reimport Shader"))
		{
			RequestAssetImport(shaderImportView->SourcePath(), true);
		}
		ImGui::EndDisabled();
		if (shaderImportView->Imported())
		{
			Icons::SameLine();
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
	if (showInspectorPanel && selectionContext.kind != EditorSelectionKind::Asset && documents &&
	    documents->Current() && documents->Current()->Kind() == ResourceKind::Texture)
	{
		auto inspected = DrawTextureInspector(*documents->Current(), inspectorError);
		if (!inspected)
		{
			workspaceError = inspected.error;
		}
	}
	if (showInspectorPanel && selectionContext.kind != EditorSelectionKind::Asset && documents &&
	    documents->Current() && documents->Current()->Kind() == ResourceKind::Material)
	{
		ImGuiInspectorUI ui;
		auto inspected = DrawMaterialInspector(*documents->Current(), *documents, *workspace, inspectorError,
		                                       &project->materialInspectors, &ui);
		if (!inspected)
		{
			workspaceError = inspected.error;
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
				auto hierarchyResult =
				    hierarchy.Draw(*play.GetScene(), &showHierarchyPanel, draft->LinkedScene(), &selectionContext,
				                   documents->Current()->Id(), true, documents->Current()->Revision(), {}, {}, {},
				                   documents->Current()->Path().FileName(),
				                   workspace ? &workspace->Index().Filesystem() : nullptr);
				if (!hierarchyResult)
				{
					inspectorError = hierarchyResult.error;
				}
			}
			else
			{
				struct PendingAssetDrop
				{
					EditorDragSession session;
					ResourceId target;
					HierarchyDropZone zone;
					ResourceId document;
				};

				Vector<PendingAssetDrop> pendingAssetDrops;
				EditorHierarchy::AssetDropAction queueAssetDrop =
				    [&pendingAssetDrops](Scene&, const EditorDragSession& drag, ResourceId target,
				                         HierarchyDropZone zone, ResourceId documentId)
				{
					pendingAssetDrops.push_back({drag, target, zone, documentId});
					return Result<void>{};
				};
				auto inspected = DrawSceneInspector(
				    *documents->Current(), hierarchy, *documents, *workspace, tagLayerSettings, project->components,
				    project->componentEditors, inspectorError,
				    scriptBuildService ? &scriptBuildService->ScriptTypes() : nullptr, &showHierarchyPanel,
				    &showInspectorPanel, selectionContext.kind != EditorSelectionKind::Asset && showInspectorPanel,
					&selectionContext, std::move(queueAssetDrop), [this](bool layers)
					{
						showTagLayerSettings = true;
						requestedTagLayerSettingsTab = layers ? 1 : 0;
					}, CanCopyGameObjectSelection(), CanPasteGameObjectSelectionAsChild(),
					[this] { return CopyGameObjectSelection(); },
					[this] { return CutGameObjectSelection(); },
					[this] { return PasteGameObjectSelection(true); });
				if (!inspected)
				{
					workspaceError = inspected.error;
				}
				else
				{
					for (const auto& pending : pendingAssetDrops)
					{
						Vector<SceneAssetPlacement> placements;
						String placementError;
						if (!documents->Current() || pending.document != documents->Current()->Id())
						{
							placementError = "The Scene document changed before asset placement";
						}
						Set<String> seen;
						for (const auto& source : pending.session.sources)
						{
							if (!placementError.empty())
							{
								break;
							}
							const auto& identity = source.identity;
							if (identity.kind != EditorSelectionKind::Asset ||
							    identity.assetRole != EditorAssetSelectionRole::Source || identity.assetLocalFileId ||
							    !identity.assetGuid || identity.IsMissing())
							{
								placementError = "Hierarchy accepts only Prefab or Model main assets";
								break;
							}
							if (!seen.insert(identity.assetGuid.ToString()).second)
							{
								continue;
							}
							auto artifact = workspace->Database()->ResolveImportedArtifact(identity.assetGuid);
							if (!artifact || (artifact.value.kind != "Prefab" && artifact.value.kind != "Model"))
							{
								placementError =
								    artifact ? "Hierarchy accepts only Prefab or Model main assets" : artifact.error;
								break;
							}
							SceneAssetPlacement item{identity.assetGuid, artifact.value.kind, nlohmann::json::object()};
							if (item.kind == "Model")
							{
								auto bytes = workspace->Index().Filesystem().Read(artifact.value.path);
								if (!bytes)
								{
									placementError = bytes.error;
									break;
								}
								try
								{
									item.modelDocument =
									    nlohmann::json::from_cbor(bytes.value.begin(), bytes.value.end());
								}
								catch (const std::exception& error)
								{
									placementError = ToUrsaString(error.what());
									break;
								}
								if (item.modelDocument.value("format", std::string{}) != "UrsaModel" ||
								    !item.modelDocument.contains("nodes") || !item.modelDocument["nodes"].is_array())
								{
									placementError = "Imported Model main asset is invalid";
									break;
								}
							}
							placements.push_back(std::move(item));
						}
						if (placementError.empty() && placements.empty())
						{
							placementError = "The drag contains no placeable main assets";
						}
						ResourceId parentId = pending.target;
						Size insertIndex = std::numeric_limits<Size>::max();
						if (placementError.empty() && pending.target && pending.zone != HierarchyDropZone::Into)
						{
							auto target = draft->GetScene().GameObjectById(pending.target);
							if (!target)
							{
								placementError = target.error;
							}
							else
							{
								auto parent = draft->GetScene().ParentOf(target.value);
								if (!parent)
								{
									placementError = parent.error;
								}
								else if (parent.value)
								{
									auto parentPersistentId = draft->GetScene().PersistentId(parent.value);
									if (!parentPersistentId)
									{
										placementError = parentPersistentId.error;
									}
									else
									{
										parentId = parentPersistentId.value;
									}
								}
								else
								{
									parentId = {};
								}
								Vector<GameObject::Ptr> siblings;
								if (parent.value)
								{
									auto children = draft->GetScene().ChildrenOf(parent.value);
									if (!children)
									{
										placementError = children.error;
									}
									else
									{
										siblings = std::move(children.value);
									}
								}
								else
								{
									siblings = draft->GetScene().RootGameObjects();
								}
								auto position = std::find(siblings.begin(), siblings.end(), target.value);
								if (position == siblings.end())
								{
									placementError = "Hierarchy target changed before asset placement";
								}
								else
								{
									insertIndex = static_cast<Size>(std::distance(siblings.begin(), position)) +
									              (pending.zone == HierarchyDropZone::Below ? 1 : 0);
								}
							}
						}
						else if (placementError.empty() && pending.target)
						{
							auto parent = draft->GetScene().GameObjectById(pending.target);
							if (!parent)
							{
								placementError = parent.error;
							}
							else
							{
								auto children = draft->GetScene().ChildrenOf(parent.value);
								if (!children)
								{
									placementError = children.error;
								}
								else
								{
									insertIndex = children.value.size();
								}
							}
						}
						else if (placementError.empty())
						{
							insertIndex = draft->GetScene().RootGameObjects().size();
						}
						Result<Vector<ResourceId>> placed =
						    placementError.empty()
						        ? draft->PlaceImportedAssets(placements, parentId, insertIndex,
						                                     documents->Current()->Path(), documents->Current()->Id())
						        : Result<Vector<ResourceId>>::Failure(placementError);
						if (!placed)
						{
							placementError = placed.error;
						}
						if (!placementError.empty())
						{
							inspectorError = placementError;
						}
					}
				}
			}
			VirtualFileSystem* settingsFiles = workspace ? &workspace->Index().Filesystem() : nullptr;
			const String projectKey = workspace ? ProjectSettingsKey(workspace->ProjectRoot()) : String("default");
			const ResourceId selectionBeforeSceneView = hierarchy.SelectedId();
			const auto placeSceneAssets = [this, draft](const EditorDragSession& drag, Vec3 position) -> Result<void>
			{
				if (!workspace || !documents || !documents->Current() || play.Active() ||
				    draft->Kind() != ResourceKind::Scene)
				{
					return Result<void>::Failure("Drop assets into an open Scene while Play mode is stopped");
				}
				Vector<SceneAssetPlacement> placements;
				Set<String> seen;
				for (const auto& source : drag.sources)
				{
					const auto& identity = source.identity;
					if (identity.kind != EditorSelectionKind::Asset ||
					    identity.assetRole != EditorAssetSelectionRole::Source || identity.assetLocalFileId ||
					    !identity.assetGuid || identity.IsMissing())
					{
						return Result<void>::Failure("Scene placement accepts only Prefab or Model main assets");
					}
					if (!seen.insert(identity.assetGuid.ToString()).second)
					{
						continue;
					}
					auto artifact = workspace->Database()->ResolveImportedArtifact(identity.assetGuid);
					if (!artifact || (artifact.value.kind != "Prefab" && artifact.value.kind != "Model"))
					{
						return Result<void>::Failure(
						    artifact ? "Scene placement accepts only Prefab or Model main assets" : artifact.error);
					}
					SceneAssetPlacement placement{identity.assetGuid, artifact.value.kind, nlohmann::json::object()};
					if (placement.kind == "Model")
					{
						auto bytes = workspace->Index().Filesystem().Read(artifact.value.path);
						if (!bytes)
						{
							return Result<void>::Failure(bytes.error);
						}
						try
						{
							placement.modelDocument = nlohmann::json::from_cbor(bytes.value.begin(), bytes.value.end());
						}
						catch (const std::exception& error)
						{
							return Result<void>::Failure(ToUrsaString(error.what()));
						}
					}
					placements.push_back(std::move(placement));
				}
				const ResourceId parentId = hierarchy.DefaultParent(draft->GetScene(), documents->Current()->Id());
				Size insertionIndex = draft->GetScene().RootGameObjects().size();
				Vec3 localPosition = position;
				if (parentId)
				{
					auto parent = draft->GetScene().GameObjectById(parentId);
					if (!parent)
					{
						return Result<void>::Failure(parent.error);
					}
					auto children = draft->GetScene().ChildrenOf(parent.value);
					auto world = draft->GetScene().WorldTransform(parent.value);
					if (!children || !world)
					{
						return Result<void>::Failure(!children ? children.error : world.error);
					}
					insertionIndex = children.value.size();
					try
					{
						const Mat4 inverse = world.value.InverseAffine();
						localPosition = {
						    inverse.At(0, 0) * position.x + inverse.At(0, 1) * position.y +
						        inverse.At(0, 2) * position.z + inverse.At(0, 3),
						    inverse.At(1, 0) * position.x + inverse.At(1, 1) * position.y +
						        inverse.At(1, 2) * position.z + inverse.At(1, 3),
						    inverse.At(2, 0) * position.x + inverse.At(2, 1) * position.y +
						        inverse.At(2, 2) * position.z + inverse.At(2, 3)};
						if (!std::isfinite(localPosition.x) || !std::isfinite(localPosition.y) ||
						    !std::isfinite(localPosition.z))
						{
							return Result<void>::Failure("Default parent has an invalid world transform");
						}
					}
					catch (const std::exception& error)
					{
						return Result<void>::Failure(String("Default parent transform is singular: ") + error.what());
					}
				}
				auto placed = draft->PlaceImportedAssets(placements, parentId, insertionIndex, documents->Current()->Path(),
				                                         documents->Current()->Id());
				if (!placed)
				{
					return Result<void>::Failure(placed.error);
				}
				for (const ResourceId id : placed.value)
				{
					auto object = draft->GetScene().GameObjectById(id);
					if (!object)
					{
						return Result<void>::Failure(object.error);
					}
					auto transform = draft->GetScene().GetLocalTransform(object.value);
					if (!transform)
					{
						return Result<void>::Failure(transform.error);
					}
					transform.value.position = localPosition;
					auto moved = draft->GetScene().SetLocalTransform(object.value, transform.value);
					if (!moved)
					{
						return moved;
					}
				}
				if (!placed.value.empty())
				{
					auto first = draft->GetScene().GameObjectById(placed.value.front());
					if (first)
					{
						auto selected = hierarchy.Select(draft->GetScene(), first.value);
						if (selected)
						{
							selectionContext.SelectGameObject(documents->Current()->Id(), placed.value.front());
						}
					}
				}
				documents->Current()->MarkDirty();
				return {};
			};
			auto drawn = sceneViewPanel->Draw(*draft, *documents->Current(), hierarchy, play, settingsFiles, projectKey,
			                                  showScenePanel, showGamePanel, viewportFocused, inspectorError,
			                                  placeSceneAssets, &focusedGameViewport, &selectionContext);
			if (!drawn)
			{
				inspectorError = drawn.error;
			}
			if (hierarchy.SelectedId() && hierarchy.SelectedId() != selectionBeforeSceneView)
			{
				selectionContext.SelectGameObject(documents->Current()->Id(), hierarchy.SelectedId());
			}
			if (play.Active() && showInspectorPanel && selectionContext.kind != EditorSelectionKind::Asset)
			{
				DrawRuntimeSceneInspector(*play.GetScene(), hierarchy, &showInspectorPanel, inspectorError,
				                          workspace.get(), play.Resources());
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
		auto* selectedDocument = assetInspectorPanel.SelectedDocument(selectionContext);
		assetPreview->SetExtensionResolver([this](ManagedEditorContext& context) { ConfigureAssetPreview(context); });
		projectBrowser.ReconcileSelection(selectionContext);
		auto prepared = assetPreview->Prepare(*workspace, projectBrowser, selectionContext,
		                                      selectedDocument ? selectedDocument
		                                      : documents      ? documents->Current()
		                                                       : nullptr,
		                                      ProjectSettingsKey(workspace->ProjectRoot()));
		if (!prepared)
		{
			return prepared;
		}
		if (startupWindow == "Material Preview")
		{
			showInspectorPanel = true;
			assetPreview->Expand();
			startupWindow = "Inspector";
		}
		assetInspectorPanel.Draw(
		    *workspace, projectBrowser, selectionContext, showInspectorPanel,
		    scriptBuildService ? &scriptBuildService->ScriptTypes() : nullptr,
		    [this](const ProjectBrowserEntry& asset)
		    {
			    auto opened = OpenProjectBrowserAsset(asset);
			    workspaceError = opened ? String{} : opened.error;
			    return opened;
		    },
		    [this](const VirtualPath& path) { RequestAssetImport(path, true); },
		    assetPreview->Supported() ? assetPreview->ReservedHeight() : 0,
		    assetPreview->Supported()
		        ? std::function<void(Float32)>(
		              [this](Float32 maximumHeight)
		              {
			              AssetPreviewTextureInput input;
			              if (textureImportView)
			              {
				              input = {textureImportView->SourcePath(), textureImportView->SourceTexture(),
				                       textureImportView->ImportedTexture()};
			              }
			              assetPreview->Draw(*workspace, project->components, &input, maximumHeight);
		              })
		        : std::function<void(Float32)>{},
		    [this]
		    {
			    const auto* entry = projectBrowser.Find(selectionContext.assetPath);
			    auto* document = documents ? documents->Current() : nullptr;
			    if (entry && !entry->folder && workspace->Database())
			    {
				    const auto descriptions = workspace->ImportRuleDescriptions();
				    const auto descriptor = descriptions->FindImporter(
				        PathToUrsaString(PathFromUrsaString(entry->path.relative).extension()));
				    const bool hasImporter =
				        descriptor && (descriptor.value->scriptType || descriptor.value->identity == "ursa.Texture" ||
				                       descriptor.value->identity == "ursa.Model");
				    if (hasImporter)
				    {
					    if (!assetImporterInspector || assetImporterInspector->SourcePath() != entry->path ||
					        assetImporterInspector->Reference().guid != entry->guid)
					    {
						    if (assetImporterInspector)
						    {
							    const auto retained = assetImporterInspector->SaveRecovery();
							    if (!retained)
							    {
								    inspectorError = retained.error;
								    return;
							    }
						    }
						    const auto opened = AssetImporterInspector::Open(*workspace, entry->path);
						    if (!opened)
						    {
							    inspectorError = opened.error;
							    ImGui::TextWrapped("%s", inspectorError.c_str());
							    return;
						    }
						    assetImporterInspector = opened.value;
					    }
					    const auto request = [this](const VirtualPath& path) { RequestAssetImport(path, true); };
					    const auto custom = [this](ManagedEditorContext& context)
					    { return DrawAssetExtension(context); };
					    const bool canApply = !play.Active() && !AssetWorkPending() && !ScriptWorkPending();
					    Result<void> drawn;
					    if (descriptor.value->identity == "ursa.Texture")
					    {
						    if (!textureImportView || textureImportView->SourcePath() != entry->path)
						    {
							    const auto settings = TextureImporter::Settings(*workspace->Database(), entry->path);
							    auto view = settings
							                    ? TextureImportView::Open(entry->path, settings.value)
							                    : Result<std::unique_ptr<TextureImportView>>::Failure(settings.error);
							    if (!view)
							    {
								    inspectorError = view.error;
								    return;
							    }
							    if (textureImportView)
							    {
								    textureImportView->Close(*gui);
							    }
							    textureImportView = std::move(view.value);
							    textureImportView->Reimport(*workspace->Database(), workspace->Importer(), device, *gui,
							                                true);
						    }
						    textureImportView->SetSettingsInspector(assetImporterInspector);
						    drawn = textureImportView->Draw(*workspace->Database(), workspace->Importer(), device, *gui,
						                                    canApply, request, workspace.get(), custom, true);
					    }
					    else if (descriptor.value->identity == "ursa.Model")
					    {
						    if (!modelImportView || modelImportView->SourcePath() != entry->path)
						    {
							    const auto settings = ModelImporter::Settings(*workspace->Database(), entry->path);
							    auto view = settings
							                    ? ModelImportView::Open(entry->path, settings.value)
							                    : Result<std::unique_ptr<ModelImportView>>::Failure(settings.error);
							    if (!view)
							    {
								    inspectorError = view.error;
								    return;
							    }
							    modelImportView = std::move(view.value);
							    modelImportView->Reimport(*workspace->Database(), workspace->Importer(), true);
						    }
						    modelImportView->SetSettingsInspector(assetImporterInspector);
						    std::function<Result<void>()> instantiate;
						    if (!play.Active() && document &&
						        (document->Kind() == ResourceKind::Scene || document->Kind() == ResourceKind::Prefab))
						    {
							    const auto draft = document->Payload<SceneDraft>();
							    if (draft)
							    {
								    instantiate = [this, draft, document]
								    {
									    auto added = modelImportView->Instantiate(*draft);
									    if (added)
									    {
										    document->MarkDirty();
									    }
									    return added;
								    };
							    }
						    }
						    modelImportView->SetObjectSelection(
						        [this](AssetReference reference)
						        {
							        const auto object =
							            workspace->Database()->ImportedObject(reference.guid, reference.localFileId);
							        if (object &&
							            selectionContext.SelectAsset(object.value.sourcePath, object.value.localFileId,
							                                         object.value.kind, reference.guid))
							        {
								        projectBrowser.ReconcileSelection(selectionContext);
							        }
						        });
						    drawn = modelImportView->Draw(*workspace->Database(), workspace->Importer(), canApply,
						                                  instantiate, request, workspace.get(), custom, true);
					    }
					    else
					    {
						    const auto rendered =
						        assetImporterInspector->DrawDefault(request, canApply, workspace.get(), custom);
						    if (!rendered)
						    {
							    drawn = Result<void>::Failure(rendered.error);
						    }
					    }
					    if (!drawn)
					    {
						    inspectorError = drawn.error;
					    }
					    return;
				    }
			    }
			    if (!entry || !document || entry->guid != document->Id() || entry->path != document->Path())
			    {
				    return;
			    }
			    if (document->Kind() == ResourceKind::Material)
			    {
				    ImGuiInspectorUI ui;
				    auto result = DrawMaterialInspector(*document, *documents, *workspace, inspectorError,
				                                        &project->materialInspectors, &ui, true);
				    if (!result)
				    {
					    inspectorError = result.error;
				    }
			    }
			    else if (document->Kind() == ResourceKind::Texture)
			    {
				    auto result = DrawTextureInspector(*document, inspectorError, true);
				    if (!result)
				    {
					    inspectorError = result.error;
				    }
			    }
		    },
		    [this](ManagedEditorContext& context)
		    {
			    context.canEditTarget = context.canEditTarget && !play.Active();
			    return DrawAssetExtension(context);
		    },
		    &project->components,
		    [this](EditorDocument& document, EditorDocuments& owner) -> std::optional<Result<bool>>
		    {
			    const auto draft = document.Payload<MaterialDraft>();
			    const auto shader = draft ? draft->Material().Shader() : nullptr;
			    if (!shader || shader->customEditor.empty() || !project->materialInspectors.Find(shader->customEditor))
			    {
				    return std::nullopt;
			    }
			    ImGuiInspectorUI ui;
			    MaterialInspectorContext context{document, *draft, owner.Session()};
			    return project->materialInspectors.Draw(shader->customEditor, context, ui);
		    },
		    documents.get());
	}
	consolePanel.Draw(
	    showConsolePanel, inspectorError,
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
					    const Size separator = asset->folder ? String::npos : asset->path.relative.rfind('/');
					    const String parent = asset->folder               ? asset->path.relative
					                          : separator == String::npos ? String{}
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
	    },
	    nullptr,
	    [this]
	    {
		    if (play.Active() && !play.Paused())
		    {
			    play.Pause();
		    }
	    },
	    play.Active(), workspace ? &workspace->Index().Filesystem() : nullptr);
	DrawImporterSelectionPrompt();
	if (documents)
	{
		documents->DrawUnsavedPrompt();
	}
	if (!startupWindow.empty() && ImGui::FindWindowByName(startupWindow.c_str()))
	{
		ImGui::SetWindowFocus(startupWindow.c_str());
		startupWindow.clear();
	}
	auto panelStateSaved = SavePanelVisibility();
	if (!panelStateSaved)
	{
		workspaceError = panelStateSaved.error;
	}
	ProcessCompletedReferencePickerCancellations(gui ? gui->Context() : nullptr);
	if (pendingPickerSelection && !HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
	{
		auto selection = std::move(*pendingPickerSelection);
		pendingPickerSelection.reset();
		selectionContext.SelectEntries(std::move(selection.entries), selection.primaryIndex, selection.rangeAnchor);
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
			if (type.group != ScriptAssemblyGroup::Game || type.kind != ScriptTypeKind::Behaviour)
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
		if (scriptType.group != ScriptAssemblyGroup::Game || scriptType.kind != ScriptTypeKind::Behaviour)
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
			if (extension.kind != ManagedExtensionKind::Inspector)
			{
				continue;
			}
			String target = extension.target;
			if (target.find("asset:") == 0 || target.find("importer:") == 0)
			{
				bool exposed = target == "asset:ursa.Prefab" || target == "asset:ursa.Material" ||
				               target == "asset:ursa.Mesh" || target == "asset:ursa.Texture" ||
				               target == "importer:ursa.Texture" || target == "importer:ursa.Model";
				for (const auto& scriptType : scriptBuildService->ScriptTypes())
				{
					if (scriptType.kind == ScriptTypeKind::ScriptableObject &&
					    scriptType.group == ScriptAssemblyGroup::Game)
					{
						exposed |= target == "asset:" + scriptType.scriptType;
					}
					if (scriptType.kind == ScriptTypeKind::ScriptedImporter)
					{
						exposed |= target == "importer:script:" + scriptType.sourceGuid.ToString() + ":" +
						                         ToUrsaString(std::to_string(scriptType.localFileId));
					}
				}
				if (!exposed)
				{
					return Result<void>::Failure("Custom Inspector target is not an exposed asset or importer: " +
					                             target);
				}
				continue;
			}
			bool matchedScript = false;
			for (const auto& scriptType : scriptBuildService->ScriptTypes())
			{
				if (scriptType.scriptType == target && scriptType.group == ScriptAssemblyGroup::Game &&
				    scriptType.kind == ScriptTypeKind::Behaviour)
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
			const bool supportsMultipleObjects = extension.canEditMultipleObjects;
			candidateEditors.ReplaceEditor(
			    target,
			    [this, extensionId, sessionId, supportsMultipleObjects](
			        Component& component, ComponentInspectorContext& selection, InspectorUI&) -> Result<bool>
			    {
				    if (selection.targets.size() > 1 && !supportsMultipleObjects)
				    {
					    return Result<bool>::Failure("This Inspector has not declared multi-object editing support");
				    }
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
				    context.componentTargets = selection.targets;
				    context.componentPrimaryIndex = selection.primaryIndex;
				    context.supportsMultipleObjects = supportsMultipleObjects;
				    context.selection = selection;
				    context.saveContext.scene = selection.scene;
				    context.saveContext.materialId = [this](const std::shared_ptr<MaterialInstance>& material)
				    { return documents->Session().IdentifyMaterial(material); };
				    ConfigureReferencePreviewFactory(context);
				    context.openAsset = [this](const VirtualPath& path)
				    {
					    pendingManagedAsset = path;
					    return Result<void>{};
				    };
				    return InvokeManagedEditorExtension(*managedHost, sessionId, extensionId, context);
			    },
			    supportsMultipleObjects);
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
	editorProductName = title;
	if (windowIconPixels.empty())
	{
		auto decoded = DecodeWindowIcon(editorIconPng, sizeof(editorIconPng));
		if (!decoded)
		{
			return decoded;
		}
	}
	auto windowResult = Window::Create(width, height, MainWindowTitle(), false);
	if (!windowResult)
	{
		return Result<void>::Failure(windowResult.error);
	}
	window = std::move(windowResult.value);
	auto applied = window->SetIcon(windowIconWidth, windowIconHeight, windowIconPixels);
	if (!applied)
	{
		window.reset();
		return applied;
	}
	if (visible)
	{
		window->Show();
	}
	Rendering::WindowCaptureScope captureScope(frameCapture, "Editor");

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
	assetPreview = std::make_unique<AssetPreviewPanel>(device, *gui);
	auto scenePanelResult = SceneViewPanel::Create(device, *gui);
	if (!scenePanelResult)
	{
		assetPreview.reset();
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
				drawn = PresentGuiFrame(false);
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
			workspace->SetImportRuleDescriptions(prepared.value.rules);
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
	}
	auto focused = window->Subscribe(
	    [this](const WindowEvent& event)
	    {
		    if (autoRefreshAssets && event.type == WindowEventType::Focused && event.focused)
		    {
			    nextAssetScan = {};
		    }
		    if (event.type == WindowEventType::FilesDropped && !event.paths.empty())
		    {
			    pendingProjectBrowserFileDrops.push_back({event.x, event.y, event.paths});
		    }
	    });
	if (focused)
	{
		assetFocusSubscription = focused.value;
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
	if (result)
	{
		frameCapture.ReadyWindowCapture();
	}
	const auto startupPlayDeadline = previous + std::chrono::seconds(60);
	Ursa::UInt32 frames = 0;
	while (result)
	{
		window->PollEvents();
		frameCapture.PollWindowCapture();
		if (window->ShouldClose() && HasActiveReferencePickerSession(gui ? gui->Context() : nullptr))
		{
			RequestCancelReferencePickerSessions(gui ? gui->Context() : nullptr, [this] { window->RequestClose(); });
			window->CancelClose();
		}
		if (window->ShouldClose() && assetInspectorPanel.HasModifiedSource())
		{
			exitAfterInspectorPrompt = true;
			EditorSelectionContext next;
			BeforeImporterSelection(next);
			window->CancelClose();
		}
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
		// Keep building the shared ImGui frame while the host window is minimized. The main
		// swapchain may have no acquired image, but detached platform windows still need input,
		// UI updates, and rendering. PresentGuiFrame() skips the unavailable host target and
		// continues through ImGuiBridge::RenderPlatformWindows().
		Memory::TemporaryScope frameMemory;
		URSA_PROFILE_SCOPE("Editor.Frame");
		FinishAssetJobs();

		const auto now = std::chrono::steady_clock::now();
		const Ursa::Float32 deltaSeconds = std::chrono::duration<Ursa::Float32>(now - previous).count();
		previous = now;
		{
			URSA_PROFILE_SCOPE("Editor.BeginFrame");
			const Ursa::UInt32 logicalWidth = window->Width() ? window->Width() : width;
			const Ursa::UInt32 logicalHeight = window->Height() ? window->Height() : height;
			result = gui->BeginFrame(deltaSeconds, logicalWidth, logicalHeight, window->FramebufferWidth(),
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
		if (startupPlay && play.Active())
		{
			startupPlay = false;
		}
		if (startupPlay && std::chrono::steady_clock::now() >= startupPlayDeadline)
		{
			result = Result<void>::Failure("Startup Play did not become active: " + scriptBuildStatus + inspectorError);
		}
		if (result && play.Active())
		{
			URSA_PROFILE_SCOPE("Editor.PlayModeUpdate");
			auto inputWindow = gui->InputWindowForViewport(focusedGameViewport);
			Window* playWindow = inputWindow ? inputWindow.get() : window.get();
			const ImGuiIO& editorIo = ImGui::GetIO();
			const bool editorInputCaptured = EditorInputBlocksPlay(
			    editorIo.WantCaptureKeyboard, editorIo.WantTextInput, ImGui::GetDragDropPayload() != nullptr,
			    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
			const FrameInput input =
			    SelectPlayInput(FrameInput::Capture(*playWindow), viewportFocused, editorInputCaptured);
			auto ticked = play.Tick(deltaSeconds, playWindow, input);
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
				EditorRenderViewport renderViewport =
				    [this](Scene& renderScene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
				           std::optional<Rendering::CameraType> cameraFilter,
				           std::optional<Rendering::CameraData> cameraOverride)
				{
					Vector<ResourceId> excludedObjects;
					if (cameraFilter == Rendering::CameraType::SceneView)
					{
						excludedObjects = hierarchy.SceneVisibilityExclusions(renderScene);
					}
					return RenderManagedViewport(renderScene, target, cameraFilter, std::move(cameraOverride),
					                             std::move(excludedObjects));
				};
				auto rendered = sceneViewPanel->RenderPending(*draft, play, renderViewport);
				if (!rendered)
				{
					inspectorError = rendered.error;
				}
				else if (inspectorError.rfind("Scene viewport render failed:", 0) == 0 ||
				         inspectorError.rfind("Game viewport render failed:", 0) == 0)
				{
					inspectorError.clear();
				}
			}
		}
		if (result)
		{
			if (assetPreview)
			{
				// A GUI callback may minimize the window after recording this frame's Preview commands.
				if (window->Minimized() || window->FramebufferWidth() == 0 || window->FramebufferHeight() == 0)
				{
					result = assetPreview->BeginFrame();
				}
				if (result)
				{
					result = assetPreview->RenderPending(
					    deltaSeconds,
					    [this](Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
					           std::optional<Rendering::CameraType> filter, std::optional<Rendering::CameraData> camera)
					    { return RenderManagedViewport(scene, target, filter, std::move(camera)); });
				}
				if (result)
				{
					result = RenderReferencePickerPreviews(
					    deltaSeconds,
					    [this](Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
					           std::optional<Rendering::CameraType> filter, std::optional<Rendering::CameraData> camera)
					    { return RenderManagedViewport(scene, target, filter, std::move(camera)); });
				}
			}
			URSA_PROFILE_SCOPE("Editor.EndFrame");
			if (result)
			{
				result = gui->EndFrame();
			}
		}
		if (!result)
		{
			break;
		}

		{
			URSA_PROFILE_SCOPE("Editor.RenderPreparation");
			result = PresentGuiFrame(!startupPlay);
			if (result)
			{
				URSA_PROFILE_FRAME("Editor");
			}
			if (!startupPlay && frameLimit && ++frames >= frameLimit)
			{
				break;
			}
			if (window->Minimized() || window->FramebufferWidth() == 0 || window->FramebufferHeight() == 0)
			{
				window->WaitEventsTimeout(1.0 / 120.0);
			}
		}
	}

	frameCapture.StopWindowCapture();
	const auto idle = device->WaitIdle();
	FinishScriptJobs();
	play.Stop();
	if (documents)
	{
		documents->SetSaveBlocked(false);
	}
	OnStop();
	assetPreview.reset();
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
	editingScriptServices.reset();
#endif
	scriptBuildService.reset();
	scriptAssetDatabase.reset();
	if (assetImporterInspector)
	{
		const auto retained = assetImporterInspector->SaveRecovery();
		if (!retained)
		{
			URSA_LOG_ERROR("Importer", retained.error);
		}
	}
	assetImporterInspector.reset();
	pendingImporterSelection.reset();
	assetInspectorPanel.Reset();
	selectionContext.beforeChange = {};
	documents.reset();
	workspace.reset();
	projectBrowser = ProjectBrowserModel{};
	projectBrowserPanel.Reset();
	selectionContext.Clear();
	hierarchy.ClearSelection();
	sceneViewPanel.reset();
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
	return result ? (idle ? frameCapture.Finish() : idle) : result;
}

Result<void> EditorApplication::PresentGuiFrame(bool capture)
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
		return gui->RenderPlatformWindows();
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
		auto editorRegion = Rhi::DebugRegionScope::Begin(*command.value, "Editor UI");
		if (!editorRegion)
		{
			result = Result<void>::Failure(editorRegion.error);
		}
		else
		{
			result = gui->Record(command.value, swapchain->Format(), acquired.value.texture->desc.width,
			                     acquired.value.texture->desc.height);
			auto ended = editorRegion.value.Close();
			if (result && !ended)
			{
				result = std::move(ended);
			}
		}
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
		result = frameCapture.Capture(device, acquired.value.texture, Rhi::ResourceState::Present, capture);
	}
	if (result)
	{
		result = swapchain->Present();
	}
	if (result)
	{
		result = submittedFence->Wait();
	}
	if (result)
	{
		result = gui->RenderPlatformWindows();
	}
	return result;
}

void EditorApplication::RequestAssetImport(const VirtualPath& scope, bool force)
{
	requestedImportScope = assetImportRequested && requestedImportScope != scope ? VirtualPath{} : scope;
	requestedImportSources.clear();
	forceAssetImport = forceAssetImport || force;
	assetImportRequested = true;
	assetRequestDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

void EditorApplication::RequestAssetReimport(const Vector<VirtualPath>& sources)
{
	if (assetImportRequested || assetImportJob.valid())
	{
		requestedImportSources.clear();
		requestedImportScope = {};
		forceAssetImport = true;
	}
	else
	{
		requestedImportSources = sources;
		requestedImportScope = {};
		forceAssetImport = true;
	}
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
				if (assetImporterInspector)
				{
					const auto retained = assetImporterInspector->SaveRecovery();
					if (!retained)
					{
						inspectorError = retained.error;
					}
					assetImporterInspector.reset();
				}
				workspace->SetImportRuleDescriptions(result.value.rules);
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
#if defined(URSA_ENABLE_DOTNET)
				if (ready && editingScriptServices)
				{
					auto services = std::static_pointer_cast<ManagedEditingSessionState>(editingScriptServices);
					ready = services->resources->ReloadImportedResources(workspace->ContentFiles());
				}
#endif
				if (ready)
				{
					ready = gui->ReloadShaders();
				}
			}
			if (ready && sceneViewPanel)
			{
				ready = sceneViewPanel->ReloadResources();
			}
			if (ready && assetPreview)
			{
				assetPreview->Reload();
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
				textureImportView->Reimport(*workspace->Database(), workspace->Importer(), device, *gui, true);
			}
			if (modelImportView)
			{
				modelImportView->Reimport(*workspace->Database(), workspace->Importer(), true);
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
		options.sources = std::move(requestedImportSources);
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
