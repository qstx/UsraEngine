#include <Assets/ProjectBrowserPanel.hpp>
#include <Assets/ExternalProjectImport.hpp>
#include <Core/StringInterop.hpp>
#include <Inspector/SceneInspectorPanel.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <imgui_internal.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#endif

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
	Done(RegisterBuiltinComponentEditors(editors));
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
	auto* sourceCamera = Take(sceneRoot->AddComponent<CameraComponent>());
	sourceCamera->verticalFovRadians = 1.1f;
	sourceCamera->orthographic = true;
	sourceCamera->orthographicSize = 7.f;
	sourceCamera->nearPlane = 0.25f;
	sourceCamera->farPlane = 250.f;
	ComponentId cameraId;
	GameObject::Ptr targetObject;
	CameraComponent* targetCamera = nullptr;
	if (kind == ResourceKind::Scene)
	{
		cameraId = Take(draft->GetScene().ComponentsOf(sceneRoot)).back();
		targetObject = Take(draft->GetScene().CreateGameObject("Camera Target"));
		targetCamera = Take(targetObject->AddComponent<CameraComponent>());
		targetCamera->verticalFovRadians = 0.9f;
		targetCamera->orthographic = false;
		targetCamera->orthographicSize = 4.f;
		targetCamera->nearPlane = 0.8f;
		targetCamera->farPlane = 800.f;
	}
	else
	{
		targetObject = sceneRoot;
		targetCamera = Take(targetObject->AddComponent<CameraComponent>());
		targetCamera->verticalFovRadians = 0.9f;
		targetCamera->orthographic = true;
		targetCamera->orthographicSize = 4.f;
		targetCamera->nearPlane = 0.8f;
		targetCamera->farPlane = 800.f;
	}
	Done(documents->RequestOpen(std::move(document)));
	EditorSelectionContext selection;
	EditorHierarchy hierarchy;
	ProjectBrowserModel model;
	Done(model.Refresh(*workspace));
	ProjectBrowserPanel project;
	const auto tagLayerSettings = ProjectTagLayerSettings::Defaults();
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
		Take(DrawSceneInspector(*documents->Current(), hierarchy, *documents, *workspace, tagLayerSettings, registry,
		                        editors, diagnostic, nullptr, &hierarchyOpen, &inspectorOpen, true, &selection));
		PrepareWindow("Project", {0, 450}, {projectWidth, 400});
		Done(project.Draw(*workspace, model, selection, hierarchy, projectOpen, false, diagnostic, {}));
		ImGui::Render();
	};
	frame();
	frame();
	auto& io = ImGui::GetIO();
	const auto row = ImGui::FindWindowByName("Hierarchy")->DC.CursorStartPos;
	const float toolbarAndGroupHeight = ImGui::GetFrameHeightWithSpacing() * (kind == ResourceKind::Scene ? 2.f : 1.f) +
	                                    (kind == ResourceKind::Scene ? ImGui::GetTextLineHeight() * 2.5f : 0.f);
	io.AddMousePosEvent(row.x + 100.f, row.y + toolbarAndGroupHeight + ImGui::GetTextLineHeight() * 0.5f);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	frame();
	Expect(selection.entries.empty(), "A press selected a hierarchy row before distinguishing a drag");
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	frame();
	Expect(selection.kind == EditorSelectionKind::GameObject && selection.objectId == sceneRoot->Id() &&
	           hierarchy.SelectedId() == sceneRoot->Id() && inspectedObject == sceneRoot->Id(),
	       "Clicking the actual Hierarchy row did not draw that object's Inspector");
	if (kind == ResourceKind::Scene)
	{
		const auto invokeComponentMenuItem = [&](ComponentId component, const char* label)
		{
			const ResourceId persistentId = Take(draft->GetScene().ComponentPersistentId(component));
			auto* window = ImGui::FindWindowByName("Inspector");
			Expect(window != nullptr, "The selected object's Inspector window is missing");
			const ImGuiID idStack = ImHashStr(persistentId.ToString().c_str(), 0, window->ID);
			ImGui::ActivateItemByID(ImHashStr("ComponentActions", 0, idStack));
			frame();
			ImGuiWindow* popup = nullptr;
			for (ImGuiWindow* candidate : ImGui::GetCurrentContext()->Windows)
			{
				if (candidate->Active && (candidate->Flags & ImGuiWindowFlags_Popup) != 0)
				{
					popup = candidate;
				}
			}
			Expect(popup != nullptr, "Activating Component actions did not open the component menu");
			ImGui::ActivateItemByID(popup->GetID(label));
			frame();
		};
		invokeComponentMenuItem(cameraId, "Copy Component");
		const ResourceId targetObjectId = Take(draft->GetScene().PersistentId(targetObject));
		selection.SelectGameObject(documents->Current()->Id(), targetObjectId);
		frame();
		const ComponentId targetCameraId = Take(draft->GetScene().ComponentsOf(targetObject)).back();
		invokeComponentMenuItem(targetCameraId, "Paste Component Values");
		if (targetCamera->verticalFovRadians != sourceCamera->verticalFovRadians || !targetCamera->orthographic ||
		    targetCamera->orthographicSize != sourceCamera->orthographicSize ||
		    targetCamera->nearPlane != sourceCamera->nearPlane || targetCamera->farPlane != sourceCamera->farPlane ||
		    !documents->Current()->Dirty() || !diagnostic.empty())
		{
			throw std::runtime_error(
			    "Pasting copied camera values failed: source=" + std::to_string(sourceCamera->verticalFovRadians) +
			    "," + std::to_string(sourceCamera->orthographicSize) + "," + std::to_string(sourceCamera->nearPlane) +
			    "," + std::to_string(sourceCamera->farPlane) +
			    "; target=" + std::to_string(targetCamera->verticalFovRadians) + "," +
			    std::to_string(targetCamera->orthographic) + "," + std::to_string(targetCamera->orthographicSize) +
			    "," + std::to_string(targetCamera->nearPlane) + "," + std::to_string(targetCamera->farPlane) +
			    "; dirty=" + std::to_string(documents->Current()->Dirty()) + "; diagnostic=" + ToStdString(diagnostic));
		}
		invokeComponentMenuItem(targetCameraId, "Paste Component As New");
		auto targetComponents = Take(draft->GetScene().ComponentsOf(targetObject));
		Expect(targetComponents.size() == 2, "Paste Component As New did not add a second Camera component");
		const ComponentId pastedCameraId = targetComponents.back();
		auto* pastedCamera = dynamic_cast<CameraComponent*>(draft->GetScene().GetComponent(pastedCameraId));
		Expect(pastedCamera && pastedCamera->verticalFovRadians == sourceCamera->verticalFovRadians &&
		           pastedCamera->orthographic && pastedCamera->nearPlane == sourceCamera->nearPlane &&
		           pastedCamera->farPlane == sourceCamera->farPlane && diagnostic.empty(),
		       "Paste Component As New did not create a component with copied editable values");
		invokeComponentMenuItem(pastedCameraId, "Move Up");
		targetComponents = Take(draft->GetScene().ComponentsOf(targetObject));
		Expect(targetComponents.front() == pastedCameraId,
		       "The component Move Up menu item did not reorder the selected component");
		invokeComponentMenuItem(pastedCameraId, "Move Down");
		targetComponents = Take(draft->GetScene().ComponentsOf(targetObject));
		Expect(targetComponents.front() == targetCameraId && targetComponents.back() == pastedCameraId,
		       "The component Move Down menu item did not reorder the selected component");
		invokeComponentMenuItem(pastedCameraId, "Reset");
		Expect(pastedCamera->verticalFovRadians == 0.78539816f && !pastedCamera->orthographic &&
		           pastedCamera->orthographicSize == 5.f && pastedCamera->nearPlane == 0.1f &&
		           pastedCamera->farPlane == 100.f && diagnostic.empty(),
		       "The component Reset menu item did not restore serialized defaults");
		invokeComponentMenuItem(targetCameraId, "Remove Component");
		Expect(draft->GetScene().GetComponent(targetCameraId) == nullptr && documents->Current()->Dirty() &&
		           diagnostic.empty(),
		       "Removing a component from its Inspector menu did not update the Scene document");
	}
	else
	{
		const auto invokePrefabMenuItem = [&](Component* component, const char* label)
		{
			const ResourceId persistentId = component->PersistentId();
			Expect(static_cast<bool>(persistentId), "Detached Prefab component has no persistent ID");
			auto* window = ImGui::FindWindowByName("Inspector");
			Expect(window != nullptr, "The selected Prefab Inspector window is missing");
			const ImGuiID idStack = ImHashStr(persistentId.ToString().c_str(), 0, window->ID);
			ImGui::ActivateItemByID(ImHashStr("ComponentActions", 0, idStack));
			frame();
			ImGuiWindow* popup = nullptr;
			for (ImGuiWindow* candidate : ImGui::GetCurrentContext()->Windows)
			{
				if (candidate->Active && (candidate->Flags & ImGuiWindowFlags_Popup) != 0)
				{
					popup = candidate;
				}
			}
			Expect(popup != nullptr, "Activating Prefab Component actions did not open its menu");
			ImGui::ActivateItemByID(popup->GetID(label));
			frame();
		};
		invokePrefabMenuItem(targetCamera, "Move Up");
		auto prefabComponents = targetObject->Components();
		Expect(prefabComponents.size() == 2 && prefabComponents.front() == targetCamera,
		       "The Prefab Inspector Move Up menu item did not reorder its detached component");
		invokePrefabMenuItem(targetCamera, "Move Down");
		prefabComponents = targetObject->Components();
		Expect(prefabComponents.back() == targetCamera,
		       "The Prefab Inspector Move Down menu item did not restore component order");
		invokePrefabMenuItem(targetCamera, "Reset");
		Expect(targetCamera->verticalFovRadians == 0.78539816f && !targetCamera->orthographic &&
		           targetCamera->orthographicSize == 5.f && targetCamera->nearPlane == 0.1f &&
		           targetCamera->farPlane == 100.f && diagnostic.empty(),
		       "The Prefab Inspector Reset menu item did not restore component defaults");
		Done(targetObject->RemoveComponent(targetCamera));
		frame();
		Expect(targetObject->FindComponents<CameraComponent>().size() == 1 && documents->Current()->Dirty() &&
		           diagnostic.empty(),
		       "Removing a detached Prefab component did not update the Prefab document");
	}
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
	const ResourceId expectedSelectionId = kind == ResourceKind::Scene ? targetObject->Id() : sceneRoot->Id();
	Expect(selection.revision == selectionRevision && selection.objectId == expectedSelectionId,
	       "Resizing Project changed the Inspector selection");
	const String savedLayout = ImGui::SaveIniSettingsToMemory();
	Expect(savedLayout.find("ProjectFolderTree") != String::npos,
	       "The resizable Project tree was not included in saved workspace settings");
	projectWidth = 300.f;
	frame();
	Expect(ProjectChild("ProjectFolderContents")->Size.x >= 120.f && ProjectChild("ProjectFolderTree")->Size.x >= 90.f,
	       "Shrinking Project hid one of its panes behind the resized folder tree");
}

void TestExternalDrop(const std::filesystem::path& root)
{
	const auto assets = root / "Assets";
	const auto external = root / "outside";
	std::filesystem::create_directories(assets);
	std::filesystem::create_directories(external);
	std::ofstream(external / "first.txt") << "first";
	std::ofstream(external / "good.txt") << "good";
	std::ofstream(external / "other.txt") << "other";
	std::ofstream(external / "locked.txt") << "locked";
	auto workspace = Take(ProjectWorkspace::Open(root));
	Done(workspace->Database()->CreateFolder(Take(VirtualPath::Parse("prj:Target"))));
	Done(workspace->Refresh());
	ProjectBrowserModel model;
	Done(model.Refresh(*workspace));
	Done(model.NavigateTo("Target"));
	EditorSelectionContext selection;
	EditorHierarchy hierarchy;
	ProjectBrowserPanel panel;
	ProjectBrowserActions actions;
	Vector<ProjectBrowserFileDrop> pending;
	int requests = 0;
	int calls = 0;
	bool open = true;
	bool readOnly = false;
	String diagnostic;
	actions.takeExternalFileDrops = [&]() { return std::exchange(pending, {}); };
	actions.importExternalFiles = [&](const Vector<String>& paths, const String& destination)
	{
		++calls;
		return ImportExternalProjectFiles(*workspace, model, selection, paths, destination, [&] { ++requests; });
	};
	const auto frame = [&]
	{
		ImGui::NewFrame();
		PrepareWindow("Project", {0, 450}, {900, 400});
		Done(panel.Draw(*workspace, model, selection, hierarchy, open, readOnly, diagnostic, actions));
		ImGui::Render();
	};
	frame();
	frame();
	const ImGuiWindow* contents = ProjectChild("ProjectFolderContents");
	const ImVec2 viewport = ImGui::GetMainViewport()->Pos;
	const Float64 x = contents->Pos.x + contents->Size.x * 0.5f - viewport.x;
	const Float64 y = contents->Pos.y + contents->Size.y * 0.7f - viewport.y;
	const auto path = [&](const char* file) { return ToUrsaString((external / file).u8string()); };
	pending.push_back({x, y, {path("first.txt")}});
	frame();
	Expect(calls == 1 && requests == 1 && model.Find("Target/first.txt") && diagnostic.empty(),
	       "Project content drop did not import and refresh the current folder");
	pending.push_back({-100, -100, {path("other.txt")}});
	frame();
	Expect(calls == 1 && requests == 1, "A file drop outside Project was imported");
	readOnly = true;
	pending.push_back({x, y, {path("other.txt")}});
	frame();
	Expect(calls == 1 && requests == 1, "A read-only Project accepted an external file drop");
	readOnly = false;
#ifdef _WIN32
	const HANDLE locked = CreateFileW((external / "locked.txt").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
	                                  FILE_ATTRIBUTE_NORMAL, nullptr);
	Expect(locked != INVALID_HANDLE_VALUE, "Cannot lock the external drop test file");
	pending.push_back({x, y, {path("good.txt"), path("locked.txt")}});
	frame();
	Expect(calls == 2 && requests == 2 && model.Find("Target/good.txt") &&
	           diagnostic.find("locked.txt") != String::npos &&
	           diagnostic.find("Project was refreshed") != String::npos,
	       "A partial drop did not refresh the successful copy or report the failed path");
	std::ofstream(assets / "invalid.txt.meta") << "{}";
	diagnostic.clear();
	pending.push_back({x, y, {path("other.txt"), path("locked.txt")}});
	frame();
	CloseHandle(locked);
	Expect(calls == 3 && requests == 3 && std::filesystem::exists(assets / "Target" / "other.txt") &&
	           diagnostic.find("locked.txt") != String::npos && diagnostic.find("refresh failed") != String::npos,
	       "A partial drop did not report both its copy failure and refresh failure");
	std::filesystem::remove(assets / "invalid.txt.meta");
	Done(workspace->Refresh());
	Done(model.Refresh(*workspace));
	pending.push_back({x, y, {path("good.txt"), path("locked.txt")}});
	frame();
	Expect(calls == 4 && requests == 4 && model.Find("Target/good (1).txt") && model.Find("Target/locked.txt") &&
	           diagnostic.empty(),
	       "Retry did not keep the previous file, import the locked file and refresh Project");
	pending.push_back({x, y, {path("missing-a.txt")}});
	pending.push_back({x, y, {path("first.txt")}});
	pending.push_back({x, y, {path("missing-b.txt")}});
	frame();
	Expect(calls == 7 && requests == 5 && model.Find("Target/first (1).txt") &&
	           diagnostic.find("missing-a.txt") != String::npos && diagnostic.find("missing-b.txt") != String::npos,
	       "Multiple drops lost failures when another drop succeeded");
#endif
}

void TestProjectKeyboardRename(const std::filesystem::path& root)
{
	const auto assets = root / "Assets";
	std::filesystem::create_directories(assets);
	std::ofstream(assets / "before.txt") << "rename through Project keyboard focus";
	auto workspace = Take(ProjectWorkspace::Open(root));
	ProjectBrowserModel model;
	Done(model.Refresh(*workspace));
	EditorSelectionContext selection;
	EditorHierarchy hierarchy;
	ProjectBrowserPanel panel;
	ProjectBrowserActions actions;
	Int32 renameCalls = 0;
	String lastRenameDestination;
	actions.renameAsset = [&](const VirtualPath& source, const VirtualPath& destination)
	{
		++renameCalls;
		lastRenameDestination = destination.relative;
		return workspace->Database()->Move(source, destination);
	};
	bool open = true;
	String diagnostic;
	const auto frame = [&]
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({900, 700}, ImGuiCond_Always);
		ImGui::Begin("Project");
		Done(panel.Draw(*workspace, model, selection, hierarchy, open, false, diagnostic, actions));
		ImGui::End();
		ImGui::Render();
	};
	frame();
	frame();
	auto& io = ImGui::GetIO();
	auto* contents = ProjectChild("ProjectFolderContents");
	const ImVec2 assetRowCenter{contents->Pos.x + 50.f, contents->DC.CursorPos.y - 8.f};
	io.AddMousePosEvent(assetRowCenter.x, assetRowCenter.y);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	frame();
	if (selection.entries.size() != 1 || !selection.PrimaryEntry() ||
	    selection.PrimaryEntry()->assetPath.relative.find("before.txt") == String::npos)
	{
		String message = "Clicking the Project source row did not select it; count=" +
		                 ToUrsaString(std::to_string(selection.entries.size())) + ", current=" + model.CurrentFolder() +
		                 ", assets=";
		for (const auto& asset : model.ChildAssets(model.CurrentFolder()))
		{
			message += asset.path.relative + ";";
		}
		if (selection.PrimaryEntry())
		{
			message += ", selected=" + selection.PrimaryEntry()->assetPath.relative;
		}
		throw std::runtime_error(ToStdString(message));
	}

	io.AddKeyEvent(ImGuiKey_F2, true);
	frame();
	io.AddKeyEvent(ImGuiKey_F2, false);
	frame();
	if (ImGui::GetActiveID() == 0)
	{
		throw std::runtime_error(
		    "F2 did not focus the inline Project rename field; active=" + std::to_string(ImGui::GetActiveID()) +
		    ", focus=" + std::to_string(ImGui::GetFocusID()) +
		    ", text=" + std::to_string(ImGui::GetIO().WantTextInput) + ", diagnostic=" + ToStdString(diagnostic));
	}
	io.AddKeyEvent(ImGuiKey_Escape, true);
	frame();
	io.AddKeyEvent(ImGuiKey_Escape, false);
	frame();
	Expect(std::filesystem::exists(assets / "before.txt") && !std::filesystem::exists(assets / "cancelled.txt"),
	       "Escape committed or changed the Project rename target");

	io.AddMousePosEvent(assetRowCenter.x, assetRowCenter.y);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	frame();
	io.AddKeyEvent(ImGuiKey_F2, true);
	frame();
	io.AddKeyEvent(ImGuiKey_F2, false);
	frame();
	io.AddInputCharactersUTF8("after");
	frame();
	io.AddKeyEvent(ImGuiKey_Enter, true);
	frame();
	io.AddKeyEvent(ImGuiKey_Enter, false);
	frame();
	if (std::filesystem::exists(assets / "before.txt") || !std::filesystem::exists(assets / "after.txt") ||
	    !diagnostic.empty() || renameCalls != 1 || lastRenameDestination != "after.txt")
	{
		throw std::runtime_error("F2 rename confirmation did not move the source asset on disk; before=" +
		                         std::to_string(std::filesystem::exists(assets / "before.txt")) +
		                         ", after=" + std::to_string(std::filesystem::exists(assets / "after.txt")) +
		                         ", diagnostic=" + ToStdString(diagnostic) + ", calls=" + std::to_string(renameCalls) +
		                         ", dest=" + ToStdString(lastRenameDestination));
	}
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
		TestProjectKeyboardRename(root / "ProjectKeyboardRename");
		TestExternalDrop(root / "ExternalDrop");
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
