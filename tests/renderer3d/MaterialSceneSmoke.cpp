#include <core/StringInterop.hpp>
#include <renderer3d/Renderer3D.hpp>
#include <scene/Scene.hpp>
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
		auto renderer = Take(Ursa::Renderer3D::Create(device));
		Ursa::Scene scene;
		auto cameraNode = Take(scene.CreateNode());
		Ursa::LocalTransform cameraPose;
		cameraPose.position = {0.0f, 0.0f, 5.0f};
		done(scene.SetLocalTransform(cameraNode, cameraPose));
		done(scene.SetActiveCamera(Take(scene.AddComponent<Ursa::CameraComponent>(cameraNode))));
		auto boxNode = Take(scene.CreateNode());
		Take(scene.AddComponent<Ursa::BoxRenderComponent>(boxNode));
		auto material = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::unlit);
		done(material->SetBaseColor({1.0f, 0.0f, 0.0f, 1.0f}));
		scene.FindComponent<Ursa::BoxRenderComponent>(boxNode)->material = material;
		auto frame = Take(scene.CollectRenderData(1.0f));
		auto color = Take(device->CreateTexture(
		    {64, 64, Ursa::Rhi::Format::rgbA8Unorm,
		     Ursa::Rhi::TextureUsage::colorAttachment | Ursa::Rhi::TextureUsage::transferSource}));
		auto submitted = renderer->RenderOffscreenModels(color, frame.viewProjection, frame.boxes, frame.lighting,
		                                                 frame.cameraPosition);
		done(Take(std::move(submitted))->Wait());
		auto readback = Take(device->CreateBuffer(
		    {64 * 64 * 4, Ursa::Rhi::BufferUsage::transferDestination, Ursa::Rhi::Memory::readback}));
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(command->Barrier(readback, Ursa::Rhi::ResourceState::undefined,
		                      Ursa::Rhi::ResourceState::transferDestination));
		done(command->CopyTextureToBuffer(color, readback));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		uint8_t center[4]{};
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
