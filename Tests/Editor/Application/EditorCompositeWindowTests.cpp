#include <MeshFixtures.hpp>
#include <Core/Types.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <RenderingFixtures.hpp>
#include <Scene/EditorGizmo.hpp>
#include <Application/PlaySession.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;

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

void CompositeFrame(const DevicePtr& device, const SwapchainPtr& swapchain, Test::SceneRenderer& renderer,
                    Editor::ImGuiBridge& gui, const std::shared_ptr<Window>& window, Scene& selection,
                    GameObject::Ptr selected, Ursa::UInt32 previewSize, const Scene* game = nullptr)
{
	window->PollEvents();
	auto preview = Take(device->CreateTexture(
	    {previewSize, previewSize, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::Sampled}));
	auto sampler = Take(device->CreateSampler({true, false}));
	const auto image = Take(gui.RegisterTexture(preview, sampler));
	done(gui.BeginFrame(1.f / 60.f, window->FramebufferWidth(), window->FramebufferHeight()));
	auto* draw = ImGui::GetBackgroundDrawList();
	draw->AddImage(image, ImVec2(20, 20), ImVec2(20.f + previewSize, 20.f + previewSize));
	draw->AddText(ImVec2(20, 2), IM_COL32_WHITE, "Scene preview");
	Camera gizmoCamera;
	gizmoCamera.position = {0, 0, 5};
	gizmoCamera.target = {0, 0, 0};
	const auto gizmo =
	    Editor::DrawGizmo(selection, selected, gizmoCamera,
	                      {20, 20, static_cast<Ursa::Float32>(previewSize), static_cast<Ursa::Float32>(previewSize)},
	                      Editor::GizmoOperation::Translate);
	if (!gizmo)
	{
		throw std::runtime_error(ToStdString(gizmo.error));
	}
	done(gui.EndFrame());

	auto acquired = Take(swapchain->Acquire());
	if (!acquired.texture)
	{
		throw std::runtime_error("Window unexpectedly minimized during compositing test");
	}
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	Camera camera;
	camera.position = {0, 0, 5};
	if (game)
	{
		auto frame = Take(game->CollectRenderData(1.f));
		done(Take(renderer.DrawOffscreen(preview, frame.viewProjection, frame.objects, frame.lighting,
		                                 frame.cameraPosition, ResourceState::ShaderRead))
		         ->Wait());
	}
	else
	{
		const Vector<Test::MeshDraw> boxes{{Mat4::Identity(), {0.9f, 0.2f, 0.1f, 1.f}, {}}};
		done(Take(renderer.DrawOffscreen(preview, camera.ViewProjection(1.f), boxes, {}, camera.position,
		                                 ResourceState::ShaderRead))
		         ->Wait());
	}
	done(command->Barrier(acquired.texture, acquired.initialState, ResourceState::ColorAttachment));
	RenderTarget target;
	target.color = acquired.texture;
	done(command->BeginRendering(target));
	done(gui.Record(command, swapchain->Format(), acquired.texture->desc.width, acquired.texture->desc.height));
	done(command->EndRendering());
	done(command->Barrier(acquired.texture, ResourceState::ColorAttachment, ResourceState::Present));
	done(command->End());
	const auto fence = Take(device->Submit(command, swapchain));
	done(swapchain->Present());
	done(fence->Wait());
	gui.UnregisterTexture(image);
}
} // namespace

int main()
{
	try
	{
		auto window = Take(Window::Create(640, 480, "Ursa Editor compositing", true));
		DeviceDesc desc;
		desc.window = window;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(CreateDevice(desc));
		auto swapchain = Take(device->CreateSwapchain({}));
		auto renderer = Take(Test::SceneRenderer::Create(device));
		auto gui = Take(Editor::ImGuiBridge::Create(device, window));
		Scene selection;
		const auto selected = Take(selection.CreateGameObject());
		CompositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 128);
		done(Editor::ApplyGizmoWorldTransform(selection, selected, Mat4::Translation({0.5f, 0, 0})));
		window->Resize(800, 600);
		CompositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 160);
		const auto root =
		    std::filesystem::current_path() / ("editor_window_play_" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directory(root);
		auto resources = Take(ResourceSession::Open({root}));
		ComponentRegistry registry;
		done(RegisterBuiltinComponents(registry));
		auto draft = Take(Editor::SceneDraft::Create(ResourceKind::Scene, registry, *resources));
		const GameObject::Ptr gameRoot = draft->GetScene().RootGameObjects().front();
		auto cameraNode = Take(draft->GetScene().CreateGameObject(gameRoot));
		LocalTransform cameraPose;
		cameraPose.position = {0, 0, 5};
		done(draft->GetScene().SetLocalTransform(cameraNode, cameraPose));
		const auto cameraId = Take(draft->GetScene().AddComponent<CameraComponent>(cameraNode));
		done(draft->GetScene().SetActiveCamera(cameraId));
		auto boxNode = Take(draft->GetScene().CreateGameObject(gameRoot));
		Take(Ursa::Test::AddRenderer(draft->GetScene(), boxNode, Color{0.2f, 0.8f, 0.4f, 1.f}));
		Editor::PlaySession play;
		done(play.Start(*draft, ResourceId::Generate(), registry, *resources));
		done(play.Tick(1.f / 30.f, window.get(), {}));
		window->Minimize();
		window->Restore();
		CompositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 160, play.GetScene());
		play.Stop();
		CompositeFrame(device, swapchain, *renderer, *gui, window, selection, selected, 160, &draft->GetScene());
		std::filesystem::remove_all(root);
		done(device->WaitIdle());
		gui.reset();
		renderer.reset();
		swapchain.reset();
		device.reset();
		window.reset();
		std::cout << "Editor scene and UI compositing window test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
