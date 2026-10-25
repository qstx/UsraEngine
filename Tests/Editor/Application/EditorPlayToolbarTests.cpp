#include <Application/EditorApplication.hpp>
#include <Core/StringInterop.hpp>
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;

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
} // namespace

namespace Ursa::Editor
{
struct EditorPlayToolbarTest
{
	static void Run(const std::filesystem::path& root)
	{
		EditorApplication editor;
		editor.project = std::make_unique<EditorProject>();
		Done(RegisterBuiltinComponents(editor.project->components));
		std::filesystem::create_directories(root / "Assets");
		editor.workspace = Take(ProjectWorkspace::Open(root));
		editor.documents = Take(EditorDocuments::Create(*editor.workspace));
		const auto newScene = [&](const char* path)
		{
			return Take(NewSceneDocument(Take(VirtualPath::Parse(path)), ResourceKind::Scene,
			                             editor.project->components, editor.documents->Session()));
		};
		Done(editor.documents->RequestOpen(newScene("prj:Toolbar.ursascene")));
		const auto drawFrame = [&]
		{
			ImGui::NewFrame();
			editor.DrawPlayToolbar(editor.documents->Current());
			ImGui::Render();
			Vector<ImU32> colors;
			for (const auto& vertex : ImGui::FindWindowByName("##EditorToolbar")->DrawList->VtxBuffer)
			{
				colors.push_back(vertex.col);
			}
			return colors;
		};
		drawFrame();
		drawFrame();
		const auto idleColors = drawFrame();
		Expect(!idleColors.empty(), "The Play toolbar did not render");
		const auto runId = ImGui::FindWindowByName("##EditorToolbar")->GetID("Run");
		for (UInt32 scan = 0; scan < 3; ++scan)
		{
			std::promise<Result<String>> checked;
			editor.scriptCheckJob = checked.get_future();
			Expect(drawFrame() == idleColors && drawFrame() == idleColors,
			       "A periodic source scan dimmed the idle Play toolbar");
			checked.set_value(Result<String>::Success("unchanged"));
			Take(editor.scriptCheckJob.get());
			Expect(drawFrame() == idleColors, "Completing a source scan changed the idle Play toolbar colors");
		}
		{
			std::promise<Result<ScriptBuildReport>> compiled;
			editor.scriptBuildJob = compiled.get_future();
			Expect(drawFrame() == idleColors, "Compilation dimmed Play instead of accepting a pending start");
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(!editor.play.Active() && editor.runAfterScriptBuild &&
			           editor.pendingRunDocument == editor.documents->Current()->Id(),
			       "Clicking Play during compilation did not queue the current scene");
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(!editor.play.Active() && !editor.requestRun && !editor.runAfterScriptBuild,
			       "Clicking Play again did not cancel the compilation wait");
			compiled.set_value(Result<ScriptBuildReport>::Failure("Simulated compilation failure"));
			editor.scriptBuildJob.get();
		}
		{
			std::promise<Result<String>> checked;
			editor.scriptCheckJob = checked.get_future();
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(editor.requestRun && !editor.play.Active(), "Play did not wait for a pending source scan");
			drawFrame();
			Expect(editor.requestRun && !editor.play.Active(), "Waiting for a source scan lost the Play request");
			checked.set_value(Result<String>::Success("unchanged"));
			Take(editor.scriptCheckJob.get());
			drawFrame();
			Expect(editor.play.Active() && !editor.requestRun, "Play did not start after the source scan finished");
			const auto* runningScene = editor.play.GetScene();
			drawFrame();
			Expect(editor.play.GetScene() == runningScene, "A queued Play click was applied more than once");
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(!editor.play.Active(), "Stop did not end the queued Play session");
		}
		{
			std::promise<Result<String>> checked;
			editor.scriptCheckJob = checked.get_future();
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(editor.requestRun, "The cancellation test did not queue Play");
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(!editor.requestRun, "Clicking Play again did not cancel the source-scan wait");
			checked.set_value(Result<String>::Success("unchanged"));
			Take(editor.scriptCheckJob.get());
			drawFrame();
			Expect(!editor.play.Active(), "Finishing a source scan started a cancelled Play request");
		}
		{
			std::promise<Result<String>> checked;
			editor.scriptCheckJob = checked.get_future();
			ImGui::ActivateItemByID(runId);
			drawFrame();
			Expect(editor.requestRun, "The document-switch test did not queue Play");
			Done(editor.documents->RequestOpen(newScene("prj:Other.ursascene")));
			if (editor.documents->PromptPending())
			{
				Done(editor.documents->ResolvePrompt(UnsavedChoice::Discard));
			}
			checked.set_value(Result<String>::Success("unchanged"));
			Take(editor.scriptCheckJob.get());
			drawFrame();
			Expect(!editor.requestRun && !editor.play.Active(),
			       "Switching documents started Play in a different scene");
		}
	}
};
} // namespace Ursa::Editor

int main()
{
	const auto root = std::filesystem::current_path() /
	                  ("editor_play_toolbar_" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
	auto* context = ImGui::CreateContext();
	try
	{
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = {800, 600};
		io.DeltaTime = 1.f / 60.f;
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		Ursa::Editor::EditorPlayToolbarTest::Run(root);
		ImGui::DestroyContext(context);
		std::filesystem::remove_all(root);
		std::cout << "Play toolbar source-scan and queued-start tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		ImGui::DestroyContext(context);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
