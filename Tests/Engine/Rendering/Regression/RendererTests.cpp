#include <MeshFixtures.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Core/Memory/Memory.hpp>
#include <RenderingFixtures.hpp>
#include <Scene/Scene.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class Draw>
Ursa::Vector<Ursa::UInt8> ReadFrameWith(const Ursa::Rhi::DevicePtr& device, Draw draw, Ursa::UInt32 width = 64,
                                        Ursa::UInt32 height = 64)
{
	using namespace Ursa::Rhi;
	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	const auto before = Ursa::Memory::Snapshot().categories[static_cast<Ursa::Size>(Ursa::Memory::MemoryTag::Renderer)];
	done(Take(draw(color))->Wait());
	const auto after = Ursa::Memory::Snapshot().categories[static_cast<Ursa::Size>(Ursa::Memory::MemoryTag::Renderer)];
	Expect(before.currentBytes == after.currentBytes && before.liveAllocations == after.liveAllocations,
	       "Renderer scratch/control blocks escaped recording scope");
	auto readback = Take(device->CreateBuffer(
	    {static_cast<Ursa::Size>(width) * height * 4, BufferUsage::TransferDestination, Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	done(command->Begin());
	done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	done(command->CopyTextureToBuffer(color, readback));
	done(command->End());
	done(Take(device->Submit(command))->Wait());
	Ursa::Vector<Ursa::UInt8> pixels(static_cast<Ursa::Size>(width) * height * 4);
	done(readback->Read(0, pixels.data(), pixels.size()));
	return pixels;
}

Ursa::Vector<Ursa::UInt8> ReadFrame(const Ursa::Rhi::DevicePtr& device, Ursa::Test::SceneRenderer& renderer,
                                    const Ursa::Camera& camera, const Ursa::Vector<Ursa::Test::CubeDraw>& boxes,
                                    Ursa::UInt32 width = 64, Ursa::UInt32 height = 64)
{
	return ReadFrameWith(
	    device, [&](const Ursa::Rhi::TexturePtr& color) { return renderer.DrawCubes(color, camera, boxes); }, width,
	    height);
}

Ursa::Array<Ursa::UInt8, 4> pixel(const Ursa::Vector<Ursa::UInt8>& image, Ursa::UInt32 width, Ursa::UInt32 x,
                                  Ursa::UInt32 y)
{
	const Ursa::Size offset = (static_cast<Ursa::Size>(y) * width + x) * 4;
	return {image[offset], image[offset + 1], image[offset + 2], image[offset + 3]};
}

void TestInstances(const Ursa::Rhi::DevicePtr& device, Ursa::Test::SceneRenderer& renderer)
{
	Ursa::Camera camera;
	camera.position = {0, 0, 4};
	camera.target = {};
	const Ursa::Vector<Ursa::Test::CubeDraw> boxes{
	    {{{-0.75f, 0, 0}, {0.8f, 0.8f, 0.8f}}, {1, 0, 0, 1}},
	    {{{0.75f, 0, 0}, {0.8f, 0.8f, 0.8f}}, {0, 0, 1, 1}},
	};
	const auto image = ReadFrame(device, renderer, camera, boxes);
	const auto left = pixel(image, 64, 18, 32);
	const auto right = pixel(image, 64, 46, 32);
	Expect(left[0] > 100 && left[2] < 30, "Left instance color or transform is wrong");
	Expect(right[2] > 100 && right[0] < 30, "Right instance color or transform is wrong");

	camera.position = {2, 0, 4};
	camera.target = {2, 0, 0};
	const auto moved = ReadFrame(device, renderer, camera, boxes);
	const auto center = pixel(moved, 64, 32, 32);
	Expect(center[0] < 30 && center[2] < 40, "Changing the camera did not change the image");
}

void TestDepth(const Ursa::Rhi::DevicePtr& device, Ursa::Test::SceneRenderer& renderer)
{
	Ursa::Camera camera;
	camera.position = {0, 0, 4};
	camera.target = {};
	const Ursa::Vector<Ursa::Test::CubeDraw> boxes{
	    {{{0, 0, 0}, {1, 1, 1}}, {1, 0, 0, 1}},
	    {{{0, 0, -1}, {1, 1, 1}}, {0, 1, 0, 1}},
	};
	const auto image = ReadFrame(device, renderer, camera, boxes);
	const auto center = pixel(image, 64, 32, 32);
	Expect(center[0] > 100 && center[1] < 30, "Farther instance covered the nearer instance");
	const auto wide = ReadFrame(device, renderer, camera, boxes, 80, 40);
	const auto wideCenter = pixel(wide, 80, 40, 20);
	Expect(wideCenter[0] > 100 && wideCenter[1] < 30, "Rendering failed after target resize");
}

void TestModelMatrices(const Ursa::Rhi::DevicePtr& device, Ursa::Test::SceneRenderer& renderer)
{
	Ursa::Camera camera;
	camera.position = {0, 0, 4};
	camera.target = {};
	const Ursa::Vector<Ursa::Test::CubeDraw> legacy{{{{0, 0, 0}, {0.7f, 0.7f, 0.7f}}, {1, 0, 0, 1}}};
	const auto oldImage = ReadFrame(device, renderer, camera, legacy);
	const auto newImage =
	    ReadFrameWith(device,
	                  [&](const Ursa::Rhi::TexturePtr& color)
	                  {
		                  return renderer.DrawOffscreen(color, camera.ViewProjection(1.0f),
		                                                {{{legacy[0].transform.Matrix()}, legacy[0].color}});
	                  });
	Expect(pixel(oldImage, 64, 32, 32) == pixel(newImage, 64, 32, 32), "Model-matrix path changed the legacy result");

	const auto parent =
	    Ursa::Mat4::Translation({1, 0, 0}) * Ursa::Mat4::Rotation(Ursa::Quat::FromAxisAngle({0, 1, 0}, 3.14159265f));
	const auto child = Ursa::Mat4::Translation({1, 0, 0}) * Ursa::Mat4::Scale({0.7f, 0.7f, 0.7f});
	const auto rotated = ReadFrameWith(
	    device, [&](const Ursa::Rhi::TexturePtr& color)
	    { return renderer.DrawOffscreen(color, camera.ViewProjection(1.0f), {{parent * child, {0, 1, 0, 1}}}); });
	const auto center = pixel(rotated, 64, 32, 32);
	Expect(center[1] > 100 && center[0] < 30, "Rotated parent did not place its child at the center");
}

void TestSceneCollection(const Ursa::Rhi::DevicePtr& device, Ursa::Test::SceneRenderer& renderer)
{
	Ursa::Scene scene;
	const auto cameraNode = Take(scene.CreateGameObject());
	Ursa::LocalTransform cameraTransform;
	cameraTransform.position = {0, 0, 4};
	done(scene.SetLocalTransform(cameraNode, cameraTransform));
	const auto camera = Take(scene.AddComponent<Ursa::CameraComponent>(cameraNode));
	const auto parent = Take(scene.CreateGameObject());
	Ursa::LocalTransform parentTransform;
	parentTransform.position = {1, 0, 0};
	parentTransform.rotation = Ursa::Quat::FromAxisAngle({0, 1, 0}, 3.14159265f);
	done(scene.SetLocalTransform(parent, parentTransform));
	const auto child = Take(scene.CreateGameObject(parent));
	Ursa::LocalTransform childTransform;
	childTransform.position = {1, 0, 0};
	childTransform.scale = {0.7f, 0.7f, 0.7f};
	done(scene.SetLocalTransform(child, childTransform));
	Take(Ursa::Test::AddRenderer(scene, child, Ursa::Color{0, 1, 0, 1}));
	Expect(!scene.CollectRenderData(1.0f) && scene.CollectRenderData(1.0f).error.find("camera") != Ursa::String::npos,
	       "Missing active camera did not return a diagnostic error");
	done(scene.SetActiveCamera(camera));
	const auto data = Take(scene.CollectRenderData(1.0f));
	Expect(data.objects.size() == 1, "Scene did not collect its child box");
	const auto visible = ReadFrameWith(device, [&](const Ursa::Rhi::TexturePtr& color)
	                                   { return renderer.DrawOffscreen(color, data.viewProjection, data.objects); });
	Expect(pixel(visible, 64, 32, 32)[1] > 100, "Scene world transform or color was not rendered");
	done(scene.SetActive(parent, false));
	const auto hiddenData = Take(scene.CollectRenderData(1.0f));
	Expect(hiddenData.objects.empty(), "Disabled parent still produced a box");
	const auto hidden =
	    ReadFrameWith(device, [&](const Ursa::Rhi::TexturePtr& color)
	                  { return renderer.DrawOffscreen(color, hiddenData.viewProjection, hiddenData.objects); });
	Expect(pixel(hidden, 64, 32, 32)[1] < 30, "Disabled box remained visible");
	done(scene.SetComponentEnabled(camera, false));
	Expect(!scene.CollectRenderData(1.0f), "Disabled camera was accepted");
}
} // namespace

int main()
{
	try
	{
		Ursa::Rhi::DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(Ursa::Rhi::CreateDevice(desc));
		auto renderer = Take(Ursa::Test::SceneRenderer::Create(device));
		TestInstances(device, *renderer);
		TestDepth(device, *renderer);
		TestModelMatrices(device, *renderer);
		TestSceneCollection(device, *renderer);
		renderer.reset();
		done(device->WaitIdle());
		std::cout << "3D renderer GPU tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "3D renderer GPU test failed: " << error.what() << '\n';
		return 1;
	}
}
