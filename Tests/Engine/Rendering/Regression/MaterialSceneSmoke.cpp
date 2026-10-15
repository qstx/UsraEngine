#include <MeshFixtures.hpp>
#include <Core/Types.hpp>
#include <Core/StringInterop.hpp>
#include <RenderingFixtures.hpp>
#include <Scene/Scene.hpp>
#include <iostream>
#include <memory>
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
		Ursa::Scene scene;
		auto cameraNode = Take(scene.CreateGameObject());
		Ursa::LocalTransform cameraPose;
		cameraPose.position = {0.0f, 0.0f, 5.0f};
		done(scene.SetLocalTransform(cameraNode, cameraPose));
		done(scene.SetActiveCamera(Take(scene.AddComponent<Ursa::CameraComponent>(cameraNode))));
		auto boxNode = Take(scene.CreateGameObject());
		Take(Ursa::Test::AddRenderer(scene, boxNode));
		auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::Unlit);
		done(material->SetBaseColor({1.0f, 0.0f, 0.0f, 1.0f}));
		done(scene.FindComponent<Ursa::MeshRendererComponent>(boxNode)->SetSharedMaterial(material));
		auto frame = Take(scene.CollectRenderData(1.0f));
		auto color = Take(device->CreateTexture(
		    {64, 64, Ursa::Rhi::Format::RgbA8Unorm,
		     Ursa::Rhi::TextureUsage::ColorAttachment | Ursa::Rhi::TextureUsage::TransferSource}));
		auto submitted =
		    renderer->DrawOffscreen(color, frame.viewProjection, frame.objects, frame.lighting, frame.cameraPosition);
		done(Take(std::move(submitted))->Wait());
		auto readback = Take(device->CreateBuffer(
		    {64 * 64 * 4, Ursa::Rhi::BufferUsage::TransferDestination, Ursa::Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(command->Barrier(readback, Ursa::Rhi::ResourceState::Undefined,
		                      Ursa::Rhi::ResourceState::TransferDestination));
		done(command->CopyTextureToBuffer(color, readback));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		Ursa::UInt8 center[4]{};
		done(readback->Read((32 * 64 + 32) * 4, center, sizeof(center)));
		if (center[0] < 240 || center[1] > 20 || center[2] > 20)
		{
			throw std::runtime_error("Material scene smoke pixel mismatch");
		}
		renderer.reset();
		done(device->WaitIdle());
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
