#include <Core/Types.hpp>
#include <Application/EditorApplication.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <Assets/TextureDraft.hpp>
#include <Assets/MaterialDraft.hpp>
#include <Assets/SceneDraft.hpp>
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
Result<VirtualPath> ProjectAssetPath(const char* input)
{
	const String name = ToUrsaString(std::string(input));
	if (name.empty())
	{
		return Result<VirtualPath>::Failure("Enter an asset path such as Snake/Level.ursascene");
	}
	return VirtualPath::Parse(String(projectResourceScheme) + ":" + name);
}
} // namespace

Result<void> EditorApplication::OpenProject(const std::filesystem::path& root, const std::filesystem::path& package)
{
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
	if (package.empty())
	{
		auto roots = ResolveEngineAssetRoots();
		if (!roots)
		{
			return Result<void>::Failure(roots.error);
		}
		profile.engineDirectories = std::move(roots.value);
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
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(files.value));
	documents = std::move(documentsResult.value);
	workspace = std::move(openedWorkspace);
	workspaceError.clear();
	URSA_LOG_INFO("Editor", "Opened project " + PathToUrsaString(workspace->ProjectRoot()));
	return {};
}

Result<void> EditorApplication::OnGui()
{
	if (ImGui::BeginMainMenuBar())
	{
		ImGui::TextUnformatted("Ursa Editor");
		ImGui::Separator();
		const auto* current = documents ? documents->Current() : nullptr;
		const bool canRun = current && current->Kind() == ResourceKind::Scene && !documents->PromptPending();
		ImGui::BeginDisabled(!play.Active() && !canRun);
		if (ImGui::Button(play.Active() ? "Stop" : "Run"))
		{
			if (play.Active())
			{
				play.Stop();
				documents->SetSaveBlocked(false);
				viewportFocused = false;
			}
			else
			{
				auto draft = current->Payload<SceneDraft>();
				if (draft)
				{
					auto started = play.Start(*draft, current->Id(), project->components, documents->Session(),
					                          project->playContextSetup);
					inspectorError = started ? String{} : started.error;
					if (started)
					{
						documents->SetSaveBlocked(true);
					}
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::Separator();
		if (workspace)
		{
			const auto name = PathToUrsaString(workspace->ProjectRoot().filename());
			ImGui::Text("Project: %s", name.c_str());
		}
		ImGui::EndMainMenuBar();
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
			const auto name = entry.location.path.ToString();
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
	}
	ImGui::EndDisabled();
	ImGui::End();
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
		auto inspected = DrawMaterialInspector(*documents->Current(), *documents, *workspace, inspectorError);
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
				auto hierarchyResult = hierarchy.Draw(*play.GetScene(), project->components);
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
				                                    project->components, project->componentEditors, inspectorError);
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
	                                 documents->Current()->Kind() == ResourceKind::Texture);
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
	if (result && projectSetup)
	{
		result = projectSetup(*project);
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
	play.Stop();
	if (documents)
	{
		documents->SetSaveBlocked(false);
	}
	OnStop();
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
