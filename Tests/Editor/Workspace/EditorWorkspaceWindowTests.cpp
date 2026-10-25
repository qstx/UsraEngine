#include <Application/EditorApplication.hpp>
#include <Core/Memory/Memory.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Core/StringInterop.hpp>
#include <Ui/EditorIcons.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Assets/SceneDraft.hpp>
#include <Workspace/EditorDragSession.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
class WorkspaceEditor final : public Ursa::Editor::EditorApplication
{
public:
	explicit WorkspaceEditor(bool restore = false, bool testCloseCancel = false, bool testDetachedClose = false,
	                         bool testCrossViewportDrag = false)
	    : restoreLayout(restore), testCloseCancel(testCloseCancel), testDetachedClose(testDetachedClose),
	      testCrossViewportDrag(testCrossViewportDrag)
	{
	}

protected:
	Ursa::Result<void> OnStart() override
	{
		auto started = EditorApplication::OnStart();
		if (!started || !testCrossViewportDrag)
		{
			return started;
		}
		workflowScenePath = Ursa::VirtualPath::Parse("prj:CrossViewportWorkflow.ursascene");
		if (!workflowScenePath)
		{
			return Ursa::Result<void>::Failure(workflowScenePath.error);
		}
		auto scene =
		    Ursa::Editor::NewSceneDocument(workflowScenePath.value, Ursa::ResourceKind::Scene, Project().components,
		                                   Documents()->Session(), Project().editContextSetup);
		if (!scene)
		{
			return Ursa::Result<void>::Failure(scene.error);
		}
		auto opened = Documents()->RequestOpen(std::move(scene.value));
		if (!opened)
		{
			return opened;
		}
		auto saved = Documents()->SaveCurrent();
		if (!saved)
		{
			return saved;
		}
		workflowModelSourcePath = Ursa::VirtualPath::Parse("prj:CrossViewportWorkflow.obj");
		if (!workflowModelSourcePath)
		{
			return Ursa::Result<void>::Failure(workflowModelSourcePath.error);
		}
		const Ursa::String modelSource = "o CrossViewportProjectModel\nv -1 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
		auto written = Workspace()->ContentFiles().WriteAtomic(
		    workflowModelSourcePath.value, Ursa::Vector<Ursa::UInt8>(modelSource.begin(), modelSource.end()));
		if (!written)
		{
			return written;
		}
		auto refreshed = Workspace()->Refresh();
		if (!refreshed)
		{
			return refreshed;
		}
		RefreshAssets(workflowModelSourcePath.value, true);
		return {};
	}

	Ursa::Result<void> OnFrame(Ursa::Float32) override
	{
		const auto tag = Ursa::Memory::RegisterTag("EditorFrameScratchTest");
		if (Ursa::Memory::Snapshot().categories[static_cast<Ursa::Size>(tag)].liveAllocations != 0)
		{
			return Ursa::Result<void>::Failure("Previous Editor frame retained scratch allocation");
		}
		Ursa::Memory::ThreadTemporaryAllocator().Allocate(128, 64, tag);
		if (testCrossViewportDrag && !workflowModelGuid)
		{
			const auto assets = Workspace()->Database()->Assets();
			const auto source = std::find_if(assets.begin(), assets.end(), [&](const auto& asset)
			                                 { return asset.path == workflowModelSourcePath.value; });
			if (source == assets.end())
			{
				return Ursa::Result<void>::Failure("The Project index did not publish the workflow Model source");
			}
			workflowModelGuid = source->metadata.guid;
		}
		if (testCrossViewportDrag && RunningScene())
		{
			workflowPlayObserved = true;
		}
		if (testCrossViewportDrag && workflowModelGuid && !workflowPlayObserved && !workflowPlayStopRequested &&
		    frames >= workflowNextPlayAttemptFrame)
		{
			FinishScriptJobs();
			RequestPlayToggle();
			++workflowPlayStartAttempts;
			workflowNextPlayAttemptFrame = frames + 12;
		}
		if (testCrossViewportDrag && !workflowPlayObserved && frames >= 500)
		{
			return Ursa::Result<void>::Failure(
			    "The Editor did not start Play before the cross-viewport asset workflow (attempts=" +
			    Ursa::ToUrsaString(std::to_string(workflowPlayStartAttempts)) + ")");
		}
		if (testCloseCancel && !closeRequested && frames >= 5 && ImGui::GetPlatformIO().Viewports.Size > 1)
		{
			auto path = Ursa::VirtualPath::Parse("prj:close-cancel.ursamat");
			auto document = path ? Ursa::Editor::EditorDocument::Create(
			                           Ursa::ResourceKind::Material, Ursa::ResourceId::Generate(), path.value,
			                           [](Ursa::VirtualFileSystem&, const Ursa::VirtualPath&, Ursa::ResourceId)
			                           { return Ursa::Result<void>{}; })
			                     : Ursa::Result<std::unique_ptr<Ursa::Editor::EditorDocument>>::Failure(path.error);
			if (!document)
			{
				return Ursa::Result<void>::Failure(document.error);
			}
			auto opened = Documents()->RequestOpen(std::move(document.value));
			if (!opened)
			{
				return opened;
			}
			Documents()->Current()->MarkDirty();
			window->RequestClose();
			closeRequested = true;
		}
		if (testDetachedClose && !detachedCloseRequested && frames >= 2 && ImGui::GetPlatformIO().Viewports.Size > 1)
		{
			auto path = Ursa::VirtualPath::Parse("prj:detached-close.ursamat");
			auto document = path ? Ursa::Editor::EditorDocument::Create(
			                           Ursa::ResourceKind::Material, Ursa::ResourceId::Generate(), path.value,
			                           [](Ursa::VirtualFileSystem&, const Ursa::VirtualPath&, Ursa::ResourceId)
			                           { return Ursa::Result<void>{}; })
			                     : Ursa::Result<std::unique_ptr<Ursa::Editor::EditorDocument>>::Failure(path.error);
			if (!document)
			{
				return Ursa::Result<void>::Failure(document.error);
			}
			auto opened = Documents()->RequestOpen(std::move(document.value));
			if (!opened)
			{
				return opened;
			}
			Documents()->Current()->MarkDirty();
			auto* viewport = ImGui::GetPlatformIO().Viewports[1];
			auto detachedWindow = Gui().InputWindowForViewport(viewport);
			if (!detachedWindow)
			{
				return Ursa::Result<void>::Failure("The floating Editor viewport has no native window");
			}
			detachedWindow->RequestClose();
			detachedCloseRequested = true;
		}
		if (testDetachedClose && detachedCloseRequested && !detachedCloseChecked &&
		    ImGui::GetPlatformIO().Viewports.Size < 2)
		{
			if (!Documents()->Current() || !Documents()->Current()->Dirty() || window->ShouldClose())
			{
				return Ursa::Result<void>::Failure(
				    "Closing the floating Editor viewport did not preserve the dirty document and host window");
			}
			detachedCloseChecked = true;
			checked = true;
		}
		if (testDetachedClose && frames >= 14 && !detachedCloseChecked)
		{
			return Ursa::Result<void>::Failure("The floating Editor viewport did not close independently");
		}
		if (testCloseCancel && closeRequested && !closeCancelChecked && Documents()->PromptPending())
		{
			auto canceled = Documents()->ResolvePrompt(Ursa::Editor::UnsavedChoice::Cancel);
			if (!canceled)
			{
				return canceled;
			}
			if (!Documents()->Current() || !Documents()->Current()->Dirty() || window->ShouldClose() ||
			    ImGui::GetPlatformIO().Viewports.Size < 2)
			{
				return Ursa::Result<void>::Failure(
				    "Cancel did not preserve the dirty document and its floating Editor viewport");
			}
			closeCancelChecked = true;
		}
		if (testCloseCancel && frames >= 14 && !closeCancelChecked)
		{
			return Ursa::Result<void>::Failure(
			    "The Editor did not present the dirty-document close prompt (requested=" +
			    Ursa::ToUrsaString(std::to_string(closeRequested)) +
			    ", pending=" + Ursa::ToUrsaString(std::to_string(Documents()->PromptPending())) + ", dirty=" +
			    Ursa::ToUrsaString(std::to_string(Documents()->Current() && Documents()->Current()->Dirty())) +
			    ", shouldClose=" + Ursa::ToUrsaString(std::to_string(window->ShouldClose())) + ")");
		}
		lastViewportCount = ImGui::GetPlatformIO().Viewports.Size;
		lastHostMinimized = window->Minimized();
		lastFloatingPlatformHandle = false;
		lastFloatingRenderer = false;
		for (int index = 1; index < ImGui::GetPlatformIO().Viewports.Size; ++index)
		{
			const auto* viewport = ImGui::GetPlatformIO().Viewports[index];
			lastFloatingPlatformHandle |= viewport->PlatformHandle != nullptr;
			lastFloatingRenderer |= viewport->RendererUserData != nullptr;
		}
		if (!testDetachedClose && !testCrossViewportDrag && !restoreLayout && floatingRequested && !minimizeStarted &&
		    ImGui::GetPlatformIO().Viewports.Size > 1)
		{
			window->Minimize();
			minimizeStarted = true;
		}
		if (!testDetachedClose && !testCrossViewportDrag && !restoreLayout && minimizeStarted && window->Minimized())
		{
			++minimizedFrames;
			if (minimizedFrames >= 2 && lastFloatingPlatformHandle && lastFloatingRenderer &&
			    (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
			{
				minimizedViewportRendered = true;
			}
			if (minimizedViewportRendered)
			{
				window->Restore();
			}
		}
		if (!testDetachedClose && !testCrossViewportDrag && !restoreLayout && floatingRequested && frames >= 14 &&
		    !minimizedViewportRendered)
		{
			return Ursa::Result<void>::Failure(
			    "The editor frame loop did not keep processing a floating viewport while the host was minimized "
			    "(viewports=" +
			    Ursa::ToUrsaString(std::to_string(lastViewportCount)) +
			    ", minimized=" + Ursa::ToUrsaString(std::to_string(lastHostMinimized)) +
			    ", platform=" + Ursa::ToUrsaString(std::to_string(lastFloatingPlatformHandle)) +
			    ", renderer=" + Ursa::ToUrsaString(std::to_string(lastFloatingRenderer)) +
			    ", minimizedFrames=" + Ursa::ToUrsaString(std::to_string(minimizedFrames)) + ")");
		}
		return {};
	}

	Ursa::Result<void> OnGui() override
	{
		auto drawn = EditorApplication::OnGui();
		if (!drawn)
		{
			return drawn;
		}
		Ursa::String documentTitle =
		    Documents()->Current() ? Documents()->Current()->Path().FileName() : Ursa::String("Untitled");
		if (const auto extension = documentTitle.find_last_of('.'); extension != Ursa::String::npos)
		{
			documentTitle.resize(extension);
		}
		if (documentTitle.empty())
		{
			documentTitle = "Untitled";
		}
		if (Documents()->Current() && Documents()->Current()->Dirty())
		{
			documentTitle += "*";
		}
		const auto expectedPrefix = Ursa::PathToUrsaString(Workspace()->ProjectRoot().filename()) + " - " +
		                            documentTitle + " - Editor Workspace Test (";
		if (window->Title().find(expectedPrefix) != 0 || window->Title().back() != ')')
		{
			return Ursa::Result<void>::Failure("Editor window title is missing project, document or version");
		}
		if (++frames < (restoreLayout ? 3u : 2u))
		{
			return {};
		}
		auto* project = ImGui::FindWindowByName("Project");
		auto* hierarchy = ImGui::FindWindowByName("Hierarchy");
		auto* scene = ImGui::FindWindowByName("Scene");
		auto* game = ImGui::FindWindowByName("Game");
		auto* console = ImGui::FindWindowByName("Console");
		auto* inspector = ImGui::FindWindowByName("Inspector");
		if (!project || !hierarchy || !scene || !game || !console || !inspector)
		{
			return Ursa::Result<void>::Failure("Editor workspace is missing a panel");
		}
		if (!Ursa::Editor::Icons::Available())
		{
			return Ursa::Result<void>::Failure("Workspace icon atlas is unavailable");
		}
		if (restoreLayout)
		{
			if (restoreDragPhase == RestoreDragPhase::None &&
			    (inspector->DockNode != project->DockNode || !game->Viewport ||
			     game->Viewport->ID == ImGui::GetMainViewport()->ID))
			{
				return Ursa::Result<void>::Failure(
				    "The saved layout did not restore the Inspector dock and floating Game viewport (inspectorDockId=" +
				    Ursa::ToUrsaString(std::to_string(inspector->DockId)) + ", inspectorNode=" +
				    Ursa::ToUrsaString(std::to_string(inspector->DockNode ? inspector->DockNode->ID : 0)) +
				    ", projectNode=" +
				    Ursa::ToUrsaString(std::to_string(project->DockNode ? project->DockNode->ID : 0)) + ")");
			}
			if (restoreDragPhase == RestoreDragPhase::None)
			{
				if (!project->DockNode)
				{
					return Ursa::Result<void>::Failure("The restored Project panel is missing its dock node");
				}
				gameTabPosition = game->TitleBarRect().GetCenter();
				if (game->DockNode && game->DockNode->TabBar)
				{
					const auto gameTab =
					    std::find_if(game->DockNode->TabBar->Tabs.begin(), game->DockNode->TabBar->Tabs.end(),
					                 [game](const ImGuiTabItem& item) { return item.Window == game; });
					if (gameTab != game->DockNode->TabBar->Tabs.end())
					{
						gameTabPosition =
						    ImVec2(game->DockNode->TabBar->BarRect.Min.x + gameTab->Offset + gameTab->Width * 0.5f,
						           game->DockNode->TabBar->BarRect.GetCenter().y);
					}
				}
				projectTabPosition = project->TitleBarRect().GetCenter();
				if (project->DockNode->TabBar)
				{
					const auto projectTab =
					    std::find_if(project->DockNode->TabBar->Tabs.begin(), project->DockNode->TabBar->Tabs.end(),
					                 [project](const ImGuiTabItem& item) { return item.Window == project; });
					if (projectTab != project->DockNode->TabBar->Tabs.end())
					{
						projectTabPosition = ImVec2(project->DockNode->TabBar->BarRect.Min.x + projectTab->Offset +
						                                projectTab->Width * 0.5f,
						                            project->DockNode->TabBar->BarRect.GetCenter().y);
					}
				}
				MoveMainCursor(gameTabPosition);
				restoreDragPhase = RestoreDragPhase::Hover;
				return {};
			}
			if (restoreDragPhase == RestoreDragPhase::Hover)
			{
				DispatchMouse(Ursa::WindowEventType::MousePressed);
				restoreDragPhase = RestoreDragPhase::Pressed;
			}
			else if (restoreDragPhase == RestoreDragPhase::Pressed)
			{
				MoveMainCursor(projectTabPosition);
				restoreDragPhase = RestoreDragPhase::Dragging;
			}
			else if (restoreDragPhase == RestoreDragPhase::Dragging)
			{
				restoreDragPhase = RestoreDragPhase::DockPending;
			}
			else if (restoreDragPhase == RestoreDragPhase::DockPending)
			{
				DispatchMouse(Ursa::WindowEventType::MouseReleased);
				restoreDragPhase = RestoreDragPhase::ReleasePending;
			}
			else if (restoreDragPhase == RestoreDragPhase::ReleasePending)
			{
				if (game->DockNode != project->DockNode || game->Viewport->ID != ImGui::GetMainViewport()->ID)
				{
					return frames >= 14 ? Ursa::Result<void>::Failure(
					                          "Dragging the floating Game tab onto Project did not re-dock it")
					                    : Ursa::Result<void>{};
				}
				restoreDragPhase = RestoreDragPhase::Complete;
			}
			else if (restoreDragPhase == RestoreDragPhase::Complete)
			{
				checked = true;
			}
			ImGui::GetIO().WantSaveIniSettings = true;
			return {};
		}
		if (frames == 2)
		{
			if (!project->DockNode || !hierarchy->DockNode || !scene->DockNode || !game->DockNode ||
			    !console->DockNode || !inspector->DockNode ||
			    !(hierarchy->Pos.x < scene->Pos.x && scene->Pos.x < inspector->Pos.x && project->Pos.y > scene->Pos.y &&
			      console->DockNode == project->DockNode && game->DockNode == scene->DockNode))
			{
				return Ursa::Result<void>::Failure("Editor workspace panel arrangement is incorrect");
			}
			ImGui::DockBuilderDockWindow("Inspector", project->DockNode->ID);
			dockDragPhase = DockDragPhase::Prepare;
			ImGui::GetIO().WantSaveIniSettings = true;
			return {};
		}
		if (dockDragPhase == DockDragPhase::Prepare)
		{
			if (!game->DockNode->TabBar)
			{
				return Ursa::Result<void>::Failure("The Game dock node has no visible tab bar");
			}
			const auto tab = std::find_if(game->DockNode->TabBar->Tabs.begin(), game->DockNode->TabBar->Tabs.end(),
			                              [game](const ImGuiTabItem& item) { return item.Window == game; });
			if (tab == game->DockNode->TabBar->Tabs.end())
			{
				return Ursa::Result<void>::Failure("The Game panel tab was not found in its dock node");
			}
			const ImVec2 tabPosition(game->DockNode->TabBar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f,
			                         game->DockNode->TabBar->BarRect.GetCenter().y);
			gameTabPosition = tabPosition;
			MoveMainCursor(tabPosition);
			dockDragPhase = DockDragPhase::Hover;
			ImGui::GetIO().WantSaveIniSettings = true;
			return {};
		}
		if (dockDragPhase != DockDragPhase::None)
		{
			if (dockDragPhase == DockDragPhase::Hover)
			{
				DispatchMouse(Ursa::WindowEventType::MousePressed);
				dockDragPhase = DockDragPhase::Pressed;
			}
			else if (dockDragPhase == DockDragPhase::Pressed)
			{
				const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
				MoveMainCursor(ImVec2(mainViewport->Pos.x + mainViewport->Size.x - 40.0f, gameTabPosition.y + 200.0f));
				dockDragPhase = DockDragPhase::Dragging;
			}
			else if (dockDragPhase == DockDragPhase::Dragging)
			{
				dockDragPhase = DockDragPhase::UndockPending;
			}
			else if (dockDragPhase == DockDragPhase::UndockPending)
			{
				DispatchMouse(Ursa::WindowEventType::MouseReleased);
				dockDragPhase = DockDragPhase::ReleasePending;
			}
			else if (dockDragPhase == DockDragPhase::ReleasePending &&
			         (ImGui::GetPlatformIO().Viewports.Size > 1 ||
			          (game->Viewport && game->Viewport->ID != ImGui::GetMainViewport()->ID)))
			{
				dockDragPhase = DockDragPhase::Complete;
				floatingRequested = true;
			}
			else if (dockDragPhase == DockDragPhase::ReleasePending && frames >= 14)
			{
				const auto mouse = ImGui::GetIO().MousePos;
				const auto clicked = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
				const auto drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
				const auto tabBar = game->DockNode ? game->DockNode->TabBar : nullptr;
				return Ursa::Result<void>::Failure(
				    "A mouse drag on the Game tab did not create a platform viewport (mouse=" +
				    Ursa::ToUrsaString(std::to_string(mouse.x)) + "," + Ursa::ToUrsaString(std::to_string(mouse.y)) +
				    ", down=" + Ursa::ToUrsaString(std::to_string(ImGui::IsMouseDown(ImGuiMouseButton_Left))) +
				    ", tab=" + Ursa::ToUrsaString(std::to_string(tabBar ? tabBar->BarRect.Min.x : -1.0f)) + "," +
				    Ursa::ToUrsaString(std::to_string(tabBar ? tabBar->BarRect.Min.y : -1.0f)) + ":" +
				    Ursa::ToUrsaString(std::to_string(tabBar ? tabBar->BarRect.Max.x : -1.0f)) + "," +
				    Ursa::ToUrsaString(std::to_string(tabBar ? tabBar->BarRect.Max.y : -1.0f)) +
				    ", clicked=" + Ursa::ToUrsaString(std::to_string(clicked.x)) + "," +
				    Ursa::ToUrsaString(std::to_string(clicked.y)) + ", delta=" +
				    Ursa::ToUrsaString(std::to_string(drag.x)) + "," + Ursa::ToUrsaString(std::to_string(drag.y)) +
				    ", active=" + Ursa::ToUrsaString(std::to_string(ImGui::GetActiveID())) +
				    ", tabId=" + Ursa::ToUrsaString(std::to_string(game->TabId)) +
				    ", dockNode=" + Ursa::ToUrsaString(std::to_string(game->DockNode != nullptr)) + ", floating=" +
				    Ursa::ToUrsaString(std::to_string(game->DockNode && game->DockNode->IsFloatingNode())) +
				    ", viewport=" + Ursa::ToUrsaString(std::to_string(game->Viewport ? game->Viewport->ID : 0)) + ")");
			}
		}
		if (testCrossViewportDrag)
		{
			if (!workflowPlayObserved)
			{
				return {};
			}
			if (floatingRequested && ImGui::GetPlatformIO().Viewports.Size > 1)
			{
				if (!workflowModelGuid)
				{
					return frames >= 500
					           ? Ursa::Result<void>::Failure(
					                 "The Project Model source identity was not ready for cross-viewport drag")
					           : Ursa::Result<void>{};
				}
				ImGui::GetIO().WantSaveIniSettings = true;
				return DrawCrossViewportDrag(game);
			}
			if (frames >= 240 && !crossViewportDragChecked)
			{
				return Ursa::Result<void>::Failure("A DragSession was not delivered across Editor viewports");
			}
			return {};
		}
		if (testDetachedClose)
		{
			return {};
		}
		if (testCloseCancel)
		{
			if (minimizedViewportRendered)
			{
				checked = true;
			}
			return {};
		}
		if (inspector->DockNode != project->DockNode)
		{
			return frames >= 4 ? Ursa::Result<void>::Failure("Icon UI did not retain the customized Inspector dock")
			                   : Ursa::Result<void>{};
		}
		ImGui::GetIO().WantSaveIniSettings = true;
		if (minimizedViewportRendered)
		{
			if (!game->Viewport || game->Viewport->ID == ImGui::GetMainViewport()->ID)
			{
				return Ursa::Result<void>::Failure("Undocking Game did not create a platform viewport");
			}
			checked = true;
			ImGui::GetIO().WantSaveIniSettings = true;
		}
		return {};
	}

public:
	bool Checked() const
	{
		return checked && (!testCloseCancel || closeCancelChecked) && (!testDetachedClose || detachedCloseChecked) &&
		       (!testCrossViewportDrag || crossViewportDragChecked);
	}

private:
	enum class DockDragPhase
	{
		None,
		Prepare,
		Hover,
		Pressed,
		Dragging,
		UndockPending,
		ReleasePending,
		Complete
	};

	enum class RestoreDragPhase
	{
		None,
		Hover,
		Pressed,
		Dragging,
		DockPending,
		ReleasePending,
		Complete
	};

	enum class CrossViewportDragPhase
	{
		None,
		HoverSource,
		Pressed,
		Dragging,
		Transfer,
		Delivery,
		Complete
	};

	void MoveMainCursor(ImVec2 desktopPosition)
	{
		ImGui::GetIO().AddMousePosEvent(desktopPosition.x, desktopPosition.y);
	}

	void DispatchMouse(Ursa::WindowEventType type)
	{
		Ursa::WindowEvent mouse;
		mouse.type = type;
		mouse.button = Ursa::MouseButton::Left;
		Ursa::Detail::WindowAccess::Dispatch(*window, mouse);
	}

	Ursa::Result<void> PlaceDraggedModel(Ursa::ResourceId modelGuid)
	{
		if (modelGuid != workflowModelGuid)
		{
			return Ursa::Result<void>::Failure("The cross-viewport drag did not carry the Project Model identity");
		}
		const nlohmann::json modelDocument{
		    {"format", "UrsaModel"},
		    {"version", 2u},
		    {"rootNodeLocalFileId", 1u},
		    {"nodes",
		     nlohmann::json::array(
		         {{{"localFileId", 1u},
		           {"name", "CrossViewportProjectModel"},
		           {"localTransform",
		            {{"position", {0.f, 0.f, 0.f}}, {"rotation", {0.f, 0.f, 0.f, 1.f}}, {"scale", {1.f, 1.f, 1.f}}}},
		           {"children", nlohmann::json::array()}}})}};
		auto* document = Documents()->Current();
		auto draft = document ? document->Payload<Ursa::Editor::SceneDraft>() : nullptr;
		if (!draft || document->Path() != workflowScenePath.value)
		{
			return Ursa::Result<void>::Failure("The cross-viewport Model drop has no active workflow Scene document");
		}
		auto placed = draft->PlaceImportedAssets({{modelGuid, "Model", modelDocument}}, {}, std::nullopt,
		                                         workflowScenePath.value, document->Id());
		if (!placed || placed.value.size() != 1)
		{
			return Ursa::Result<void>::Failure(placed ? "Cross-viewport Model drop did not create one Scene object"
			                                          : placed.error);
		}
		document->MarkDirty();
		auto saved = Documents()->SaveCurrent();
		if (!saved)
		{
			return saved;
		}
		auto reopened =
		    Ursa::Editor::OpenSceneDocument(workflowScenePath.value, Ursa::ResourceKind::Scene, Project().components,
		                                    Documents()->Session(), Project().editContextSetup);
		if (!reopened)
		{
			return Ursa::Result<void>::Failure(reopened.error);
		}
		auto requested = Documents()->RequestOpen(std::move(reopened.value));
		if (!requested)
		{
			return requested;
		}
		auto* savedDocument = Documents()->Current();
		auto savedDraft = savedDocument ? savedDocument->Payload<Ursa::Editor::SceneDraft>() : nullptr;
		if (!savedDraft || savedDocument->Dirty() || savedDraft->GetScene().RootGameObjects().empty())
		{
			return Ursa::Result<void>::Failure(
			    "The Model placement did not survive saving and reopening the Scene (draft=" +
			    Ursa::ToUrsaString(std::to_string(savedDraft != nullptr)) + ", dirty=" +
			    Ursa::ToUrsaString(std::to_string(savedDocument ? savedDocument->Dirty() : false)) + ", roots=" +
			    Ursa::ToUrsaString(std::to_string(savedDraft ? savedDraft->GetScene().RootGameObjects().size() : 0)) +
			    ")");
		}
		auto placedObject = savedDraft->GetScene().GameObjectById(placed.value.front());
		auto placedName = placedObject ? savedDraft->GetScene().GameObjectName(placedObject.value)
		                               : Ursa::Result<Ursa::String>::Failure(placedObject.error);
		if (!placedName || placedName.value != "CrossViewportProjectModel")
		{
			return Ursa::Result<void>::Failure("The reopened Scene lost the cross-viewport Model identity or name");
		}
		workflowPlacementChecked = true;
		return {};
	}

	Ursa::Result<void> DrawCrossViewportDrag(ImGuiWindow* game)
	{
		if (workflowPlayStopRequested)
		{
			if (RunningScene())
			{
				return {};
			}
			workflowPlayStopRequested = false;
		}
		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const ImGuiViewport* targetViewport = game->Viewport;
		if (!targetViewport || targetViewport->ID == mainViewport->ID)
		{
			return Ursa::Result<void>::Failure("The cross-viewport drag target is not detached");
		}
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
		                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowViewport(mainViewport->ID);
		ImGui::SetNextWindowPos(ImVec2(mainViewport->Pos.x + 24.0f, mainViewport->Pos.y + 92.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f), ImGuiCond_Always);
		ImGui::Begin("Cross Viewport Drag Source", nullptr, flags);
		ImGui::Button("Project Asset");
		const ImVec2 sourceMinimum = ImGui::GetItemRectMin();
		const ImVec2 sourceMaximum = ImGui::GetItemRectMax();
		crossViewportSourcePosition =
		    ImVec2((sourceMinimum.x + sourceMaximum.x) * 0.5f, (sourceMinimum.y + sourceMaximum.y) * 0.5f);
		crossViewportSourceViewport = ImGui::GetWindowViewport()->ID;
		if (ImGui::BeginDragDropSource())
		{
			const auto session = Ursa::Editor::BeginEditorDragSession(
			    Ursa::Vector<Ursa::Editor::EditorSelectionEntry>{crossViewportDragSource}, crossViewportDragRevision);
			if (!session)
			{
				crossViewportDragError = session.error;
			}
			else
			{
				crossViewportDragPayload = session.value;
				ImGui::SetDragDropPayload(Ursa::Editor::editorDragSessionPayloadType, &crossViewportDragPayload,
				                          sizeof(crossViewportDragPayload));
				ImGui::TextUnformatted("Stable Project asset identity");
			}
			ImGui::EndDragDropSource();
		}
		ImGui::End();

		ImGui::SetNextWindowViewport(targetViewport->ID);
		ImGui::SetNextWindowPos(ImVec2(targetViewport->Pos.x + 28.0f, targetViewport->Pos.y + 82.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(240.0f, 120.0f), ImGuiCond_Always);
		ImGui::Begin("Cross Viewport Drag Target", nullptr, flags);
		ImGui::Button("Serialized Asset Reference");
		const ImVec2 targetMinimum = ImGui::GetItemRectMin();
		const ImVec2 targetMaximum = ImGui::GetItemRectMax();
		crossViewportTargetPosition =
		    ImVec2((targetMinimum.x + targetMaximum.x) * 0.5f, (targetMinimum.y + targetMaximum.y) * 0.5f);
		crossViewportTargetViewport = ImGui::GetWindowViewport()->ID;
		if (ImGui::BeginDragDropTarget())
		{
			if (const auto* payload = Ursa::Editor::AcceptEditorDragDropPayload())
			{
				const auto decoded = Ursa::Editor::DecodeEditorDragPayload(payload->DataType, payload->Data,
				                                                           static_cast<Ursa::Size>(payload->DataSize),
				                                                           crossViewportDragRevision);
				if (!decoded || decoded.value.sources.size() != 1 ||
				    !decoded.value.sources.front().identity.SameIdentity(crossViewportDragSource))
				{
					crossViewportDragError =
					    decoded ? "The cross-window target received the wrong asset identity" : decoded.error;
				}
				else
				{
					++crossViewportDropDeliveries;
					if (RunningScene())
					{
						workflowPlayDropDelivered = true;
						Ursa::FrameInput capturedInput;
						capturedInput.keys[static_cast<Ursa::Size>(Ursa::Key::Space)] = true;
						const auto& io = ImGui::GetIO();
						const bool dragActive = ImGui::GetDragDropPayload() != nullptr;
						const bool inputBlocked = Ursa::Editor::EditorInputBlocksPlay(
						    io.WantCaptureKeyboard, io.WantTextInput, dragActive,
						    ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
						workflowPlayDragInputBlocked =
						    dragActive && inputBlocked &&
						    !Ursa::Editor::SelectPlayInput(capturedInput, true, inputBlocked).KeyDown(Ursa::Key::Space);
					}
					else
					{
						auto placed = PlaceDraggedModel(decoded.value.sources.front().identity.assetGuid);
						if (!placed)
						{
							crossViewportDragError = placed.error;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::End();

		if (crossViewportDragPhase == CrossViewportDragPhase::None)
		{
			crossViewportDragSource.kind = Ursa::Editor::EditorSelectionKind::Asset;
			crossViewportDragSource.assetGuid = workflowModelGuid;
			crossViewportDragSource.assetKind = "Model";
			crossViewportDragSource.assetPath = workflowModelSourcePath.value;
			crossViewportDragSource.assetRole = Ursa::Editor::EditorAssetSelectionRole::Source;
			crossViewportDragStartFrame = frames;
			MoveMainCursor(crossViewportSourcePosition);
			crossViewportDragPhase = CrossViewportDragPhase::HoverSource;
		}
		else if (crossViewportDragPhase == CrossViewportDragPhase::HoverSource)
		{
			MoveMainCursor(crossViewportSourcePosition);
			DispatchMouse(Ursa::WindowEventType::MousePressed);
			crossViewportDragPhase = CrossViewportDragPhase::Pressed;
		}
		else if (crossViewportDragPhase == CrossViewportDragPhase::Pressed)
		{
			MoveMainCursor(ImVec2(crossViewportSourcePosition.x + 14.0f, crossViewportSourcePosition.y));
			crossViewportDragPhase = CrossViewportDragPhase::Dragging;
		}
		else if (crossViewportDragPhase == CrossViewportDragPhase::Dragging)
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();
			if (payload && payload->IsDataType(Ursa::Editor::editorDragSessionPayloadType))
			{
				MoveMainCursor(crossViewportTargetPosition);
				crossViewportDragPhase = CrossViewportDragPhase::Transfer;
			}
			else if (frames >= crossViewportDragStartFrame + 18)
			{
				return Ursa::Result<void>::Failure("The Project asset did not start an ImGui drag session");
			}
		}
		else if (crossViewportDragPhase == CrossViewportDragPhase::Transfer)
		{
			DispatchMouse(Ursa::WindowEventType::MouseReleased);
			crossViewportDragPhase = CrossViewportDragPhase::Delivery;
		}
		else if (crossViewportDragPhase == CrossViewportDragPhase::Delivery)
		{
			if (!crossViewportDragError.empty())
			{
				return Ursa::Result<void>::Failure(crossViewportDragError);
			}
			if (workflowPlayDropDelivered)
			{
				if (!RunningScene() || !workflowPlayDragInputBlocked || crossViewportDropDeliveries != 1)
				{
					return Ursa::Result<void>::Failure(
					    "The floating Game asset drag was not blocked from the active Play input");
				}
				Ursa::Editor::EndEditorDragSession(crossViewportDragPayload.token);
				workflowPlayDropDelivered = false;
				workflowPlayDragChecked = true;
				workflowPlayStopRequested = true;
				crossViewportDragPhase = CrossViewportDragPhase::None;
				crossViewportDropDeliveries = 0;
				RequestPlayToggle();
				return {};
			}
			if (crossViewportTargetViewport != targetViewport->ID ||
			    crossViewportSourceViewport == crossViewportTargetViewport || crossViewportDropDeliveries != 1 ||
			    !workflowPlacementChecked || !workflowPlayObserved || !workflowPlayDragChecked)
			{
				return frames >= 240
				           ? Ursa::Result<void>::Failure(
				                 "The cross-viewport asset drop did not resolve once to its detached reference target "
				                 "(source=" +
				                 Ursa::ToUrsaString(std::to_string(crossViewportSourceViewport)) +
				                 ", target=" + Ursa::ToUrsaString(std::to_string(crossViewportTargetViewport)) +
				                 ", expectedTarget=" + Ursa::ToUrsaString(std::to_string(targetViewport->ID)) +
				                 ", mouse=" + Ursa::ToUrsaString(std::to_string(ImGui::GetIO().MousePos.x)) + "," +
				                 Ursa::ToUrsaString(std::to_string(ImGui::GetIO().MousePos.y)) + ", deliveries=" +
				                 Ursa::ToUrsaString(std::to_string(crossViewportDropDeliveries)) + ", drag=" +
				                 Ursa::ToUrsaString(std::to_string(ImGui::GetDragDropPayload() != nullptr)) + ")")
				           : Ursa::Result<void>{};
			}
			Ursa::Editor::EndEditorDragSession(crossViewportDragPayload.token);
			crossViewportDragPhase = CrossViewportDragPhase::Complete;
			crossViewportDragChecked = true;
			checked = true;
		}
		return {};
	}

	unsigned frames = 0;
	bool checked = false;
	bool restoreLayout = false;
	bool testCloseCancel = false;
	bool testDetachedClose = false;
	bool testCrossViewportDrag = false;
	bool closeRequested = false;
	bool closeCancelChecked = false;
	bool detachedCloseRequested = false;
	bool detachedCloseChecked = false;
	bool floatingRequested = false;
	DockDragPhase dockDragPhase = DockDragPhase::None;
	ImVec2 gameTabPosition{};
	RestoreDragPhase restoreDragPhase = RestoreDragPhase::None;
	ImVec2 projectTabPosition{};
	CrossViewportDragPhase crossViewportDragPhase = CrossViewportDragPhase::None;
	Ursa::Editor::EditorSelectionEntry crossViewportDragSource;
	Ursa::Editor::EditorDragPayload crossViewportDragPayload;
	Ursa::UInt64 crossViewportDragRevision = 77;
	ImVec2 crossViewportSourcePosition{};
	ImVec2 crossViewportTargetPosition{};
	unsigned crossViewportDragStartFrame = 0;
	Ursa::UInt32 crossViewportSourceViewport = 0;
	Ursa::UInt32 crossViewportTargetViewport = 0;
	Ursa::Size crossViewportDropDeliveries = 0;
	Ursa::String crossViewportDragError;
	bool crossViewportDragChecked = false;
	Ursa::Result<Ursa::VirtualPath> workflowScenePath = Ursa::Result<Ursa::VirtualPath>::Failure("not initialized");
	Ursa::Result<Ursa::VirtualPath> workflowModelSourcePath =
	    Ursa::Result<Ursa::VirtualPath>::Failure("not initialized");
	Ursa::ResourceId workflowModelGuid;
	bool workflowPlacementChecked = false;
	bool workflowPlayObserved = false;
	bool workflowPlayDropDelivered = false;
	bool workflowPlayDragInputBlocked = false;
	bool workflowPlayDragChecked = false;
	bool workflowPlayStopRequested = false;
	unsigned workflowPlayStartAttempts = 0;
	unsigned workflowNextPlayAttemptFrame = 0;
	bool minimizeStarted = false;
	bool minimizedViewportRendered = false;
	unsigned minimizedFrames = 0;
	int lastViewportCount = 0;
	bool lastHostMinimized = false;
	bool lastFloatingPlatformHandle = false;
	bool lastFloatingRenderer = false;
};
} // namespace

int main(int argc, char** argv)
{
	const bool crossViewportDragOnly = argc > 1 && std::string(argv[1]) == "--cross-viewport-drag-only";
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root =
	    std::filesystem::temp_directory_path() / ("UrsaEditorWorkspaceWindowTests-" + std::to_string(unique));
	const auto userDataRoot = std::filesystem::absolute(root / "UserData").string();
#if defined(_WIN32)
	if (_putenv_s("URSA_USER_DATA_ROOT", userDataRoot.c_str()) != 0)
#else
	if (setenv("URSA_USER_DATA_ROOT", userDataRoot.c_str(), 1) != 0)
#endif
	{
		std::cerr << "Could not isolate the Editor window test user settings\n";
		return 1;
	}
	std::error_code error;
	std::filesystem::create_directories(root / "Assets", error);
	if (error)
	{
		std::cerr << error.message() << '\n';
		return 1;
	}
	Ursa::Result<void> ran;
	bool checked = false;
	const auto crossViewportDragRoot = root / "CrossViewportDragProject";
	if (crossViewportDragOnly)
	{
		ran = Ursa::Result<void>{};
		checked = true;
	}
	if (!crossViewportDragOnly)
	{
		WorkspaceEditor editor;
		auto opened = editor.OpenProject(root);
		if (opened)
		{
			opened = editor.SetFrameCapture(std::filesystem::current_path() / "IconEvidence/Workspace-Custom.png", 4);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 16) : opened;
		checked = editor.Checked();
	}
	if (!crossViewportDragOnly && ran && checked)
	{
		WorkspaceEditor editor(true);
		auto opened = editor.OpenProject(root);
		// Reload Engine UI resources from a real package while leaving the authoring project writable.
		if (opened)
		{
			auto storage = std::make_shared<Ursa::VirtualFileSystem>();
			auto mountedStorage =
			    storage->Mount(Ursa::editorLibraryScheme, root / "Library", Ursa::MountAccess::ReadOnly);
			auto snapshot = Ursa::ReadRuntimeContent(*storage, Ursa::EngineBuildConfiguration(), "EditorBootstrap");
			auto files = snapshot ? Ursa::OpenRuntimeContentView(storage, snapshot.value)
			                      : Ursa::Result<Ursa::VirtualFileSystem>::Failure(snapshot.error);
			Ursa::VirtualFileSystem output;
			auto mounted =
			    output.Mount("out", std::filesystem::current_path() / "IconEvidence", Ursa::MountAccess::ReadWrite);
			auto path = Ursa::VirtualPath::Parse("out:WorkspaceEngine.pak");
			opened = files && mounted && mountedStorage
			             ? Ursa::PakBuilder::Build(files.value, {Ursa::engineScheme}, output, path.value)
			             : Ursa::Result<void>::Failure(files ? mounted.error : files.error);
			if (opened)
			{
				auto packaged =
				    Ursa::OpenEngineFileSystem(std::filesystem::current_path() / "IconEvidence/WorkspaceEngine.pak");
				if (packaged)
				{
					Ursa::ConfigureEngineFileSystem(
					    std::make_shared<Ursa::VirtualFileSystem>(std::move(packaged.value)));
				}
				else
				{
					opened = Ursa::Result<void>::Failure(packaged.error);
				}
			}
		}
		if (opened)
		{
			opened = editor.SetFrameCapture(std::filesystem::current_path() / "IconEvidence/Workspace-Restored.png", 3);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 16) : opened;
		checked = editor.Checked();
	}
	if (!crossViewportDragOnly && ran && checked)
	{
		const auto closeCancelRoot = root / "CloseCancelProject";
		std::filesystem::create_directories(closeCancelRoot / "Assets", error);
		WorkspaceEditor editor(false, true);
		auto opened = error ? Ursa::Result<void>::Failure(Ursa::ToUrsaString(error.message()))
		                    : editor.OpenProject(closeCancelRoot);
		if (opened)
		{
			opened =
			    editor.SetFrameCapture(std::filesystem::current_path() / "IconEvidence/Workspace-CloseCanceled.png", 3);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 16) : opened;
		checked = editor.Checked();
	}
	if (!crossViewportDragOnly && ran && checked)
	{
		const auto detachedCloseRoot = root / "DetachedCloseProject";
		std::filesystem::create_directories(detachedCloseRoot / "Assets", error);
		WorkspaceEditor editor(false, false, true);
		auto opened = error ? Ursa::Result<void>::Failure(Ursa::ToUrsaString(error.message()))
		                    : editor.OpenProject(detachedCloseRoot);
		if (opened)
		{
			opened = editor.SetFrameCapture(
			    std::filesystem::current_path() / "IconEvidence/Workspace-DetachedClosed.png", 3);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 16) : opened;
		checked = editor.Checked();
	}
	if (crossViewportDragOnly || (ran && checked))
	{
		std::filesystem::create_directories(crossViewportDragRoot / "Assets", error);
		WorkspaceEditor editor(false, false, false, true);
		auto opened = error ? Ursa::Result<void>::Failure(Ursa::ToUrsaString(error.message()))
		                    : editor.OpenProject(crossViewportDragRoot);
		if (opened)
		{
			opened = editor.SetFrameCapture(
			    std::filesystem::current_path() / "IconEvidence/Workspace-CrossViewportDrag.png", 12);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 600) : opened;
		checked = editor.Checked();
	}
	if (ran && checked)
	{
		WorkspaceEditor editor(true);
		auto opened = editor.OpenProject(crossViewportDragRoot);
		if (opened)
		{
			opened = editor.SetFrameCapture(
			    std::filesystem::current_path() / "IconEvidence/Workspace-CrossViewportWorkflow-Restored.png", 4);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 16) : opened;
		checked = editor.Checked();
	}
	const auto scratch = Ursa::Memory::Snapshot()
	                         .categories[static_cast<Ursa::Size>(Ursa::Memory::RegisterTag("EditorFrameScratchTest"))];
	std::filesystem::remove_all(root, error);
	if (!ran || !checked || scratch.currentBytes != 0 || scratch.liveAllocations != 0)
	{
		std::cerr << (ran ? "Editor workspace was not checked" : ran.error.c_str()) << '\n';
		return 1;
	}
	return 0;
}
