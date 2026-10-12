#include <Renderer3D/Renderer3D.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshFilterComponent.hpp>
#include <Scene/MeshRendererComponent.hpp>
#include <Core/StringInterop.hpp>
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

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

Vector<UInt8> Render(Rhi::DevicePtr device, Renderer3D& renderer, const SceneRenderData& data)
{
	auto target = Take(device->CreateTexture(
	    {64, 64, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
	Done(Take(renderer.RenderOffscreenModels(target, data.viewProjection, data.boxes, data.lighting,
	                                         data.cameraPosition))
	         ->Wait());
	auto readback =
	    Take(device->CreateBuffer({64 * 64 * 4, Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(target, readback));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	Vector<UInt8> pixels(64 * 64 * 4, 0);
	Done(readback->Read(0, pixels.data(), pixels.size()));
	return pixels;
}

Array<UInt8, 4> Pixel(const Vector<UInt8>& image, UInt32 x, UInt32 y)
{
	const Size index = (Size(y) * 64 + x) * 4;
	return {image[index], image[index + 1], image[index + 2], image[index + 3]};
}

std::shared_ptr<MaterialInstance> ColorMaterial(Color color, MaterialType type = MaterialType::Unlit)
{
	auto material = std::make_shared<MaterialInstance>(type);
	Done(material->SetBaseColor(color));
	return material;
}

void Run()
{
	Rhi::DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = Take(Rhi::CreateDevice(description));
	auto renderer = Take(Renderer3D::Create(device));
	Scene scene;
	auto object = Take(scene.CreateGameObject("Dynamic Mesh"));
	auto* filter = Take(object->AddComponent<MeshFilterComponent>());
	auto* component = Take(object->AddComponent<MeshRendererComponent>());
	MeshData geometry;
	geometry.positions = {{-1.8f, -0.8f, 0}, {-0.2f, -0.8f, 0}, {-0.2f, 0.8f, 0}, {-1.8f, 0.8f, 0},
	                      {0.2f, -0.8f, 0},  {1.8f, -0.8f, 0},  {1.8f, 0.8f, 0},  {0.2f, 0.8f, 0}};
	geometry.normals.assign(8, {0, 0, 1});
	geometry.tangents.assign(8, {1, 0, 0, 1});
	geometry.indices = {0, 2, 1, 0, 3, 2, 12, 14, 13, 12, 15, 14};
	geometry.subMeshes = {{0, 6, 0}, {6, 6, -8}};
	auto mesh = Take(Mesh::Create(geometry));
	Done(filter->SetSharedMesh(mesh));
	auto red = ColorMaterial({1, 0, 0, 1});
	auto green = ColorMaterial({0, 1, 0, 1});
	Done(component->SetSharedMaterials({red, green}));
	Camera camera;
	camera.position = {0, 0, 5};
	camera.target = {};
	const auto collect = [&]() { return Take(scene.CollectPreviewRenderData(camera, 1)); };
	auto pixels = Render(device, *renderer, collect());
	auto left = Pixel(pixels, 21, 32);
	auto right = Pixel(pixels, 43, 32);
	Expect(left[0] > 200 && left[1] < 20 && right[1] > 200 && right[0] < 20,
	       "Submeshes did not use their ordered material slots");
	Done(component->SetSharedMaterials({green, red}));
	pixels = Render(device, *renderer, collect());
	Expect(Pixel(pixels, 21, 32)[1] > 200 && Pixel(pixels, 43, 32)[0] > 200,
	       "Material reordering did not change the image");
	Done(component->SetSharedMaterials({red, nullptr}));
	pixels = Render(device, *renderer, collect());
	right = Pixel(pixels, 43, 32);
	Expect(right[0] > 200 && right[1] > 200 && right[2] > 200, "Null material slot did not use the default material");
	auto overlay = ColorMaterial({0, 0, 1, 0.5f});
	Done(overlay->SetAlphaMode(AlphaMode::Blend));
	Done(component->SetSharedMaterials({red, green, overlay}));
	pixels = Render(device, *renderer, collect());
	right = Pixel(pixels, 43, 32);
	Expect(right[1] > 60 && right[1] < 220 && right[2] > 60, "Extra transparent slot did not redraw the last submesh");
	Done(component->SetSharedMaterials({red, green}));
	const auto retained = collect();
	auto moved = geometry.positions;
	for (Size i = 4; i < moved.size(); ++i)
	{
		moved[i].x += 10;
	}
	Done(mesh->SetVertices(moved));
	pixels = Render(device, *renderer, retained);
	Expect(Pixel(pixels, 43, 32)[1] > 200, "Collected snapshot changed after a Mesh edit");
	for (Size frame = 0; frame < 40; ++frame)
	{
		const bool visible = frame % 2 == 0;
		Done(mesh->SetVertices(visible ? geometry.positions : moved));
		pixels = Render(device, *renderer, collect());
		Expect((Pixel(pixels, 43, 32)[1] > 200) == visible, "GPU cache reused an outdated Mesh version");
		Expect(renderer->MeshCacheSize() <= 2, "Old GPU Mesh versions accumulated");
	}
	Done(mesh->SetVertices(geometry.positions));
	auto normalMapped = ColorMaterial({1, 1, 1, 1}, MaterialType::PbrMetallicRoughness);
	Done(
	    normalMapped->SetTexture(MaterialTextureSlot::Normal,
	                             Take(MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {128, 255, 128, 255}))));
	Done(component->SetSharedMaterials({normalMapped, normalMapped}));
	Done(scene.SetAmbientLight({1, 1, 1, 1}, 0));
	auto lit = collect();
	lit.lighting.directional.push_back({{0, -1, -0.1f}, {1, 1, 1, 1}, 4});
	auto positive = Pixel(Render(device, *renderer, lit), 43, 32);
	auto tangents = geometry.tangents;
	for (auto& tangent : tangents)
	{
		tangent.w = -1;
	}
	Done(mesh->SetTangents(tangents));
	const auto negativeData = collect();
	lit.boxes = negativeData.boxes;
	auto negative = Pixel(Render(device, *renderer, lit), 43, 32);
	Expect(int(positive[0]) - int(negative[0]) > 30, "Tangent handedness did not affect normal mapped lighting");
	Done(component->SetEnabled(false));
	Expect(collect().boxes.empty(), "Disabled Renderer was collected");
	Done(component->SetEnabled(true));
	Done(mesh->Destroy());
	Expect(!scene.CollectPreviewRenderData(camera, 1), "Destroyed Mesh binding was silently drawn");
	Done(object->RemoveComponent(filter));
	Render(device, *renderer, collect());
	Expect(renderer->MeshCacheSize() == 0, "Destroyed Mesh GPU resources remained cached");
	renderer.reset();
	Done(device->WaitIdle());
}
} // namespace

int main()
{
	try
	{
		Run();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
