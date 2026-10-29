#include <Core/StringInterop.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <imgui.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;
using namespace Ursa::Editor;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Result<void> result)
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

GizmoInteractionReport Frame(ImGuiBridge& gui, Scene& scene, GameObject::Ptr selected, const Camera& camera,
                             ViewportPoint mouse, bool pressed, GizmoOperation operation)
{
	ImGui::SetCurrentContext(gui.Context());
	const ImVec2 desktopOrigin = ImGui::GetMainViewport()->Pos;
	ImGui::GetIO().AddMousePosEvent(desktopOrigin.x + mouse.x, desktopOrigin.y + mouse.y);
	ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, pressed);
	done(gui.BeginFrame(1.f / 60.f, 640, 480, 640, 480));
	const ImVec2 viewportOrigin = ImGui::GetMainViewport()->Pos;
	ImGui::SetNextWindowPos(viewportOrigin, ImGuiCond_Always);
	ImGui::SetNextWindowSize({640, 480}, ImGuiCond_Always);
	ImGui::Begin("Gizmo Interaction Test", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
	GizmoInteractionReport interaction;
	const auto result =
	    DrawGizmo(scene, {selected}, selected, camera, {viewportOrigin.x + 20, viewportOrigin.y + 20, 600, 400},
	              operation, GizmoPivotMode::Pivot, GizmoSpace::Local, {}, &interaction);
	if (!result)
	{
		ImGui::End();
		throw std::runtime_error(ToStdString(result.error));
	}
	ImGui::End();
	done(gui.EndFrame());
	return interaction;
}
} // namespace

int main()
{
	try
	{
		auto window = Take(Window::Create(640, 480, "Ursa Gizmo Input Test", false));
		window->SetPosition(-1280, 120);
		DeviceDesc desc;
		desc.window = window;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(CreateDevice(desc));
		auto gui = Take(ImGuiBridge::Create(device, window));
		Scene scene;
		auto selected = Take(scene.CreateGameObject("Gizmo Target"));
		Camera camera;
		camera.position = {0, 0, 5};
		camera.target = {0, 0, 0};
		const ViewportPoint positions[] = {{370, 220}, {390, 220}, {410, 220}, {430, 220}, {450, 220},
		                                   {470, 220}, {330, 180}, {330, 200}, {330, 240}};
		bool transformed = false;
		GizmoInteractionReport lastInteraction;
		bool sawGizmoUse = false;
		for (const ViewportPoint start : positions)
		{
			Frame(*gui, scene, selected, camera, start, false, GizmoOperation::Translate);
			Frame(*gui, scene, selected, camera, start, true, GizmoOperation::Translate);
			lastInteraction =
			    Frame(*gui, scene, selected, camera, {start.x + 30, start.y + 10}, true, GizmoOperation::Translate);
			sawGizmoUse |= lastInteraction.isUsing;
			Frame(*gui, scene, selected, camera, {start.x + 30, start.y + 10}, false, GizmoOperation::Translate);
			const LocalTransform transform = Take(scene.GetLocalTransform(selected));
			if (std::abs(transform.position.x) > 0.001f || std::abs(transform.position.y) > 0.001f ||
			    std::abs(transform.position.z) > 0.001f)
			{
				transformed = true;
				break;
			}
		}
		Expect(transformed && sawGizmoUse, (String("Mouse drag did not hit and use the visible gizmo: over=") +
		                                    (lastInteraction.isOver ? "true" : "false") +
		                                    ", using=" + (lastInteraction.isUsing ? "true" : "false") +
		                                    ", matrixChanged=" + (lastInteraction.matrixChanged ? "true" : "false") +
		                                    ", desktopOrigin=(-1280,120), rect=(origin+20,origin+20,600,400)")
		                                       .c_str());

		done(scene.SetLocalTransform(selected, LocalTransform{}));
		bool rotated = false;
		for (Float32 radius : {24.f, 32.f, 40.f, 50.f, 64.f, 80.f})
		{
			for (int angleIndex = 0; angleIndex < 12; ++angleIndex)
			{
				const Float32 angle = static_cast<Float32>(angleIndex) * 6.2831853f / 12.f;
				const ViewportPoint start{320.f + radius * std::cos(angle), 220.f + radius * std::sin(angle)};
				const ViewportPoint end{320.f + radius * std::cos(angle + 0.35f),
				                        220.f + radius * std::sin(angle + 0.35f)};
				Frame(*gui, scene, selected, camera, start, false, GizmoOperation::Rotate);
				Frame(*gui, scene, selected, camera, start, true, GizmoOperation::Rotate);
				Frame(*gui, scene, selected, camera, end, true, GizmoOperation::Rotate);
				Frame(*gui, scene, selected, camera, end, false, GizmoOperation::Rotate);
				const Quat rotation = Take(scene.GetLocalTransform(selected)).rotation;
				if (std::abs(rotation.x) > 0.001f || std::abs(rotation.y) > 0.001f || std::abs(rotation.z) > 0.001f)
				{
					rotated = true;
					break;
				}
			}
			if (rotated)
			{
				break;
			}
		}
		Expect(rotated, "Mouse drag did not hit and rotate the visible gizmo");

		const ViewportPoint scalePositions[] = {{370, 220}, {390, 220}, {410, 220}, {350, 190},
		                                        {350, 250}, {290, 190}, {290, 250}, {320, 170}};
		done(scene.SetLocalTransform(selected, LocalTransform{}));
		bool scaled = false;
		for (const ViewportPoint start : scalePositions)
		{
			Frame(*gui, scene, selected, camera, start, false, GizmoOperation::Scale);
			Frame(*gui, scene, selected, camera, start, true, GizmoOperation::Scale);
			Frame(*gui, scene, selected, camera, {start.x + 25, start.y + 10}, true, GizmoOperation::Scale);
			Frame(*gui, scene, selected, camera, {start.x + 25, start.y + 10}, false, GizmoOperation::Scale);
			const Vec3 scale = Take(scene.GetLocalTransform(selected)).scale;
			if (std::abs(scale.x - 1.f) > 0.001f || std::abs(scale.y - 1.f) > 0.001f ||
			    std::abs(scale.z - 1.f) > 0.001f)
			{
				scaled = true;
				break;
			}
		}
		Expect(scaled, "Mouse drag did not hit and scale the visible gizmo");
		done(device->WaitIdle());
		gui.reset();
		device.reset();
		window.reset();
		std::cout << "Editor gizmo mouse interaction test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
