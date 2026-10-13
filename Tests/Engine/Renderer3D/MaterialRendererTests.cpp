#include <MeshFixtures.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Scene/Scene.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

Ursa::Vector<Ursa::UInt8> ReadFrame(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer,
                                    const Ursa::Vector<Ursa::ModelBoxInstance>& boxes,
                                    const Ursa::SceneLighting& lighting = {}, Ursa::UInt32 width = 64,
                                    Ursa::UInt32 height = 64)
{
	using namespace Ursa::Rhi;
	Ursa::Camera camera;
	camera.position = {0, 0, 5};
	camera.target = {};
	auto color = Take(device->CreateTexture(
	    {width, height, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	done(Take(renderer.RenderOffscreenModels(color, camera.ViewProjection(Ursa::Float32(width) / height), boxes,
	                                         lighting, camera.position))
	         ->Wait());
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

Ursa::Array<Ursa::UInt8, 4> pixel(const Ursa::Vector<Ursa::UInt8>& image, Ursa::UInt32 x, Ursa::UInt32 y,
                                  Ursa::UInt32 width = 64)
{
	const Ursa::Size index = (static_cast<Ursa::Size>(y) * width + x) * 4;
	return {image[index], image[index + 1], image[index + 2], image[index + 3]};
}

Ursa::ModelBoxInstance box(Ursa::Vec3 position, Ursa::Vec3 scale, std::shared_ptr<Ursa::MaterialInstance> material = {},
                           Ursa::Color color = {})
{
	return {Ursa::Mat4::Translation(position) * Ursa::Mat4::Scale(scale), color, std::move(material)};
}

Ursa::MaterialTexturePtr Texture(Ursa::UInt32 width, Ursa::UInt32 height, Ursa::TextureColorSpace space,
                                 Ursa::Vector<Ursa::UInt8> pixels)
{
	return Take(Ursa::MaterialTexture::Create(width, height, space, std::move(pixels)));
}

std::shared_ptr<Ursa::MaterialInstance> Material(Ursa::MaterialType type, Ursa::Color color = {})
{
	auto result = std::make_shared<Ursa::MaterialInstance>(type);
	done(result->SetBaseColor(color));
	return result;
}

Ursa::SceneLighting sun(Ursa::Float32 intensity = 4.0f)
{
	Ursa::SceneLighting lighting;
	lighting.ambientIntensity = 0.0f;
	lighting.directional.push_back({{0, 0, -1}, {}, intensity});
	return lighting;
}

int Difference(Ursa::Array<Ursa::UInt8, 4> a, Ursa::Array<Ursa::UInt8, 4> b)
{
	return std::abs(int(a[0]) - int(b[0])) + std::abs(int(a[1]) - int(b[1])) + std::abs(int(a[2]) - int(b[2]));
}

void TestUvAndColorSpace(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	auto unlit = Material(Ursa::MaterialType::Unlit);
	done(unlit->SetSampler({false, false}));
	done(unlit->SetTexture(Ursa::MaterialTextureSlot::BaseColor,
	                       Texture(2, 1, Ursa::TextureColorSpace::Srgb, {255, 0, 0, 255, 0, 0, 255, 255})));
	const auto image = ReadFrame(device, renderer, {box({}, {2, 2, 1}, unlit)});
	const auto left = pixel(image, 20, 32);
	const auto right = pixel(image, 44, 32);
	Expect(left[0] > 240 && left[2] < 20 && right[2] > 240 && right[0] < 20,
	       "Cube UVs or procedural base-color sampling failed");
	done(unlit->SetTexture(Ursa::MaterialTextureSlot::BaseColor,
	                       Texture(1, 1, Ursa::TextureColorSpace::Srgb, {128, 128, 128, 255})));
	const auto gray = pixel(ReadFrame(device, renderer, {box({}, {1, 1, 1}, unlit)}), 32, 32);
	Expect(gray[0] >= 123 && gray[0] <= 133, "sRGB material texture was not decoded and encoded correctly");
}

void TestPbrMapsAndLighting(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	auto pbr = Material(Ursa::MaterialType::PbrMetallicRoughness, {0.8f, 0.3f, 0.15f, 1});
	auto render = [&](const Ursa::SceneLighting& lighting)
	{ return pixel(ReadFrame(device, renderer, {box({}, {1.5f, 1.5f, 1}, pbr)}, lighting), 32, 32); };
	const auto directional = sun();
	const auto original = render(directional);
	done(pbr->SetBaseColor({0.15f, 0.3f, 0.8f, 1}));
	const auto blue = render(directional);
	Expect(original[0] > original[2] && blue[2] > blue[0], "PBR base color had no visible effect");
	done(pbr->SetMetallic(1.0f));
	done(pbr->SetRoughness(0.2f));
	const auto metal = render(directional);
	Expect(Difference(blue, metal) > 15, "PBR metallic or roughness factors had no visible effect");
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::MetallicRoughness,
	                     Texture(1, 1, Ursa::TextureColorSpace::Linear, {0, 255, 0, 255})));
	const auto mappedMetal = render(directional);
	Expect(Difference(metal, mappedMetal) > 10, "Metallic-roughness texture had no visible effect");
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::MetallicRoughness, {}));
	auto angled = directional;
	angled.directional[0].direction = {-0.55f, 0, -0.84f};
	const auto flatNormal = render(angled);
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Normal,
	                     Texture(1, 1, Ursa::TextureColorSpace::Linear, {220, 128, 220, 255})));
	const auto tiltedNormal = render(angled);
	Expect(Difference(flatNormal, tiltedNormal) > 10, "Tangent-space normal map had no visible effect");
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Normal, {}));
	Ursa::SceneLighting ambient;
	ambient.ambientIntensity = 1.0f;
	const auto unoccluded = render(ambient);
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Occlusion,
	                     Texture(1, 1, Ursa::TextureColorSpace::Linear, {0, 0, 0, 255})));
	const auto occluded = render(ambient);
	Expect(Difference(unoccluded, occluded) > 15, "Occlusion map did not affect ambient light");
	done(pbr->SetEmissive({1.0f, 0.0f, 0.0f, 1.0f}));
	const auto emissive = render(ambient);
	Expect(emissive[0] > occluded[0] + 80, "Emissive factor had no visible effect");
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Emissive,
	                     Texture(1, 1, Ursa::TextureColorSpace::Srgb, {0, 0, 0, 255})));
	Expect(render(ambient)[0] < emissive[0] - 80, "Emissive texture had no visible effect");

	auto unlit = Material(Ursa::MaterialType::Unlit, {0.4f, 0.5f, 0.6f, 1});
	const auto unlitBox = box({}, {1, 1, 1}, unlit);
	const auto withSun = pixel(ReadFrame(device, renderer, {unlitBox}, directional), 32, 32);
	const auto withoutSun = pixel(ReadFrame(device, renderer, {unlitBox}, ambient), 32, 32);
	Expect(withSun == withoutSun, "Unlit material changed with lighting");

	done(pbr->SetEmissive({0, 0, 0, 1}));
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Emissive, {}));
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Occlusion, {}));
	done(pbr->SetMetallic(0.0f));
	done(pbr->SetRoughness(1.0f));
	const auto noLight = render(sun(0));
	const auto direct = render(directional);
	Expect(Difference(noLight, direct) > 15, "Directional light did not affect PBR");
	const auto side = render(angled);
	Expect(Difference(direct, side) > 5, "Directional orientation did not affect PBR");
	Ursa::SceneLighting point;
	point.ambientIntensity = 0;
	point.point.push_back({{0, 0, 2}, {}, 20.0f, 5.0f});
	const auto near = render(point);
	point.point[0].position = {0, 0, 4};
	const auto far = render(point);
	Expect(Difference(near, far) > 10, "Point-light distance had no effect");
	point.point[0].position = {0, 0, 2};
	point.point.push_back({{1, 0, 2}, {}, 20.0f, 5.0f});
	const auto twoPoints = render(point);
	Expect(Difference(near, twoPoints) > 5, "Second point light was ignored");
}

void TestTransformedNormals(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	auto pbr = Material(Ursa::MaterialType::PbrMetallicRoughness, {0.8f, 0.8f, 0.8f, 1});
	auto cube = box({}, {1.0f, 0.7f, 1.0f}, pbr);
	cube.model = Ursa::Mat4::Rotation(Ursa::Quat::FromAxisAngle({0, 1, 0}, 0.78539816f)) * cube.model;
	const auto unscaled = cube;
	cube.model = Ursa::Mat4::Scale({2.0f, 1.0f, 0.5f}) * cube.model;
	auto light = sun();
	const auto baseline = pixel(ReadFrame(device, renderer, {unscaled}, light), 32, 32);
	const auto flat = pixel(ReadFrame(device, renderer, {cube}, light), 32, 32);
	Expect(flat[0] > baseline[0], "Nonuniform parent scale did not use inverse-transpose normals");
	light.directional[0].direction = {-0.4f, 0, -0.9f};
	done(pbr->SetTexture(Ursa::MaterialTextureSlot::Normal,
	                     Texture(1, 1, Ursa::TextureColorSpace::Linear, {220, 128, 220, 255})));
	const auto mapped = pixel(ReadFrame(device, renderer, {cube}, light), 32, 32);
	Expect(Difference(flat, mapped) > 10, "Rotated, nonuniformly scaled cube ignored its normal map");
}

void TestSharedAndLegacy(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	auto shared = Material(Ursa::MaterialType::Unlit, {1, 0, 0, 1});
	Ursa::Vector<Ursa::ModelBoxInstance> boxes{
	    box({-0.8f, 0, 0}, {0.8f, 0.8f, 0.8f}, shared),
	    box({0.8f, 0, 0}, {0.8f, 0.8f, 0.8f}, shared),
	};
	const auto red = ReadFrame(device, renderer, boxes);
	Expect(pixel(red, 20, 32)[0] > 200 && pixel(red, 44, 32)[0] > 200, "Shared material was not used by both cubes");
	done(shared->SetBaseColor({0, 0, 1, 1}));
	const auto blue = ReadFrame(device, renderer, boxes);
	Expect(pixel(blue, 20, 32)[2] > 200 && pixel(blue, 44, 32)[2] > 200,
	       "Shared material update did not reach the next frame");
	auto independent = shared->Clone();
	done(independent->SetBaseColor({0, 1, 0, 1}));
	boxes[1].material = independent;
	boxes.push_back(box({0, -1.1f, -0.2f}, {0.8f, 0.8f, 0.8f}, {}, {1, 0, 0, 1}));
	const auto mixed = ReadFrame(device, renderer, boxes);
	Expect(pixel(mixed, 20, 32)[2] > 200 && pixel(mixed, 44, 32)[1] > 200 && pixel(mixed, 32, 50)[0] > 150,
	       "Material replacement, clone, or legacy color path failed");
}

void TestLegacyPbrMaterialBackingReuse(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	auto ceramic = Material(Ursa::MaterialType::PbrMetallicRoughness, {0.12f, 0.45f, 0.95f, 1});
	auto gold = Material(Ursa::MaterialType::PbrMetallicRoughness, {0.95f, 0.55f, 0.16f, 1});
	done(gold->SetMetallic(1.0f));
	done(gold->SetRoughness(0.25f));
	const Ursa::Vector<Ursa::ModelBoxInstance> boxes{
	    box({-0.8f, 0, 0}, {0.8f, 0.8f, 0.8f}, ceramic),
	    box({0.8f, 0, 0}, {0.8f, 0.8f, 0.8f}, gold),
	};
	for (Ursa::UInt32 frame = 0; frame < 24; ++frame)
	{
		const auto image = ReadFrame(device, renderer, boxes, sun());
		const auto ceramicPixel = pixel(image, 20, 32);
		const auto goldPixel = pixel(image, 44, 32);
		Expect(ceramicPixel[2] > ceramicPixel[0] && goldPixel[0] > goldPixel[2],
		       "Legacy PBR material backing reused another material's uniform data between frames");
	}
}

void TestRecordedOffscreenFramesTrackDepthState(const Ursa::Rhi::DevicePtr& device)
{
	using namespace Ursa::Rhi;
	auto renderer = Take(Ursa::Renderer3D::Create(device));
	auto color = Take(
	    device->CreateTexture({64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::Sampled}));
	Ursa::Camera camera;
	camera.position = {0, 0, 5};
	camera.target = {};
	for (Ursa::UInt32 frame = 0; frame < 4; ++frame)
	{
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(renderer->RecordOffscreenModels(command, color,
		                                     frame == 0 ? ResourceState::Undefined : ResourceState::ShaderRead,
		                                     ResourceState::ShaderRead, camera.ViewProjection(1.0f), {}));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
	}
}

class SwapMaterialOnUpdate final : public Ursa::Component
{
public:
	SwapMaterialOnUpdate(Ursa::MeshRendererComponent* targetBox, std::shared_ptr<Ursa::MaterialInstance> nextMaterial)
	    : box(targetBox), next(std::move(nextMaterial))
	{
	}

	Ursa::Result<void> Update(const Ursa::FrameContext&) override
	{
		done(box->SetSharedMaterial(next));
		return {};
	}

private:
	Ursa::MeshRendererComponent* box;
	std::shared_ptr<Ursa::MaterialInstance> next;
};

void TestSceneMaterialReplacement(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	Ursa::Scene scene;
	const auto cameraNode = Take(scene.CreateGameObject());
	Ursa::LocalTransform pose;
	pose.position = {0, 0, 5};
	done(scene.SetLocalTransform(cameraNode, pose));
	done(scene.SetActiveCamera(Take(scene.AddComponent<Ursa::CameraComponent>(cameraNode))));
	const auto node = Take(scene.CreateGameObject());
	Take(Ursa::Test::AddRenderer(scene, node, Ursa::Color{1, 0, 0, 1}));
	auto* boxComponent = scene.FindComponent<Ursa::MeshRendererComponent>(node);
	const auto legacyData = Take(scene.CollectRenderData(1.0f));
	const auto legacy = pixel(ReadFrame(device, renderer, legacyData.boxes), 32, 32);
	Expect(legacy[0] > 200, "Legacy scene box lost its color");
	auto blue = Material(Ursa::MaterialType::Unlit, {0, 0, 1, 1});
	Take(scene.AddComponent<SwapMaterialOnUpdate>(node, boxComponent, blue));
	scene.BeginFrame();
	done(scene.Update({}));
	const auto changedData = Take(scene.CollectRenderData(1.0f));
	const auto changed = pixel(ReadFrame(device, renderer, changedData.boxes), 32, 32);
	Expect(changed[2] > 200 && changed[0] < 20 && legacyData.boxes[0].material != blue,
	       "Material replacement during update missed the current render frame");
}

void TestAlphaModes(const Ursa::Rhi::DevicePtr& device, Ursa::Renderer3D& renderer)
{
	auto red = Material(Ursa::MaterialType::Unlit, {1, 0, 0, 0.25f});
	auto blue = Material(Ursa::MaterialType::Unlit, {0, 0, 1, 1});
	const auto back = box({0, 0, -1}, {2, 2, 0.5f}, blue);
	const auto front = box({0, 0, 0}, {2, 2, 0.5f}, red);
	auto opaque = pixel(ReadFrame(device, renderer, {back, front}), 32, 32);
	Expect(opaque[0] > 240 && opaque[2] < 20, "Opaque mode used base-color alpha");
	const auto reverseOpaque = pixel(ReadFrame(device, renderer, {front, back}), 32, 32);
	Expect(reverseOpaque[0] > 240 && reverseOpaque[2] < 20,
	       "Opaque mode did not write depth independently of input order");
	done(red->SetAlphaMode(Ursa::AlphaMode::Mask, 0.5f));
	done(red->SetSampler({false, false}));
	done(red->SetTexture(
	    Ursa::MaterialTextureSlot::BaseColor,
	    Texture(3, 1, Ursa::TextureColorSpace::Srgb, {255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 0})));
	done(red->SetBaseColor({1, 0, 0, 1}));
	const auto masked = ReadFrame(device, renderer, {front, back});
	Expect(pixel(masked, 20, 32)[2] > 240 && pixel(masked, 32, 32)[0] > 240,
	       "Mask cutoff did not discard low-alpha texels or preserve high-alpha texels");
	done(red->SetAlphaMode(Ursa::AlphaMode::Mask, 128.0f / 255.0f));
	done(red->SetTexture(Ursa::MaterialTextureSlot::BaseColor,
	                     Texture(1, 1, Ursa::TextureColorSpace::Srgb, {255, 255, 255, 128})));
	Expect(pixel(ReadFrame(device, renderer, {front, back}), 32, 32)[0] > 240,
	       "A mask texel at the cutoff was incorrectly discarded");
	done(red->SetTexture(Ursa::MaterialTextureSlot::BaseColor,
	                     Texture(1, 1, Ursa::TextureColorSpace::Srgb, {255, 255, 255, 127})));
	Expect(pixel(ReadFrame(device, renderer, {front, back}), 32, 32)[2] > 240,
	       "A mask texel below the cutoff was not discarded");
	done(red->SetTexture(Ursa::MaterialTextureSlot::BaseColor, {}));
	done(red->SetBaseColor({1, 0, 0, 0.5f}));
	done(red->SetAlphaMode(Ursa::AlphaMode::Blend));
	const auto blended = pixel(ReadFrame(device, renderer, {front, back}), 32, 32);
	Expect(blended[0] > 100 && blended[2] > 100 && blended[0] < 200, "Blend mode did not mix with opaque background");
	auto far = Material(Ursa::MaterialType::Unlit, {0, 1, 0, 0.5f});
	done(far->SetAlphaMode(Ursa::AlphaMode::Blend));
	const auto farBox = box({0, 0, -0.6f}, {2, 2, 0.5f}, far);
	const auto orderA = pixel(ReadFrame(device, renderer, {front, farBox, back}), 32, 32);
	const auto orderB = pixel(ReadFrame(device, renderer, {back, farBox, front}), 32, 32);
	Expect(orderA == orderB && orderA[0] > orderA[1],
	       "Blend objects were not drawn in a deterministic far-to-near order");
	const auto middleOpaque = box({0, 0, -0.3f}, {2, 2, 0.2f}, blue);
	const auto occludedFar = pixel(ReadFrame(device, renderer, {front, farBox, middleOpaque}), 32, 32);
	Expect(occludedFar[1] < 10 && occludedFar[0] > 100 && occludedFar[2] > 100,
	       "Opaque depth did not reject a farther blend object");
	const auto sameDepth = box({0, 0, 0}, {2, 2, 0.5f}, far);
	const auto equalA = pixel(ReadFrame(device, renderer, {front, sameDepth}), 32, 32);
	const auto equalB = pixel(ReadFrame(device, renderer, {front, sameDepth}), 32, 32);
	Expect(equalA == equalB, "Equal-distance blend order was not stable");
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
		auto renderer = Take(Ursa::Renderer3D::Create(device));
		TestUvAndColorSpace(device, *renderer);
		TestPbrMapsAndLighting(device, *renderer);
		TestTransformedNormals(device, *renderer);
		TestSharedAndLegacy(device, *renderer);
		TestLegacyPbrMaterialBackingReuse(device, *renderer);
		TestRecordedOffscreenFramesTrackDepthState(device);
		TestSceneMaterialReplacement(device, *renderer);
		TestAlphaModes(device, *renderer);
		renderer.reset();
		done(device->WaitIdle());
		std::cout << "Material renderer GPU tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Material renderer GPU test failed: " << error.what() << '\n';
		return 1;
	}
}
