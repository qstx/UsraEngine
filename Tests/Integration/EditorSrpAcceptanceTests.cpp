#include <Application/EditorApplication.hpp>
#include "CaptureCliAcceptance.hpp"
#include <Assets/SceneDraft.hpp>
#include <Rendering/Pipeline/GraphicsSettings.hpp>
#include <ScriptableAssets/ScriptableAssetFile.hpp>
#include <Core/StringInterop.hpp>
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

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

constexpr const char* pipelineSource = R"(
using System.Collections.Generic;
using Ursa;
using Ursa.Rendering;
[UrsaScriptType("test.editor-acceptance-pipeline")]
[CreateAssetMenu(MenuName = "Rendering/SRP Acceptance Pipeline", FileName = "AcceptancePipeline")]
public sealed class AcceptancePipelineAsset : RenderPipelineAsset
{
    public bool enabled;
    protected override RenderPipeline CreatePipeline() => new AcceptancePipeline(enabled);
}
public sealed class AcceptancePipeline : RenderPipeline
{
    private readonly bool enabled;
    private readonly CommandBuffer commands = new("Editor acceptance camera colors");
    public AcceptancePipeline(bool value) => enabled = value;
    public override void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
    {
        foreach (var camera in cameras)
        {
            context.SetupCameraProperties(camera);
            commands.Clear();
            var color = !enabled ? new Vector4(0, 0, 0, 1) : camera.Type switch
            {
                CameraType.SceneView => new Vector4(0.1f, 0.8f, 0.3f, 1),
                CameraType.Preview => new Vector4(0.85f, 0.25f, 0.6f, 1),
                _ => new Vector4(0.15f, 0.3f, 0.9f, 1)
            };
            commands.ClearTarget(true, true, false, color);
            context.ExecuteCommandBuffer(commands);
            context.Submit();
        }
    }
    protected override void Dispose(bool disposing) => commands.Dispose();
}
)";

class AcceptanceEditor final : public EditorApplication
{
public:
	explicit AcceptanceEditor(std::filesystem::path output) : evidence(std::move(output))
	{
		SetStartupAsset(Take(VirtualPath::Parse("prj:MaterialShowcase.ursascene")));
	}

	bool Verified() const
	{
		return verified;
	}

protected:
	Result<void> OnStart() override
	{
		// Includes external capture processes and diagnostic builds in Debug.
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
		return EditorApplication::OnStart();
	}

	Result<void> OnGui() override
	{
		auto result = EditorApplication::OnGui();
		if (!result)
		{
			return result;
		}
		try
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				throw std::runtime_error("SRP acceptance timed out at phase " + std::to_string(phase) + ": " +
				                         ToStdString(AssetStatus()) + "; " + ToStdString(ScriptStatus()) + "; " +
				                         ToStdString(InspectorStatus()));
			}
			if (AssetWorkPending() || ScriptWorkPending() || ++settledFrames < 4)
			{
				return {};
			}
			auto& files = Workspace()->Index().Filesystem();
			switch (phase)
			{
				case 0:
				{
					draft = Documents()->Current()->Payload<SceneDraft>();
					Expect(bool(draft), "Demo Scene document was not opened");
					marker = Take(draft->GetScene().CreateGameObject("Unsaved SRP acceptance marker"));
					Documents()->Current()->MarkDirty();
					Activate("##MainMenuBar", "Assets");
					Next();
					break;
				}
				case 1:
					Activate("##Menu_00", "Create");
					Next();
					break;
				case 2:
					Activate("##Menu_01", "Rendering/SRP Acceptance Pipeline");
					Next();
					break;
				case 3:
				{
					if (!Take(files.Exists(assetPath)))
					{
						return {};
					}
					guid = Take(Workspace()->Database()->Metadata(assetPath)).guid;
					Expect(!Asset().Payload()["Fields"]["enabled"].get<bool>(), "Created asset lost its default");
					SetStartupWindow("Inspector");
					Capture("01Created.png");
					auto* inspector = ImGui::FindWindowByName("Inspector");
					Expect(inspector != nullptr, "Created asset Inspector was not visible");
					ImGui::FocusWindow(inspector);
					ImGui::ActivateItemByID(ImHashStr("##Value", 0, ImHashStr("enabled", 0, inspector->ID)));
					Next();
					break;
				}
				case 4:
					Expect(!Asset().Payload()["Fields"]["enabled"].get<bool>(), "Unsaved UI edit changed the source");
					Activate("Inspector", "Save ScriptableObject");
					Next();
					break;
				case 5:
					Expect(Asset().Payload()["Fields"]["enabled"].get<bool>(),
					       "Inspector widget did not save its field");
					Capture("02Saved.png");
					Activate("##MainMenuBar", "Window");
					Next();
					break;
				case 6:
					Activate("##Menu_00", "Project Graphics Settings");
					Next();
					break;
				case 7:
					Activate("Project Graphics Settings", "##DefaultRenderPipeline");
					Next();
					break;
				case 8:
					Activate("##Combo_00", assetPath.ToString() + "##" + guid.ToString());
					Next();
					break;
				case 9:
					Activate("Project Graphics Settings", "Apply Graphics Settings");
					Next();
					break;
				case 10:
					if (!InspectorStatus().empty())
					{
						return {};
					}
					Expect(Take(Rendering::ReadGraphicsSettings(files)).renderPipeline.guid == guid,
					       "Graphics widgets did not apply the created pipeline");
					FocusPanel("Scene");
					Capture("03Scene.png");
					if (!SelectProjectItem("Materials", "[Folder] Materials"))
					{
						return {};
					}
					Next();
					break;
				case 11:
					if (!SelectProjectItem("Materials/Gold.ursamat", "Gold.ursamat"))
					{
						return {};
					}
					Capture("ProjectSelection.png");
					Next();
					break;
				case 12:
					if (!ImGui::FindWindowByName("Material Preview"))
					{
						Expect(settledFrames < 30, "Material selection opened no preview; see ProjectSelection.png");
						return {};
					}
					SetStartupWindow("Material Preview");
					Capture("04Preview.png");
					RequestPlayToggle();
					Next();
					break;
				case 13:
					if (!RunningScene())
					{
						return {};
					}
					FocusPanel("Game");
					Capture("05Game" + ToUrsaString(std::to_string(runs)) + ".png");
					if (runs == 0)
					{
						Take(Take(RunningScene()->CreateGameObject("Capture simulation probe"))
						         ->AddComponent<PauseProbe>(simulationUpdates));
						Activate("##EditorToolbar", "Pause");
						phase = 23;
						settledFrames = 0;
					}
					else
					{
						Next();
					}
					break;
				case 14:
					if (runs == 0)
					{
						Expect(!PlayPaused() && simulationUpdates > pausedUpdates,
						       "Resume did not restart simulation after capture");
					}
					RequestPlayToggle();
					Next();
					break;
				case 15:
					if (RunningScene())
					{
						return {};
					}
					Expect(Documents()->Current()->Dirty() && Take(marker->Name()) == "Unsaved SRP acceptance marker",
					       "Run/Stop lost an unsaved editing object");
					if (++runs < 3)
					{
						RequestPlayToggle();
						phase = 13;
						settledFrames = 0;
						break;
					}
					std::ofstream(Workspace()->ProjectRoot() / "Assets/AcceptancePipeline.cs", std::ios::app)
					    << "\n// Stop reload acceptance\n";
					Done(CompileScripts());
					Next();
					break;
				case 16:
					Expect(Asset().Payload()["Fields"]["enabled"].get<bool>() &&
					           Take(Rendering::ReadGraphicsSettings(files)).renderPipeline.guid == guid,
					       "Reload lost the saved field or pipeline selection");
					window->Resize(1100, 760);
					window->Minimize();
					window->PollEvents();
					window->Restore();
					FocusPanel("Scene");
					Capture("06ReloadedScene.png");
					Next();
					break;
				case 17:
					Expect(InspectorStatus().empty(), "Viewport pipeline reported an Inspector diagnostic");
					Activate("Project Graphics Settings", "##DefaultRenderPipeline");
					Next();
					break;
				case 18:
					Activate("##Combo_00", "Default Forward");
					Next();
					break;
				case 19:
					Activate("Project Graphics Settings", "Apply Graphics Settings");
					Next();
					break;
				case 20:
					Expect(!Take(Rendering::ReadGraphicsSettings(files)).renderPipeline.guid,
					       "Clearing the Graphics selection did not restore default Forward");
					Capture("07DefaultForward.png");
					Done(Documents()->SaveCurrent());
					Next();
					break;
				case 21:
					verified = true;
					window->RequestClose();
					break;
				case 23:
					if (!PlayPaused())
					{
						return {};
					}
					pausedUpdates = simulationUpdates;
					selectionBeforeCapture = SelectionState();
					captureFuture = std::async(std::launch::async, [this]
					                           { return Test::RunCaptureCli(evidence / "08OnDemandPause.png"); });
					Next();
					break;
				case 24:
					if (captureFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
					{
						return {};
					}
					firstCapture = Take(captureFuture.get());
					CheckCapture(firstCapture);
					std::ofstream(evidence / "08OnDemandPause.result.json")
					    << Diagnostics::SerializeCaptureResult(firstCapture).c_str();
					captureFuture = std::async(std::launch::async, [this]
					                           { return Test::RunCaptureCli(evidence / "09OnDemandRepeat.png"); });
					Next();
					break;
				case 25:
				{
					if (captureFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
					{
						return {};
					}
					const auto repeated = Take(captureFuture.get());
					CheckCapture(repeated);
					Expect(repeated.frameSerial > firstCapture.frameSerial &&
					           repeated.request.requestId != firstCapture.request.requestId,
					       "Repeated external CLI request did not capture another completed Editor frame");
					std::ofstream(evidence / "09OnDemandRepeat.result.json")
					    << Diagnostics::SerializeCaptureResult(repeated).c_str();
					Activate("##EditorToolbar", "Pause");
					phase = 14;
					settledFrames = 0;
					break;
				}
			}
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure(ToUrsaString(error.what()));
		}
		return {};
	}

private:
	class PauseProbe final : public Component
	{
	public:
		explicit PauseProbe(UInt64& counter) : updates(counter)
		{
		}

		Result<void> Update(const FrameContext&) override
		{
			++updates;
			return {};
		}

	private:
		UInt64& updates;
	};

	void CheckCapture(const Diagnostics::CaptureResult& result)
	{
		Expect(result.Succeeded() && result.request.pid == Diagnostics::CaptureProcessId() &&
		           result.width == window->FramebufferWidth() && result.height == window->FramebufferHeight(),
		       "External CLI did not return the current Editor framebuffer");
		Expect(PlayPaused() && simulationUpdates == pausedUpdates && Documents()->Current()->Dirty(),
		       "Capturing resumed simulation or changed Dirty state");
		const auto& selected = SelectionState();
		Expect(selected.kind == selectionBeforeCapture.kind && selected.objectId == selectionBeforeCapture.objectId &&
		           selected.documentId == selectionBeforeCapture.documentId &&
		           selected.assetPath == selectionBeforeCapture.assetPath,
		       "Capturing changed the Editor selection");
	}

	std::future<Result<Diagnostics::CaptureResult>> captureFuture;
	Diagnostics::CaptureResult firstCapture;
	EditorSelectionContext selectionBeforeCapture;
	UInt64 simulationUpdates = 0;
	UInt64 pausedUpdates = 0;

	void Next()
	{
		std::cout << "Completed UI phase " << phase++ << '\n';
		settledFrames = 0;
	}

	void Capture(const String& name)
	{
		Done(SetFrameCapture(evidence / PathFromUrsaString(name), 2));
	}

	void FocusPanel(const char* name)
	{
		auto* panel = ImGui::FindWindowByName(name);
		if (panel && panel->DockNode && panel->DockNode->TabBar)
		{
			if (auto* tab = ImGui::TabBarFindTabByID(panel->DockNode->TabBar, panel->TabId))
			{
				ImGui::TabBarQueueFocus(panel->DockNode->TabBar, tab);
			}
		}
		SetStartupWindow(name);
	}

	ScriptableAsset Asset()
	{
		return Take(LoadScriptableAssetSource(Workspace()->Index().Filesystem(), assetPath, guid));
	}

	void Activate(const char* windowName, const String& label)
	{
		auto* target = ImGui::FindWindowByName(windowName);
		if (!target && std::string(windowName).find("##") == 0)
		{
			for (auto* candidate : GImGui->Windows)
			{
				if (candidate->Active &&
				    std::string(candidate->Name).find(std::string(windowName).substr(2)) != std::string::npos)
				{
					target = candidate;
					break;
				}
			}
		}
		if (!target || !target->Active)
		{
			std::cerr << "Navigation activation=" << GImGui->NavActivateId << ", submitted menus:";
			for (const auto menu : GImGui->MenusIdSubmittedThisFrame)
			{
				std::cerr << ' ' << menu;
			}
			for (auto* candidate : GImGui->Windows)
			{
				if (candidate->Active && std::string(candidate->Name).find("Menu") != std::string::npos)
				{
					std::cerr << " active=" << candidate->Name;
				}
			}
			std::cerr << '\n';
			throw std::runtime_error(std::string("Expected interactive window is not active: ") + windowName);
		}
		ImGui::FocusWindow(target);
		const ImGuiID seed =
		    std::string(windowName) == "##MainMenuBar" ? ImHashStr("##MenuBar", 0, target->ID) : target->ID;
		ImGui::ActivateItemByID(ImHashStr(label.c_str(), 0, seed));
		std::cout << "Activate " << windowName << '/' << label.c_str() << " id=" << GImGui->NavNextActivateId << '\n';
	}

	bool SelectProjectItem(const char* path, const char* label)
	{
		auto* projectWindow = ImGui::FindWindowByName("Project");
		if (projectWindow && projectWindow->Active && projectWindow->DockNode && projectWindow->Size.y < 400)
		{
			ImGui::DockBuilderSetNodeSize(projectWindow->DockNode->ID, {projectWindow->Size.x, 450});
			ImGui::SetScrollY(projectWindow, 0);
			return false;
		}
		for (auto* target : GImGui->Windows)
		{
			if (target->Active && std::string(target->Name).find("ProjectFolderContents") != std::string::npos)
			{
				ImGui::FocusWindow(target);
				const ImGuiID item = ImHashStr(label, 0, ImHashStr(path, 0, target->ID));
				ImGui::SetFocusID(item, target);
				ImGui::ActivateItemByID(item);
				return true;
			}
		}
		SetStartupWindow("Project");
		return false;
	}

	const VirtualPath assetPath = Take(VirtualPath::Parse("prj:AcceptancePipeline.ursaasset"));
	std::filesystem::path evidence;
	std::chrono::steady_clock::time_point deadline;
	std::shared_ptr<SceneDraft> draft;
	GameObject::Ptr marker;
	ResourceId guid;
	UInt32 phase = 0;
	UInt32 settledFrames = 0;
	UInt32 runs = 0;
	bool verified = false;
};
} // namespace

int main(int count, char** arguments)
{
	std::cout << std::unitbuf;
	try
	{
		Expect(count == 3, "Expected Demo project and evidence output root");
		const auto root = std::filesystem::u8path(arguments[2]) / ToStdString(ResourceId::Generate().ToString());
		std::filesystem::create_directories(root);
		std::filesystem::copy(std::filesystem::u8path(arguments[1]) / "Assets", root / "Assets",
		                      std::filesystem::copy_options::recursive);
		std::ofstream(root / "Assets/AcceptancePipeline.cs") << pipelineSource;
		std::cout << "SRP evidence: " << root << '\n';
		AcceptanceEditor editor(root);
		Done(editor.OpenProject(root));
		Done(editor.Run(1280, 720, "Ursa Editor SRP acceptance", false));
		Expect(editor.Verified(), "SRP Editor workflow did not complete");
		std::cout << "SRP actual widgets: create, edit/save, Graphics, Scene/Game/Preview, reload, 3 Run/Stop passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
