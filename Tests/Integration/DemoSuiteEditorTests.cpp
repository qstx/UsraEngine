#include <Application/EditorApplication.hpp>
#include <Application/FrameReplay.hpp>
#include <Assets/SceneDraft.hpp>
#include <Rendering/Diagnostics/FrameCapture.hpp>
#include <Serialization/SceneResource.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/PakBuilder.hpp>
#include <fstream>
#include <imgui_internal.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace Ursa::Editor
{
struct EditorPlayToolbarTest
{
	static PlaySession& Play(EditorApplication& app)
	{
		return app.play;
	}

	static SceneViewPanel& Panel(EditorApplication& app)
	{
		return *app.sceneViewPanel;
	}

	static void Show(EditorApplication& app)
	{
		app.showGamePanel = true;
		app.showScenePanel = true;
	}

	static void DeferSourcePolling(EditorApplication& app)
	{
		// This recording does not edit source files. Keep periodic IDE generation
		// from obscuring the explicit initial compile and the measured Run cycles.
		app.nextScriptCheck = std::chrono::steady_clock::now() + std::chrono::minutes(10);
	}

	static void Select(EditorApplication& app, ResourceId document, ResourceId object)
	{
		app.selectionContext.SelectGameObject(document, object);
	}

	static nlohmann::json Input(EditorApplication& app)
	{
		const auto& io = ImGui::GetIO();
		const auto* navigation = ImGui::GetCurrentContext()->NavWindow;
		return {{"gameFocused", app.viewportFocused},        {"runtimeAccepted", app.runtimeGuiAccepted},
		        {"keyboardCapture", io.WantCaptureKeyboard}, {"textCapture", io.WantTextInput},
		        {"activeId", ImGui::GetActiveID()},          {"navigation", navigation ? navigation->Name : ""}};
	}
};

struct SceneViewPanelTestAccess
{
	static void Configure(SceneViewPanel& panel)
	{
		panel.gameSizeMode = GameViewSizeMode::FixedResolution;
		panel.gamePresetWidth = 1024;
		panel.gamePresetHeight = 720;
		panel.gameFit = true;
	}

	static std::shared_ptr<Rendering::RenderTextureResource> Output(SceneViewPanel& panel)
	{
		return panel.gameTarget.output;
	}
};
} // namespace Ursa::Editor

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
	return std::move(value.value);
}

void Done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(value.error.c_str());
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

class SuiteEditor final : public EditorApplication
{
public:
	SuiteEditor(std::filesystem::path replayFile, std::filesystem::path output)
	    : replayFile(std::move(replayFile)), output(std::move(output))
	{
	}

	bool Verified() const
	{
		return runs == 2;
	}

protected:
	Result<void> OnStart() override
	{
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(540);
		return EditorApplication::OnStart();
	}

	nlohmann::json Graph(Scene& graph)
	{
		return Take(EncodeSceneDocument(Documents()->Current()->Path().ToString(), Documents()->Current()->Id(), graph,
		                                Project().components));
	}

	nlohmann::json State()
	{
		auto graph = Graph(*RunningScene());
		auto& guiState = EditorPlayToolbarTest::Play(*this).GuiSession();
		nlohmann::json state = {{"objects", graph["gameObjects"].size()},
		                        {"editorInput", EditorPlayToolbarTest::Input(*this)},
		                        {"behaviours", nlohmann::json::object()},
		                        {"hasTextFocus", guiState.HasTextFocus()},
		                        {"hasPointerCapture", guiState.HasPointerCapture()}};
		for (const auto& object : graph["gameObjects"])
		{
			for (const auto& component : object["components"])
			{
				if (component["type"] != "ursa.ScriptBehaviour")
				{
					continue;
				}
				const auto& fields = component["data"]["scriptPayload"]["Fields"];
				state["behaviours"][component["data"]["typeName"].get<std::string>()] = fields;
				if (fields.contains("activeScenePath"))
				{
					const auto path = fields["activeScenePath"].get<std::string>();
					Expect(path == "Tests/Menu.ursascene" || path == "Tests/OnGUI/OnGUI.ursascene",
					       "Editor did not expose the active runtime Scene identity");
					const bool isMenu = path == "Tests/Menu.ursascene";
					state["scene"] = isMenu ? "Menu" : "OnGUI";
					state["buildIndex"] = isMenu ? 0 : 1;
				}
			}
		}
		return state;
	}

	Result<void> OnGui() override
	{
		EditorPlayToolbarTest::DeferSourcePolling(*this);
		if (std::chrono::steady_clock::now() > deadline)
		{
			return Result<void>::Failure("Editor DemoSuite timed out: " + ScriptStatus() + " / " + InspectorStatus());
		}
		if (replaying && replayFrame && replayFrame != verifiedFrame)
		{
			verifiedFrame = replayFrame;
			const auto state = State();
			Done(replay->Verify(replayFrame, state));
			const auto field = [&](const char* name)
			{ return state["behaviours"]["Ursa.Demo.Tests.OnGUITest, Ursa.Game"][name].get<int>(); };
			if (replayFrame == 4)
			{
				pausedUpdates = field("updates");
				EditorPlayToolbarTest::Play(*this).Pause();
			}
			if (replayFrame == 7)
			{
				Expect(field("updates") == pausedUpdates, "Paused GUI clicks advanced simulation");
				EditorPlayToolbarTest::Play(*this).Step();
			}
			if (replayFrame == 8)
			{
				Expect(field("updates") == pausedUpdates + 1, "Step did not advance exactly one Update");
			}
			if (replayFrame == 10)
			{
				EditorPlayToolbarTest::Play(*this).Resume();
			}
			if (replayFrame == 19)
			{
				auto target = SceneViewPanelTestAccess::Output(EditorPlayToolbarTest::Panel(*this));
				Expect(bool(target), "Editor has no rendered Game target");
				auto pixels = Take(Rendering::ReadRenderTexture(*device, *target));
				const Size red = (318 * 1024 + 470) * 4, green = (318 * 1024 + 520) * 4;
				Expect(pixels[red] > pixels[red + 1] + 60 && pixels[green + 1] > pixels[green] + 60,
				       "Editor Game GUI texture pixels differ from Player");
				Rendering::FrameCapture capture;
				Done(capture.Configure(output / ("EditorOnGUI-" + std::to_string(runs) + ".png")));
				Done(capture.Capture(device, target->color, target->colorState));
				Done(capture.Finish());
			}
			if (replayFrame == 44)
			{
				EditorPlayToolbarTest::Play(*this).Pause();
			}
			if (replayFrame == 47)
			{
				Expect(field("updates") == 0, "Paused Scene activation advanced simulation");
			}
			if (replayFrame == 48)
			{
				EditorPlayToolbarTest::Play(*this).Resume();
			}
			if (replayFrame == replay->EndFrame())
			{
				Done(replay->Finish());
				replaying = false;
				stopping = true;
				RequestPlayToggle();
			}
		}
		if (auto* game = ImGui::FindWindowByName("Game"))
		{
			ImGui::FocusWindow(game);
		}
		EditorPlayToolbarTest::Show(*this);
		auto drawn = EditorApplication::OnGui();
		if (!drawn)
		{
			return drawn;
		}
		if (ScriptWorkPending(false) || AssetWorkPending())
		{
			return {};
		}
		if (stopping && !RunningScene())
		{
			auto draft = Documents()->Current()->Payload<SceneDraft>();
			const auto restoredAuthor = Graph(draft->GetScene());
			std::ofstream(output / ("AuthorRestore-" + std::to_string(runs) + ".json")) << nlohmann::json{
			    {"before", authorSnapshot},
			    {"after", restoredAuthor},
			    {"dirty", Documents()->Current()->Dirty()}}.dump(2);
			Expect(Documents()->Current()->Dirty() && restoredAuthor == authorSnapshot,
			       "Stop changed the unsaved author graph or Dirty state");
			Expect(SelectionState().objectId == authorSelection && !SelectionState().entries.empty() &&
			           !SelectionState().entries[0].playSession,
			       "Stop did not restore the original author selection");
			stopping = false;
			++runs;
			if (runs == 2)
			{
				Done(Documents()->RequestClose());
				if (Documents()->PromptPending())
				{
					Done(Documents()->ResolvePrompt(UnsavedChoice::Discard));
				}
				window->RequestClose();
				return {};
			}
			RequestPlayToggle();
			return {};
		}
		if (!authorReady && Documents() && Documents()->Current())
		{
			if (!EditingRenderReady())
			{
				if (!initialCompileRequested && !ScriptWorkPending())
				{
					Done(CompileScripts());
					initialCompileRequested = true;
				}
				return {};
			}
			auto draft = Documents()->Current()->Payload<SceneDraft>();
			Expect(bool(draft), "Default Editor startup did not open Menu Scene");
			auto root = draft->GetScene().RootGameObjects()[0];
			Done(root->SetTag("Unsaved author"));
			Documents()->Current()->MarkDirty();
			authorSelection = root->Id();
			EditorPlayToolbarTest::Select(*this, Documents()->Current()->Id(), authorSelection);
			authorSnapshot = Graph(draft->GetScene());
			authorReady = true;
			RequestPlayToggle();
			return {};
		}
		if (RunningScene() && !replaying)
		{
			replay = Take(FrameReplay::Open(replayFile));
			replayFrame = 0;
			verifiedFrame = 0;
			replaying = true;
		}
		if (replaying)
		{
			auto& panel = EditorPlayToolbarTest::Panel(*this);
			SceneViewPanelTestAccess::Configure(panel);
			const auto layout = panel.GameLayout();
			if (!layout.visible || layout.targetPixels.width != 1024 || layout.targetPixels.height != 720)
			{
				return {};
			}
			auto source = Gui().InputWindowForViewport(panel.GameViewport());
			if (!source)
			{
				source = window;
			}
			const auto position = source->Position();
			const bool desktop = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
			Done(replay->Feed(++replayFrame, *source,
			                  [&](Float64 x, Float64 y)
			                  {
				                  return std::make_pair(layout.displayRect.x + x * layout.displayRect.width / 1024 -
				                                            (desktop ? position.first : 0),
				                                        layout.displayRect.y + y * layout.displayRect.height / 720 -
				                                            (desktop ? position.second : 0));
			                  }));
		}
		return {};
	}

private:
	std::filesystem::path replayFile, output;
	std::unique_ptr<FrameReplay> replay;
	nlohmann::json authorSnapshot;
	ResourceId authorSelection;
	std::chrono::steady_clock::time_point deadline;
	UInt32 replayFrame = 0;
	UInt32 verifiedFrame = 0;
	int pausedUpdates = 0, runs = 0;
	bool authorReady = false, replaying = false, stopping = false, initialCompileRequested = false;
};
} // namespace

int main(int count, char** values)
{
	try
	{
		if (count == 5 && std::string(values[1]) == "--fault-scene-pak")
		{
			const auto input = std::filesystem::absolute(std::filesystem::u8path(values[2]));
			const auto output = std::filesystem::absolute(std::filesystem::u8path(values[3]));
			Expect(input != output, "Fault fixture must not overwrite its source Pak");
			auto archive = Take(PakArchive::Open(input));
			VirtualFileSystem files;
			Done(archive->Mount(files));
			Vector<PakEntryData> entries;
			Size corrupted = 0;
			for (const auto& scheme : archive->Schemes())
			{
				for (const auto& entry : Take(files.List(scheme)))
				{
					if (entry.path.ToString() == values[4])
					{
						++corrupted;
						entries.push_back({entry.path, {'{', '}'}});
						continue;
					}
					entries.push_back({entry.path, Take(files.Read(entry.path))});
				}
			}
			Expect(corrupted == 1, "Fault Pak did not corrupt exactly its requested Scene target");
			const auto bytes = Take(PakBuilder::Encode(std::move(entries)));
			std::ofstream file(output, std::ios::binary);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			Expect(bool(file), "Could not save fault Pak fixture");
			return 0;
		}
		if (count != 4)
		{
			throw std::runtime_error("Usage: UrsaDemoSuiteEditorTests <project> <Replay.json> <output>");
		}
		const auto output = std::filesystem::absolute(std::filesystem::u8path(values[3]));
		std::filesystem::create_directories(output);
		SuiteEditor editor(std::filesystem::absolute(std::filesystem::u8path(values[2])), output);
		Done(editor.OpenProject(std::filesystem::absolute(std::filesystem::u8path(values[1]))));
		Done(editor.Run(1600, 1000, "Ursa DemoSuite Editor", false));
		Expect(editor.Verified(), "Editor DemoSuite did not complete both Runs");
		std::cout << "Two Editor Runs passed real Menu button navigation, Pause/Step, reset, Game pixels and Stop "
		             "author restoration. Input was injected through Window.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
