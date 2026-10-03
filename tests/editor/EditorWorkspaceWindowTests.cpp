#include <application/EditorApplication.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{
class WorkspaceEditor final : public Ursa::Editor::EditorApplication
{
protected:
	Ursa::Result<void> OnGui() override
	{
		auto drawn = EditorApplication::OnGui();
		if (!drawn)
		{
			return drawn;
		}
		if (++frames < 2)
		{
			return {};
		}
		auto* project = ImGui::FindWindowByName("Project");
		auto* hierarchy = ImGui::FindWindowByName("Hierarchy");
		auto* viewport = ImGui::FindWindowByName("Viewport");
		auto* inspector = ImGui::FindWindowByName("Inspector");
		if (!project || !hierarchy || !viewport || !inspector)
		{
			return Ursa::Result<void>::Failure("Editor workspace is missing a panel");
		}
		if (!project->DockNode || !hierarchy->DockNode || !viewport->DockNode || !inspector->DockNode)
		{
			return Ursa::Result<void>::Failure("Editor workspace panels are not docked");
		}
		if (!(hierarchy->Pos.x < viewport->Pos.x && viewport->Pos.x < inspector->Pos.x &&
		      project->Pos.y > viewport->Pos.y))
		{
			return Ursa::Result<void>::Failure("Editor workspace panel arrangement is incorrect");
		}
		checked = true;
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
};
} // namespace

int main()
{
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto root =
	    std::filesystem::temp_directory_path() / ("UrsaEditorWorkspaceWindowTests-" + std::to_string(unique));
	std::error_code error;
	std::filesystem::create_directories(root / "assets", error);
	if (error)
	{
		std::cerr << error.message() << '\n';
		return 1;
	}
	WorkspaceEditor editor;
	auto opened = editor.OpenProject(root);
	auto ran = opened ? editor.Run(1280, 720, "Editor Workspace Test", false, 3) : opened;
	std::filesystem::remove_all(root, error);
	if (!ran || !editor.Checked())
	{
		std::cerr << (ran ? "Editor workspace was not checked" : ran.error.c_str()) << '\n';
		return 1;
	}
	return 0;
}
