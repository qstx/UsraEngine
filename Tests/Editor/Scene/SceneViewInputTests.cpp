#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/SceneViewNavigation.hpp>
#include <Scene/SceneViewPanel.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

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

struct Harness
{
	std::filesystem::path root =
	    std::filesystem::current_path() / ("scene_input_" + ToStdString(ResourceId::Generate().ToString()));
	std::unique_ptr<ProjectWorkspace> workspace;
	std::unique_ptr<EditorDocuments> documents;
	ComponentRegistry registry;
	std::shared_ptr<SceneDraft> draft;
	Rhi::DevicePtr device;
	std::unique_ptr<ImGuiBridge> gui;
	std::unique_ptr<SceneViewPanel> views;
	EditorHierarchy hierarchy;
	PlaySession play;
	Rendering::CameraData camera;
	bool showScene = true;
	bool focusOther = false;
	bool refocus = false;
	bool focused = false;
	String diagnostic;

	Harness()
	{
		std::filesystem::create_directories(root / "Assets");
		workspace = Take(ProjectWorkspace::Open(root));
		documents = Take(EditorDocuments::Create(*workspace));
		Done(RegisterBuiltinComponents(registry));
		auto document = Take(NewSceneDocument(Take(VirtualPath::Parse("prj:Input.ursascene")), ResourceKind::Scene,
		                                      registry, documents->Session()));
		draft = document->Payload<SceneDraft>();
		Done(documents->RequestOpen(std::move(document)));
		Done(documents->SaveCurrent());
		Rhi::DeviceDesc description;
#ifndef NDEBUG
		description.validation = true;
#endif
		device = Take(Rhi::CreateDevice(description));
		gui = Take(ImGuiBridge::Create(device));
		ImGui::GetIO().IniFilename = nullptr;
		views = Take(SceneViewPanel::Create(device, *gui));
		Frame();
		Frame();
	}

	~Harness()
	{
		(void)device->WaitIdle();
		views.reset();
		gui.reset();
		std::filesystem::remove_all(root);
	}

	void Frame()
	{
		Done(gui->BeginFrame(1.f / 60.f, 1100, 800));
		ImGui::SetNextWindowPos({970, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({120, 200}, ImGuiCond_Always);
		ImGui::Begin("Other");
		ImGui::TextUnformatted("Other");
		ImGui::End();
		if (focusOther || refocus)
		{
			ImGui::FocusWindow(ImGui::FindWindowByName(focusOther ? "Other" : "Scene"));
			refocus = false;
		}
		ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({950, 700}, ImGuiCond_Always);
		bool showGame = false;
		Done(views->Draw(*draft, *documents->Current(), hierarchy, play, nullptr, "input-test", showScene, showGame,
		                 focused, diagnostic));
		Done(
		    views->RenderPending(*draft, play,
		                         [&](Scene&, const std::shared_ptr<Rendering::RenderTextureResource>&,
		                             std::optional<Rendering::CameraType>, std::optional<Rendering::CameraData> current)
		                         {
			                         Expect(current.has_value(), "Scene did not expose its rendered camera");
			                         camera = *current;
			                         return Result<void>{};
		                         }));
		Done(gui->EndFrame());
	}

	ImVec2 Point() const
	{
		const auto& rect = views->SceneLayout().displayRect;
		return {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.55f};
	}

	void BeginClick()
	{
		Expect(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "A previous popup remained open");
		const auto point = Point();
		ImGui::GetIO().AddMousePosEvent(point.x, point.y);
		Frame();
		Frame();
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
		Frame();
	}

	void Release(bool expectMenu)
	{
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
		Frame();
		Expect(!ImGui::GetCurrentContext()->OpenPopupStack.empty() == expectMenu,
		       expectMenu ? "An ordinary Scene right click did not open its menu"
		                  : "Scene navigation or a cancelled right press opened the context menu");
		if (expectMenu)
		{
			ImGui::ClosePopupToLevel(0, true);
			refocus = true;
		}
		Frame();
	}
};

void TestSceneRightClick()
{
	Harness h;
	auto& io = ImGui::GetIO();
	h.BeginClick();
	h.Release(true);

	h.BeginClick();
	const auto beforeDrag = h.camera.view;
	const auto point = h.Point();
	io.AddMousePosEvent(point.x + 45, point.y + 20);
	h.Frame();
	Expect(!std::equal(beforeDrag.values.begin(), beforeDrag.values.end(), h.camera.view.values.begin()),
	       "The right drag did not navigate the actual Scene camera");
	h.Release(false);

	h.BeginClick();
	const auto beforeFly = h.camera.position;
	io.AddKeyEvent(ImGuiKey_W, true);
	h.Frame();
	h.Frame();
	Expect(std::abs(h.camera.position.x - beforeFly.x) + std::abs(h.camera.position.y - beforeFly.y) +
	               std::abs(h.camera.position.z - beforeFly.z) >
	           0.0001f,
	       "Right-held keyboard navigation did not move the Scene camera");
	io.AddKeyEvent(ImGuiKey_W, false);
	h.Release(false);

	h.BeginClick();
	io.AddMouseWheelEvent(0, 1);
	h.Frame();
	h.Release(false);

	for (const auto modifier : {ImGuiMod_Alt, ImGuiMod_Ctrl})
	{
		io.AddKeyEvent(modifier, true);
		h.BeginClick();
		io.AddKeyEvent(modifier, false);
		h.Frame();
		h.Release(false);
	}

	h.BeginClick();
	io.AddKeyEvent(ImGuiKey_Escape, true);
	h.Frame();
	io.AddKeyEvent(ImGuiKey_Escape, false);
	h.Release(false);

	h.BeginClick();
	h.focusOther = true;
	h.Frame();
	h.focusOther = false;
	h.refocus = true;
	h.Frame();
	h.Release(false);

	h.BeginClick();
	h.showScene = false;
	h.Frame();
	h.Release(false);
	h.showScene = true;
	h.Frame();

	io.AddMousePosEvent(-20, -20);
	h.Frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
	h.Frame();
	io.AddMousePosEvent(point.x, point.y);
	h.Frame();
	h.Release(false);

	h.BeginClick();
	io.AddMousePosEvent(-20, -20);
	h.Frame();
	h.Release(false);
	h.BeginClick();
	h.Release(true);
	const auto panPoint = h.Point();
	io.AddMousePosEvent(panPoint.x, panPoint.y);
	h.Frame();
	h.Frame();
	const Vec3 beforePan = h.camera.position;
	io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
	h.Frame();
	io.AddMousePosEvent(panPoint.x + 35.f, panPoint.y + 25.f);
	h.Frame();
	Expect(Dot(h.camera.position - beforePan, h.camera.position - beforePan) > 0.f,
	       "Middle-button dragging did not pan the Scene camera");
	io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
	h.Frame();
	Expect(!h.documents->Current()->Dirty(), "Scene navigation changed authoring Dirty state");
}

void TestPanPrecision()
{
	Camera camera;
	camera.position = {1.f, 1.f, 1.f};
	camera.target = {};
	const Vec3 initialPosition = camera.position;
	PanSceneView(camera, {10.f, 5.f}, 100.f);
	Expect(Dot(camera.position - initialPosition, camera.position - initialPosition) > 0.f,
	       "A normal middle-button pan did not move the camera");
	(void)camera.View();
	camera.position = {1.f, 1.f, 1.f};
	camera.target = {};
	PanSceneView(camera, {1.0e10f, 1.0e10f}, 100.f);
	(void)camera.View();
	Expect(Dot(camera.target - camera.position, camera.target - camera.position) > 1.0e-6f,
	       "Middle-button pan collapsed the camera onto its target");
	Expect(Dot(camera.position - initialPosition, camera.position - initialPosition) == 0.f,
	       "An unrepresentable pan changed the camera");
}
} // namespace

int main()
{
	try
	{
		TestPanPrecision();
		TestSceneRightClick();
		std::cout << "Scene right-click menu, navigation and cancellation input tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
