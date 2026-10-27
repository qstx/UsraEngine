#include <Assets/SceneDraft.hpp>
#include <Core/StringInterop.hpp>
#include <Scene/SceneViewNavigation.hpp>
#include <Scene/SceneViewPanel.hpp>
#include <Scene/EditorScenePreview.hpp>
#include <Workspace/EditorDocuments.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace Ursa::Editor
{
struct SceneViewPanelTestAccess
{
	static Camera& CameraOf(SceneViewPanel& panel)
	{
		return panel.sceneTarget.preview->GetCamera();
	}

	static void StartFocus(SceneViewPanel& panel, const Camera& from, const Camera& to)
	{
		panel.StartFocusTransition(from, to);
	}

	static void AdvanceFocus(SceneViewPanel& panel, Camera& camera, Float32 deltaTime)
	{
		panel.AdvanceFocusTransition(camera, deltaTime);
	}

	static Float32 TargetSpeed(const SceneViewPanel& panel)
	{
		return panel.sceneFlythroughTargetSpeed;
	}

	static void Set2D(SceneViewPanel& panel, bool enabled)
	{
		panel.sceneView2DMode = enabled;
	}

	static bool FocusActive(const SceneViewPanel& panel)
	{
		return panel.sceneFocusTransition.active;
	}
};
} // namespace Ursa::Editor

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

	void Frame(Float32 deltaTime = 1.f / 60.f)
	{
		Done(gui->BeginFrame(deltaTime, 1100, 800));
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

void TestMiddleClickAndPan()
{
	Harness h;
	auto focusTarget = Take(h.draft->GetScene().CreateGameObject("Focus target"));
	Camera& camera = SceneViewPanelTestAccess::CameraOf(*h.views);
	camera.position = {0.f, 0.f, 5.f};
	camera.target = {};
	camera.up = {0.f, 1.f, 0.f};
	auto& io = ImGui::GetIO();
	const auto& rect = h.views->SceneLayout().displayRect;
	const ImVec2 point{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
	io.AddMousePosEvent(point.x, point.y);
	h.Frame();
	const Vec3 beforeClick = camera.position;
	io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
	h.Frame();
	io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
	h.Frame();
	Expect(!SceneViewPanelTestAccess::FocusActive(*h.views), "A middle click started focusing instead of staying idle");
	for (int frame = 0; frame < 30; ++frame)
	{
		h.Frame();
	}
	Expect(Dot(camera.position - beforeClick, camera.position - beforeClick) < 1.0e-8f,
	       "A middle click changed the Scene camera");
	Done(h.hierarchy.Select(h.draft->GetScene(), focusTarget));
	h.hierarchy.RequestFocusSelection();
	io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
	h.Frame();
	io.AddMousePosEvent(point.x + 10.f, point.y);
	h.Frame();
	const Vec3 afterPan = camera.position;
	io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
	h.Frame();
	Expect(!SceneViewPanelTestAccess::FocusActive(*h.views), "Releasing a middle drag started a focus transition");
	for (int frame = 0; frame < 30; ++frame)
	{
		h.Frame();
	}
	Expect(Dot(camera.position - afterPan, camera.position - afterPan) < 1.0e-8f,
	       "The Scene camera moved after releasing a middle drag");
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

void TestSceneCameraRecovery()
{
	Harness h;
	Camera& camera = SceneViewPanelTestAccess::CameraOf(*h.views);
	camera.position = {-7.24953254e37f, -1.11738583e38f, -1.98268038e38f};
	camera.target = camera.position;
	camera.up = {0.f, 1.f, 0.f};
	Expect(!IsSceneViewCameraValid(camera), "The captured collapsed camera was considered valid");
	h.Frame();
	Expect(IsSceneViewCameraValid(camera) && h.diagnostic.find("camera") != String::npos &&
	           !h.documents->Current()->Dirty(),
	       "Scene rendering did not recover the captured collapsed camera without authoring changes");
	(void)camera.ViewProjection(1.f);
	SceneViewPanelTestAccess::Set2D(*h.views, true);
	camera.orthographic = true;
	camera.position = {-7.24953254e37f, -1.11738583e38f, -1.98268038e38f};
	camera.target = camera.position;
	h.Frame();
	Expect(IsSceneViewCameraValid(camera) && camera.orthographic && camera.position.x == 0.f &&
	           camera.position.y == 0.f && camera.position.z > 0.f,
	       "Scene camera recovery did not preserve the 2D view orientation");
	SceneViewPanelTestAccess::Set2D(*h.views, false);
	camera.orthographic = false;
	const ImVec2 pointer = h.Point();
	ImGui::GetIO().AddMousePosEvent(pointer.x, pointer.y);
	h.refocus = true;
	h.Frame();
	ImGui::GetIO().AddKeyEvent(ImGuiKey_UpArrow, true);
	h.Frame();
	h.Frame(1000.f);
	h.Frame(1000.f);
	Expect(SceneViewPanelTestAccess::TargetSpeed(*h.views) > 0.f &&
	           std::isfinite(SceneViewPanelTestAccess::TargetSpeed(*h.views)) && IsSceneViewCameraValid(camera),
	       "Prolonged keyboard navigation produced an invalid Scene camera or speed");
	ImGui::GetIO().AddKeyEvent(ImGuiKey_UpArrow, false);
	h.Frame();
	Expect(!h.documents->Current()->Dirty(), "Scene camera recovery or navigation changed authoring state");
	Camera start;
	start.position = {0.f, 0.f, 1.f};
	start.target = {};
	Camera end = start;
	end.position.z = -1.f;
	SceneViewPanelTestAccess::StartFocus(*h.views, start, end);
	Camera transitioning = start;
	SceneViewPanelTestAccess::AdvanceFocus(*h.views, transitioning, 0.25f * (1.f - std::cbrt(0.5f)));
	Expect(IsSceneViewCameraValid(transitioning), "An opposing focus transition collapsed the camera");
	(void)transitioning.View();
}

void TestNavigationPrecision()
{
	const auto check = [](const char* operation, const auto& navigate)
	{
		Camera camera;
		camera.position = {1.f, 1.f, 1.f};
		camera.target = {};
		navigate(camera);
		try
		{
			(void)camera.View();
		}
		catch (const std::exception& error)
		{
			throw std::runtime_error(std::string(operation) + " produced an invalid camera: " + error.what());
		}
	};
	check("keyboard move", [](Camera& camera) { MoveSceneView(camera, {1.f, 1.f}, 1.f, 1.0e38f, false); });
	check("flythrough", [](Camera& camera) { FlythroughSceneView(camera, {}, {1.f, 1.f}, 1.f, 1.f, 1.0e38f, false); });
	check("pointer zoom", [](Camera& camera) { ZoomSceneView(camera, 1.f, {1.0e20f, 1.0e20f}, {100.f, 100.f}); });
	check("focus", [](Camera& camera) { FocusSceneView(camera, {1.0e38f, 1.0e38f, 1.0e38f}, 1.f, 1.f); });
	Float32 targetSpeed = 9.f;
	Float32 currentSpeed = 9.f;
	AdvanceSceneViewMovementSpeed(true, 1000.f, targetSpeed, currentSpeed);
	Expect(std::isfinite(targetSpeed) && std::isfinite(currentSpeed) && targetSpeed <= 1.0e6f && currentSpeed <= 1.0e6f,
	       "Prolonged camera acceleration overflowed its speed");
	AdvanceSceneViewMovementSpeed(false, 1.f, targetSpeed, currentSpeed);
	Expect(targetSpeed == 0.f && currentSpeed == 0.f, "Camera speed did not recover after movement stopped");
}
} // namespace

int main()
{
	try
	{
		TestPanPrecision();
		TestNavigationPrecision();
		TestSceneCameraRecovery();
		TestSceneRightClick();
		TestMiddleClickAndPan();
		std::cout << "Scene right-click menu, navigation and cancellation input tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
