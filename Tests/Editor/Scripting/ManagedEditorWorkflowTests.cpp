#include <Application/EditorApplication.hpp>
#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <Serialization/SerializedObject.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

const char* source = "using Ursa; namespace Workflow; public sealed class Probe : Behaviour { "
                     "private static int count; public int value = Defaults(); private static int Defaults() => 5; "
                     "public override void Awake() { Application.SetWindowTitle(\"Gameplay title\"); "
                     "GameObject.Name = $\"Runtime:{++count}:{value}\"; } }\n";

const char* editorWindowSource = "namespace Workflow; [Ursa.Editor.EditorWindowMenu(\"Window/Workflow State Probe\")] "
                                 "public sealed class WorkflowStateProbeWindow : Ursa.Editor.EditorWindow { "
                                 "private int drawCount; public override void OnGUI() { "
                                 "Ursa.Editor.EditorGui.Text($\"Managed draws: {++drawCount}\"); } }\n";

class WorkflowEditor final : public EditorApplication
{
public:
	bool Verified() const
	{
		return completed && responsiveFrames >= 2;
	}

protected:
	Result<void> OnStart() override
	{
		auto path = Take(VirtualPath::Parse("prj:Workflow.ursascene"));
		auto document = Take(NewSceneDocument(path, ResourceKind::Scene, Project().components, Documents()->Session()));
		draft = document->Payload<SceneDraft>();
		owner = Take(draft->GetScene().CreateGameObject("Editor root"));
		auto camera = Take(owner->AddComponent<CameraComponent>());
		Done(draft->GetScene().SetActiveCamera(camera->Id()));
		document->MarkDirty();
		Done(Documents()->RequestOpen(std::move(document)));
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		return {};
	}

	Result<void> OnGui() override
	{
		if (reportedPhase != phase)
		{
			reportedPhase = phase;
			std::cerr << "Editor workflow phase " << phase << std::endl;
		}
		auto rendered = EditorApplication::OnGui();
		if (!rendered)
		{
			return rendered;
		}
		try
		{
			Expect(window->Title().find("Gameplay title") == String::npos,
			       "A gameplay window title request replaced the Editor document title");
			if (std::chrono::steady_clock::now() > deadline)
			{
				throw std::runtime_error("Editor workflow timed out at phase " + std::to_string(phase) +
				                         ", running=" + std::to_string(RunningScene() != nullptr) +
				                         ", managedWindowMenuPhase=" + std::to_string(managedWindowMenuPhase) +
				                         ", managedWindowMenuAttempts=" + std::to_string(managedWindowMenuAttempts) +
				                         ", assets=" + ToStdString(AssetStatus()) + ": " + ToStdString(ScriptStatus()));
			}
			if (ScriptWorkPending())
			{
				if (phase == 6 && ScriptIdeStatus().find("正在同步") != String::npos)
				{
					const auto originalRoot = Workspace()->ProjectRoot();
					auto changedProject = OpenProject(originalRoot);
					Expect(!changedProject && changedProject.error.find("script IDE") != String::npos &&
					           Workspace()->ProjectRoot() == originalRoot,
					       "Project switching did not protect an active IDE generation request");
					checkedSwitchGuard = true;
				}
				if (phase == 8 && ScriptIdeStatus().find("正在同步") != String::npos)
				{
					completed = true;
					window->RequestClose();
				}
				++responsiveFrames;
				return {};
			}
			if (phase == 0)
			{
				const auto* descriptor = Project().componentEditors.FindType("Workflow.Probe, Ursa.Game");
				if (!descriptor)
				{
					return {};
				}
				if (!VerifyManagedWindowMenu())
				{
					return {};
				}
				Expect(ScriptDebugInfo().find("CoreCLR: initialized") != String::npos &&
				           ScriptDebugInfo().find(PathToUrsaString(Workspace()->ProjectRoot())) != String::npos,
				       "Script debug information does not identify the active host/project");
				ComponentInspectorContext context;
				context.object = owner;
				context.scene = &draft->GetScene();
				context.roots = draft->GetScene().RootGameObjects();
				script = static_cast<Scripting::ScriptBehaviourComponent*>(Take(descriptor->create(context)));
				auto payload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
				stableScriptId = ToUrsaString(payload.at("ScriptType").get<std::string>());
				Expect(payload["Fields"]["value"] == 5, "Add Component did not use the script constructor default");
				auto properties = Take(SerializedObject::Create(*script));
				Done(Take(properties->FindProperty("/Fields/value")).SetValue(19));
				Done(properties->Apply());
				WriteSource(String(source) + "public class Broken { invalid }\n");
				Done(CompileScripts());
				phase = 1;
			}
			else if (phase == 1)
			{
				Expect(!RunningScene() && ScriptStatus().find("prj:Probe.cs") != String::npos,
				       "Compilation diagnostics did not point to the source virtual path");
				Expect(Project().componentEditors.FindType("Workflow.Probe, Ursa.Game") != nullptr,
				       "Failed compilation removed the previous editing registration");
				Expect(!ScriptDiagnostics().empty() && ScriptDiagnostics()[0].source.relative == "Probe.cs" &&
				           ScriptDiagnostics()[0].line > 0 && ScriptDiagnostics()[0].column > 0,
				       "Editor has no structured error source location");
				Done(OpenCSharpProject({}, 1, 1, false));
				phase = 6;
			}
			else if (phase == 6)
			{
				Expect(checkedSwitchGuard, "IDE generation did not exercise the project-switch guard");
				Expect(ScriptIdeStatus().find("已同步") != String::npos,
				       "Broken script blocked an IDE regeneration request");
				const auto project = Take(Workspace()->Index().Filesystem().ReadText(
				    Take(VirtualPath::Parse("lib:ScriptIDE/Projects/Game/Ursa.Game.csproj"))));
				Expect(project.find("Probe.cs") != String::npos, "IDE project did not contain the broken script");
				Expect(!OpenScriptDiagnostic(ScriptDiagnostics().size()), "An invalid diagnostic index was navigable");
				Done(OpenScriptDiagnostic(0));
				phase = 7;
			}
			else if (phase == 7)
			{
				Expect(Project().componentEditors.FindType("Workflow.Probe, Ursa.Game") != nullptr,
				       "Opening an error in the IDE removed the last valid editing environment");
				RequestPlayToggle();
				phase = 2;
			}
			else if (phase == 2)
			{
				Expect(!RunningScene(), "Run accepted failed latest script inputs");
				WriteSource(source);
				Done(CompileScripts());
				RequestPlayToggle();
				phase = 3;
			}
			else if (phase == 3)
			{
				Expect(Take(owner->Name()) == "Editor root" &&
				           nlohmann::json::parse(ToStdString(script->SerializedPayload()))["Fields"]["value"] == 19,
				       "Reload changed an editing object or lost an unsaved field");
				phase = 4;
			}
			else if (phase == 4 && !RunningScene())
			{
				Expect(InspectorStatus().empty(), ("Run failed: " + ToStdString(InspectorStatus())).c_str());
			}
			else if (phase == 4 && RunningScene())
			{
				const auto roots = RunningScene()->RootGameObjects();
				GameObject::Ptr runtimeOwner;
				for (const auto& root : roots)
				{
					if (root->Id() == owner->Id())
					{
						runtimeOwner = root;
					}
				}
				if (!runtimeOwner || Take(runtimeOwner->Name()) == "Editor root")
				{
					return {};
				}
				if (Take(runtimeOwner->Name()) != "Runtime:1:19" || Take(owner->Name()) != "Editor root")
				{
					throw std::runtime_error(
					    "Unexpected Run clone: runtime=" + ToStdString(Take(runtimeOwner->Name())) + ", edit=" +
					    ToStdString(Take(owner->Name())) + ", payload=" + ToStdString(script->SerializedPayload()));
				}
				if (runs == 0)
				{
					if (!changedDuringRun)
					{
						WriteSource(String(source) + "public sealed class Added : Ursa.Behaviour {}\n");
						Expect(!CompileScripts(), "Run allowed an explicit script reload");
						changedDuringRun = true;
						const String helper = "public class RuntimeOnlyHelper {}\n";
						std::ofstream helperSource(Workspace()->ProjectRoot() / "Assets" / "RuntimeOnlyHelper.cs",
						                           std::ios::binary);
						helperSource << ToStdString(helper);
						helperSource.close();
						if (!helperSource)
						{
							throw std::runtime_error("Could not write the external Play-mode C# source change");
						}
						deferredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
						ideSyncDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
						return {};
					}
					Expect(Project().componentEditors.FindType("Workflow.Added, Ursa.Game") == nullptr,
					       "Run published source changes into the active editing catalog");
					if (std::chrono::steady_clock::now() < deferredDeadline)
					{
						return {};
					}
					const auto gameProject = Take(Workspace()->Index().Filesystem().ReadText(
					    Take(VirtualPath::Parse("lib:ScriptIDE/Projects/Game/Ursa.Game.csproj"))));
					if (gameProject.find("RuntimeOnlyHelper.cs") == String::npos)
					{
						if (std::chrono::steady_clock::now() < ideSyncDeadline)
						{
							return {};
						}
						throw std::runtime_error(
						    "IDE structure did not synchronize during Play: " + ToStdString(AssetStatus()) + "; " +
						    ToStdString(ScriptIdeStatus()) + "; " + ToStdString(ScriptStatus()));
					}
				}
				RequestPlayToggle();
				phase = 5;
			}
			else if (phase == 5 && RunningScene())
			{
				RequestPlayToggle();
			}
			else if (phase == 5 && !RunningScene())
			{
				if (runs == 0 && Project().componentEditors.FindType("Workflow.Added, Ursa.Game") == nullptr)
				{
					return {};
				}
				Expect(Documents()->Current()->Dirty() && Take(owner->Name()) == "Editor root",
				       "Stop lost the original editing document");
				if (++runs < 3)
				{
					RequestPlayToggle();
					phase = 4;
				}
				else
				{
					String renamed(source);
					renamed.replace(renamed.find("namespace Workflow;"), String("namespace Workflow;").size(),
					                "namespace RenamedWorkflow;");
					renamed.replace(renamed.find("class Probe"), String("class Probe").size(), "class RenamedProbe");
					WriteSource(renamed);
					Done(CompileScripts());
					phase = 9;
				}
			}
			else if (phase == 9)
			{
				const auto payload = nlohmann::json::parse(ToStdString(script->SerializedPayload()));
				Expect(script->TypeName() == "RenamedWorkflow.RenamedProbe, Ursa.Game" &&
				           payload["ScriptType"] == ToStdString(stableScriptId) && payload["Fields"]["value"] == 19 &&
				           Documents()->Current()->Dirty(),
				       "Attribute-free class/namespace rename lost its identity, live component or unsaved fields");
				Done(Documents()->SaveCurrent());
				Done(OpenCSharpProject({}, 1, 1, false));
				phase = 8;
			}
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(ToUrsaString(error.what()));
		}
		return {};
	}

private:
	bool VerifyManagedWindowMenu()
	{
		ImGuiWindow* managedWindow = nullptr;
		Size matchingWindows = 0;
		for (ImGuiWindow* candidate : GImGui->Windows)
		{
			if (candidate && std::string(candidate->Name).rfind("WorkflowStateProbeWindow###window:Workflow.", 0) == 0)
			{
				managedWindow = candidate;
				++matchingWindows;
			}
		}

		if (managedWindowMenuPhase == 2)
		{
			const ImGuiID itemSeed = ImHashStr("Workflow State Probe", 0, managedWindowPopup->IDStack.back());
			const ImGuiID expectedItemId = ImHashStr("", 0, itemSeed);
			if (GImGui->HoveredWindow != managedWindowPopup || GImGui->HoveredId != expectedItemId)
			{
				throw std::runtime_error(
				    "Injected menu click missed its target row (hovered=" + std::to_string(GImGui->HoveredId) +
				    ", mouse=" + std::to_string(managedMenuClickPosition.x) + "," +
				    std::to_string(managedMenuClickPosition.y) + ", popup=" +
				    (managedWindowPopup
				         ? std::string(managedWindowPopup->Name) + "@" + std::to_string(managedWindowPopup->Pos.x) +
				               "," + std::to_string(managedWindowPopup->Pos.y) + ":" +
				               std::to_string(managedWindowPopup->Size.x) + "," +
				               std::to_string(managedWindowPopup->Size.y) +
				               ", expected=" + std::to_string(expectedItemId)
				         : "null") +
				    ", hoveredWindow=" + (GImGui->HoveredWindow ? std::string(GImGui->HoveredWindow->Name) : "null") +
				    ")");
			}
			ImGui::ActivateItemByID(expectedItemId);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			managedWindowMenuPhase = 3;
			return false;
		}
		if (managedWindowMenuPhase == 3)
		{
			if (matchingWindows == 1 && managedWindow->LastFrameActive == GImGui->FrameCount)
			{
				if (managedWindowIdentity && managedWindowIdentity != managedWindow)
				{
					throw std::runtime_error("Opening a managed EditorWindow created a second ImGui window identity");
				}
				managedWindowIdentity = managedWindow;
				++managedWindowDrawFrames;
				if (managedWindowDrawFrames >= 3)
				{
					managedWindowMenuPhase = 4;
				}
				return managedWindowMenuPhase == 4;
			}
			if (++managedWindowMenuAttempts > 8)
			{
				String windowNames;
				for (ImGuiWindow* candidate : GImGui->Windows)
				{
					if (candidate && std::string(candidate->Name).find("Workflow") != std::string::npos)
					{
						windowNames += ToUrsaString(candidate->Name) + ";";
					}
				}
				throw std::runtime_error(
				    "The Window menu did not open the managed EditorWindow exactly once (windows=" +
				    ToStdString(windowNames) + ", active=" + std::to_string(GImGui->ActiveId) +
				    ", hover=" + std::to_string(GImGui->HoveredId) + ")");
			}
			return false;
		}
		if (managedWindowMenuPhase == 5)
		{
			ImGuiPopupData* popup = GImGui->OpenPopupStack.Size ? &GImGui->OpenPopupStack.back() : nullptr;
			if (!popup || !popup->Window || std::string(popup->Window->Name).rfind("Window###Menu_00", 0) != 0)
			{
				throw std::runtime_error("The Window menu popup closed before the managed item could be selected");
			}
			managedWindowPopup = popup->Window;
			managedMenuClickPosition = ImVec2(popup->Window->Pos.x + popup->Window->Size.x * 0.5f,
			                                  popup->Window->Pos.y + popup->Window->Size.y -
			                                      ImGui::GetStyle().WindowPadding.y - ImGui::GetFontSize() * 0.5f);
			ImGui::GetIO().AddMousePosEvent(managedMenuClickPosition.x, managedMenuClickPosition.y);
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			managedWindowMenuPhase = 2;
			return false;
		}
		if (managedWindowMenuPhase == 4)
		{
			if (matchingWindows != 1 || managedWindow != managedWindowIdentity ||
			    managedWindow->LastFrameActive != GImGui->FrameCount)
			{
				throw std::runtime_error(
				    "The managed EditorWindow did not retain its single active window across frames");
			}
			return VerifyManagedWindowDocking();
		}

		if (managedWindowMenuPhase == 0)
		{
			ImGuiWindow* menuBar = ImGui::FindWindowByName("##MainMenuBar");
			if (!menuBar)
			{
				return false;
			}
			const ImGuiID menuBarSeed = ImHashStr("##MenuBar", 0, menuBar->IDStack.back());
			const ImGuiID menuSeed = ImHashStr("Window", 0, menuBarSeed);
			ImGui::ActivateItemByID(ImHashStr("", 0, menuSeed));
			managedWindowMenuPhase = 1;
			return false;
		}

		if (managedWindowMenuPhase == 1)
		{
			ImGuiPopupData* popup = GImGui->OpenPopupStack.Size ? &GImGui->OpenPopupStack.back() : nullptr;
			if (!popup || !popup->Window || std::string(popup->Window->Name).rfind("Window###Menu_00", 0) != 0)
			{
				if (++managedWindowMenuAttempts > 12)
				{
					throw std::runtime_error(
					    "The Window main-menu item did not open its ImGui popup via injected navigation");
				}
				managedWindowMenuPhase = 0;
				return false;
			}
			managedWindowMenuPhase = 5;
			return false;
		}
		return false;
	}

	bool VerifyManagedWindowDocking()
	{
		ImGuiWindow* currentManagedWindow = nullptr;
		Size managedWindowCount = 0;
		for (ImGuiWindow* candidate : GImGui->Windows)
		{
			if (candidate && std::string(candidate->Name).rfind("WorkflowStateProbeWindow###window:Workflow.", 0) == 0)
			{
				currentManagedWindow = candidate;
				++managedWindowCount;
			}
		}
		if (managedWindowCount != 1 || currentManagedWindow != managedWindowIdentity ||
		    (managedWindowDockPhase != 14 && managedWindowIdentity->LastFrameActive != GImGui->FrameCount))
		{
			throw std::runtime_error("The managed EditorWindow identity changed during detach/re-dock");
		}
		ImGuiWindow* inspector = ImGui::FindWindowByName("Inspector");
		if (!inspector || !inspector->DockNode)
		{
			return false;
		}
		if (managedWindowDockPhase == 0)
		{
			ImGui::DockBuilderDockWindow(managedWindowIdentity->Name, inspector->DockNode->ID);
			managedWindowDockPhase = 1;
			return false;
		}
		if (managedWindowDockPhase == 1)
		{
			if (managedWindowIdentity->DockNode != inspector->DockNode || !inspector->DockNode->TabBar)
			{
				if (++managedWindowDockAttempts > 8)
				{
					throw std::runtime_error("The managed EditorWindow did not join the Inspector dock tab bar");
				}
				return false;
			}
			const auto tab =
			    std::find_if(inspector->DockNode->TabBar->Tabs.begin(), inspector->DockNode->TabBar->Tabs.end(),
			                 [this](const ImGuiTabItem& item) { return item.Window == managedWindowIdentity; });
			if (tab == inspector->DockNode->TabBar->Tabs.end())
			{
				return false;
			}
			managedWindowTabPosition =
			    ImVec2(inspector->DockNode->TabBar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f,
			           inspector->DockNode->TabBar->BarRect.GetCenter().y);
			MoveMainCursor(managedWindowTabPosition);
			managedWindowDockPhase = 2;
			return false;
		}
		if (managedWindowDockPhase == 2)
		{
			DispatchMouse(WindowEventType::MousePressed);
			managedWindowDockPhase = 3;
			return false;
		}
		if (managedWindowDockPhase == 3)
		{
			const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			MoveMainCursor(
			    ImVec2(mainViewport->Pos.x + mainViewport->Size.x - 40.0f, managedWindowTabPosition.y + 180.0f));
			managedWindowDockPhase = 4;
			return false;
		}
		if (managedWindowDockPhase == 4)
		{
			managedWindowDockPhase = 5;
			return false;
		}
		if (managedWindowDockPhase == 5)
		{
			DispatchMouse(WindowEventType::MouseReleased);
			managedWindowDockPhase = 6;
			return false;
		}
		if (managedWindowDockPhase == 6)
		{
			if (managedWindowIdentity->Viewport && managedWindowIdentity->Viewport->ID != ImGui::GetMainViewport()->ID)
			{
				if (!inspector->DockNode->TabBar)
				{
					throw std::runtime_error("The Inspector dock target has no visible tab bar");
				}
				const auto inspectorTab =
				    std::find_if(inspector->DockNode->TabBar->Tabs.begin(), inspector->DockNode->TabBar->Tabs.end(),
				                 [inspector](const ImGuiTabItem& item) { return item.Window == inspector; });
				if (inspectorTab == inspector->DockNode->TabBar->Tabs.end())
				{
					return false;
				}
				managedWindowDockTarget = ImVec2(inspector->DockNode->TabBar->BarRect.Min.x + inspectorTab->Offset +
				                                     inspectorTab->Width * 0.5f,
				                                 inspector->DockNode->TabBar->BarRect.GetCenter().y);
				MoveMainCursor(managedWindowIdentity->TitleBarRect().GetCenter());
				managedWindowDockPhase = 7;
				return false;
			}
			if (++managedWindowDockAttempts > 18)
			{
				throw std::runtime_error("Dragging the managed EditorWindow tab did not create a platform viewport");
			}
			return false;
		}
		if (managedWindowDockPhase == 7)
		{
			DispatchMouse(WindowEventType::MousePressed);
			managedWindowDockPhase = 8;
			return false;
		}
		if (managedWindowDockPhase == 8)
		{
			MoveMainCursor(managedWindowDockTarget);
			managedWindowDockPhase = 9;
			return false;
		}
		if (managedWindowDockPhase == 9)
		{
			managedWindowDockPhase = 10;
			return false;
		}
		if (managedWindowDockPhase == 10)
		{
			DispatchMouse(WindowEventType::MouseReleased);
			managedWindowDockPhase = 11;
			return false;
		}
		if (managedWindowDockPhase == 11)
		{
			if (managedWindowIdentity->DockNode == inspector->DockNode && managedWindowIdentity->Viewport &&
			    managedWindowIdentity->Viewport->ID == ImGui::GetMainViewport()->ID)
			{
				const auto tab =
				    std::find_if(inspector->DockNode->TabBar->Tabs.begin(), inspector->DockNode->TabBar->Tabs.end(),
				                 [this](const ImGuiTabItem& item) { return item.Window == managedWindowIdentity; });
				if (tab == inspector->DockNode->TabBar->Tabs.end())
				{
					throw std::runtime_error("The re-docked managed EditorWindow tab disappeared before close testing");
				}
				managedWindowClosePosition =
				    ImVec2(inspector->DockNode->TabBar->BarRect.Min.x + tab->Offset + tab->Width -
				               inspector->DockNode->TabBar->FramePadding.x - ImGui::GetFontSize() * 0.5f,
				           inspector->DockNode->TabBar->BarRect.Min.y + inspector->DockNode->TabBar->FramePadding.y +
				               ImGui::GetFontSize() * 0.5f);
				MoveMainCursor(managedWindowClosePosition);
				managedWindowDockPhase = 12;
				return false;
			}
			if (++managedWindowDockAttempts > 26)
			{
				throw std::runtime_error("The detached managed EditorWindow did not rejoin the Inspector dock");
			}
		}
		if (managedWindowDockPhase == 12)
		{
			const ImGuiID closeButtonId = ImHashStr("#CLOSE", 0, managedWindowIdentity->ID);
			if (GImGui->HoveredId != closeButtonId)
			{
				throw std::runtime_error("The managed EditorWindow tab close button was not hovered");
			}
			DispatchMouse(WindowEventType::MousePressed);
			managedWindowDockPhase = 13;
			return false;
		}
		if (managedWindowDockPhase == 13)
		{
			DispatchMouse(WindowEventType::MouseReleased);
			managedWindowDockPhase = 14;
			return false;
		}
		if (managedWindowDockPhase == 14)
		{
			if (managedWindowIdentity->LastFrameActive != GImGui->FrameCount && Documents()->Current() &&
			    Documents()->Current()->Dirty() && !window->ShouldClose())
			{
				return true;
			}
			if (++managedWindowDockAttempts > 32)
			{
				throw std::runtime_error("Closing the managed EditorWindow view also closed its dirty document");
			}
		}
		return false;
	}

	void MoveMainCursor(ImVec2 desktopPosition)
	{
		ImGui::GetIO().AddMousePosEvent(desktopPosition.x, desktopPosition.y);
	}

	void DispatchMouse(WindowEventType type)
	{
		WindowEvent event;
		event.type = type;
		event.button = MouseButton::Left;
		Detail::WindowAccess::Dispatch(*window, event);
	}

	void WriteSource(const String& text)
	{
		Vector<UInt8> bytes(text.begin(), text.end());
		Done(Workspace()->Index().Filesystem().WriteAtomic(Take(VirtualPath::Parse("prj:Probe.cs")), bytes));
	}

	std::shared_ptr<SceneDraft> draft;
	GameObject::Ptr owner;
	Scripting::ScriptBehaviourComponent* script = nullptr;
	String stableScriptId;
	std::chrono::steady_clock::time_point deadline;
	UInt32 reportedPhase = ~0u;
	UInt32 phase = 0;
	UInt32 runs = 0;
	UInt32 responsiveFrames = 0;
	bool completed = false;
	bool changedDuringRun = false;
	bool checkedSwitchGuard = false;
	UInt32 managedWindowMenuPhase = 0;
	UInt32 managedWindowMenuAttempts = 0;
	UInt32 managedWindowDrawFrames = 0;
	ImGuiWindow* managedWindowIdentity = nullptr;
	ImGuiWindow* managedWindowPopup = nullptr;
	ImVec2 managedMenuClickPosition{};
	UInt32 managedWindowDockPhase = 0;
	UInt32 managedWindowDockAttempts = 0;
	ImVec2 managedWindowTabPosition{};
	ImVec2 managedWindowDockTarget{};
	ImVec2 managedWindowClosePosition{};
	std::chrono::steady_clock::time_point deferredDeadline;
	std::chrono::steady_clock::time_point ideSyncDeadline;
};
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("ManagedEditorWorkflow-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root / "Assets");
		std::ofstream(root / "Assets" / "Probe.cs") << source;
		std::filesystem::create_directories(root / "Assets" / "Editor");
		std::ofstream(root / "Assets" / "Editor" / "WorkflowStateProbeWindow.cs") << editorWindowSource;
		WorkflowEditor editor;
		Done(editor.OpenProject(root));
		Done(editor.Run(1280, 720, "Managed Editor Workflow", false));
		Expect(editor.Verified(), "Editor did not render while compiling or did not finish the workflow");
		std::cout << "Editor workflow: compile/error/recovery/reload/unsaved clone/3 Run-Stop cycles/save passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
