#include <Assets/ProjectBrowserPanel.hpp>
#include <Core/StringInterop.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <imgui_internal.h>
#include <cstring>
#include <filesystem>
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

void PrepareWindow(const char* name, ImVec2 position, ImVec2 size)
{
	ImGui::SetNextWindowPos(position, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::Begin(name);
	ImGui::End();
}

ImGuiWindow* ProjectChild(const char* name)
{
	for (auto* child : ImGui::FindWindowByName("Project")->DC.ChildWindows)
	{
		if (std::strstr(child->Name, name))
		{
			return child;
		}
	}
	throw std::runtime_error("The Project panel child was not drawn");
}

void TestPanels(const std::filesystem::path& root, ResourceKind kind)
{
	std::filesystem::create_directories(root / "Assets");
	auto workspace = Take(ProjectWorkspace::Open(root));
	auto documents = Take(EditorDocuments::Create(*workspace));
	ComponentRegistry registry;
	Done(RegisterBuiltinComponents(registry));
	ComponentEditors editors;
	ResourceId inspectedObject;
	Done(editors.RegisterEditor("ursa.Camera",
	                            [&](Component&, ComponentInspectorContext& context, InspectorUI&)
	                            {
		                            inspectedObject = context.object->Id();
		                            return Result<bool>::Success(false);
	                            }));
	const auto path = kind == ResourceKind::Prefab ? "prj:Panels.ursaprefab" : "prj:Panels.ursascene";
	auto document = Take(NewSceneDocument(Take(VirtualPath::Parse(path)), kind, registry, documents->Session()));
	auto draft = document->Payload<SceneDraft>();
	auto sceneRoot =
	    kind == ResourceKind::Prefab ? draft->GetPrefabAsset()->Root() : draft->GetScene().RootGameObjects().front();
	Take(sceneRoot->AddComponent<CameraComponent>());
	Done(documents->RequestOpen(std::move(document)));
	EditorSelectionContext selection;
	EditorHierarchy hierarchy;
	ProjectBrowserModel model;
	Done(model.Refresh(*workspace));
	ProjectBrowserPanel project;
	bool hierarchyOpen = true;
	bool inspectorOpen = true;
	bool projectOpen = true;
	String diagnostic;
	float projectWidth = 1000.f;
	const auto frame = [&]
	{
		ImGui::NewFrame();
		PrepareWindow("Hierarchy", {0, 0}, {300, 400});
		PrepareWindow("Inspector", {320, 0}, {680, 420});
		Take(DrawSceneInspector(*documents->Current(), hierarchy, *documents, *workspace, registry, editors, diagnostic,
		                        nullptr, &hierarchyOpen, &inspectorOpen, true, &selection));
		PrepareWindow("Project", {0, 450}, {projectWidth, 400});
		Done(project.Draw(*workspace, model, selection, hierarchy, projectOpen, false, diagnostic, {}));
		ImGui::Render();
	};
	frame();
	frame();
	auto& io = ImGui::GetIO();
	const auto row = ImGui::FindWindowByName("Hierarchy")->DC.CursorStartPos;
	io.AddMousePosEvent(row.x + 100.f, row.y + ImGui::GetTextLineHeight() * 0.5f);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	frame();
	Expect(selection.entries.empty(), "A press selected a hierarchy row before distinguishing a drag");
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	frame();
	Expect(selection.kind == EditorSelectionKind::GameObject && selection.objectId == sceneRoot->Id() &&
	           hierarchy.SelectedId() == sceneRoot->Id() && inspectedObject == sceneRoot->Id(),
	       "Clicking the actual Hierarchy row did not draw that object's Inspector");
	const auto initialTreeWidth = ProjectChild("ProjectFolderTree")->Size.x;
	const auto initialContentsWidth = ProjectChild("ProjectFolderContents")->Size.x;
	const auto selectionRevision = selection.revision;
	auto* tree = ProjectChild("ProjectFolderTree");
	const ImVec2 border(tree->Pos.x + tree->Size.x, tree->Pos.y + tree->Size.y * 0.5f);
	io.AddMousePosEvent(border.x, border.y);
	for (UInt32 hoverFrame = 0; hoverFrame < 5; ++hoverFrame)
	{
		frame();
	}
	Expect(ImGui::GetMouseCursor() == ImGuiMouseCursor_ResizeEW, "The Project separator has no resize affordance");
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	frame();
	io.AddMousePosEvent(border.x + 90.f, border.y);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	frame();
	Expect(ProjectChild("ProjectFolderTree")->Size.x > initialTreeWidth + 50.f &&
	           ProjectChild("ProjectFolderContents")->Size.x < initialContentsWidth - 50.f,
	       "Dragging the actual Project separator did not resize both panes");
	Expect(selection.revision == selectionRevision && selection.objectId == sceneRoot->Id(),
	       "Resizing Project changed the Inspector selection");
	const String savedLayout = ImGui::SaveIniSettingsToMemory();
	Expect(savedLayout.find("ProjectFolderTree") != String::npos,
	       "The resizable Project tree was not included in saved workspace settings");
	projectWidth = 300.f;
	frame();
	Expect(ProjectChild("ProjectFolderContents")->Size.x >= 140.f && ProjectChild("ProjectFolderTree")->Size.x >= 90.f,
	       "Shrinking Project hid one of its panes behind the resized folder tree");
}
} // namespace

int main()
{
	const auto root = std::filesystem::current_path() /
	                  ("editor_panels_" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
	auto* context = ImGui::CreateContext();
	try
	{
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = {1200, 1000};
		io.DeltaTime = 1.f / 60.f;
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		TestPanels(root / "Scene", Ursa::ResourceKind::Scene);
		TestPanels(root / "Prefab", Ursa::ResourceKind::Prefab);
		ImGui::DestroyContext(context);
		std::filesystem::remove_all(root);
		std::cout << "Actual Hierarchy/Inspector clicks and Project pane resizing passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		ImGui::DestroyContext(context);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
