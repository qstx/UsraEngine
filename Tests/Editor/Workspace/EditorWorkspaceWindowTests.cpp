#include <Application/EditorApplication.hpp>
#include <Core/Memory/Memory.hpp>
#include <Core/Memory/LinearAllocator.hpp>
#include <Core/StringInterop.hpp>
#include <Ui/EditorIcons.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/PakBuilder.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{
class WorkspaceEditor final : public Ursa::Editor::EditorApplication
{
public:
	explicit WorkspaceEditor(bool restore = false) : restoreLayout(restore)
	{
	}

protected:
	Ursa::Result<void> OnFrame(Ursa::Float32) override
	{
		const auto tag = Ursa::Memory::RegisterTag("EditorFrameScratchTest");
		if (Ursa::Memory::Snapshot().categories[static_cast<Ursa::Size>(tag)].liveAllocations != 0)
		{
			return Ursa::Result<void>::Failure("Previous Editor frame retained scratch allocation");
		}
		Ursa::Memory::ThreadTemporaryAllocator().Allocate(128, 64, tag);
		return {};
	}

	Ursa::Result<void> OnGui() override
	{
		auto drawn = EditorApplication::OnGui();
		if (!drawn)
		{
			return drawn;
		}
		const auto expectedPrefix =
		    Ursa::PathToUrsaString(Workspace()->ProjectRoot().filename()) + " - Untitled - Editor Workspace Test (";
		if (window->Title().find(expectedPrefix) != 0 || window->Title().back() != ')')
		{
			return Ursa::Result<void>::Failure("Editor window title is missing project, document or version");
		}
		if (++frames < 2)
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
		if (!project->DockNode || !hierarchy->DockNode || !scene->DockNode || !game->DockNode || !console->DockNode ||
		    !inspector->DockNode)
		{
			return Ursa::Result<void>::Failure("Editor workspace panels are not docked");
		}
		if (!Ursa::Editor::Icons::Available())
		{
			return Ursa::Result<void>::Failure("Workspace icon atlas is unavailable");
		}
		if (restoreLayout || frames > 2)
		{
			if (inspector->DockNode != project->DockNode)
			{
				return Ursa::Result<void>::Failure("Icon UI did not retain the customized Inspector dock");
			}
			checked = true;
			ImGui::GetIO().WantSaveIniSettings = true;
			return {};
		}
		if (!(hierarchy->Pos.x < scene->Pos.x && scene->Pos.x < inspector->Pos.x && project->Pos.y > scene->Pos.y &&
		      console->DockNode == project->DockNode && game->DockNode == scene->DockNode))
		{
			return Ursa::Result<void>::Failure("Editor workspace panel arrangement is incorrect");
		}
		ImGui::DockBuilderDockWindow("Inspector", project->DockNode->ID);
		ImGui::GetIO().WantSaveIniSettings = true;
		return {};
	}

public:
	bool Checked() const
	{
		return checked;
	}

private:
	unsigned frames = 0;
	bool checked = false;
	bool restoreLayout = false;
};
} // namespace

int main()
{
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root =
	    std::filesystem::temp_directory_path() / ("UrsaEditorWorkspaceWindowTests-" + std::to_string(unique));
	std::error_code error;
	std::filesystem::create_directories(root / "Assets", error);
	if (error)
	{
		std::cerr << error.message() << '\n';
		return 1;
	}
	Ursa::Result<void> ran;
	bool checked = false;
	{
		WorkspaceEditor editor;
		auto opened = editor.OpenProject(root);
		if (opened)
		{
			opened = editor.SetFrameCapture(std::filesystem::current_path() / "IconEvidence/Workspace-Custom.png", 4);
		}
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 5) : opened;
		checked = editor.Checked();
	}
	if (ran && checked)
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
		ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 4) : opened;
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
