#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Scene/EditorScenePreview.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Rhi;

template <class T> T Take(Result<T> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
	return std::move(value.value);
}

void done(Result<void> value)
{
	if (!value)
	{
		throw std::runtime_error(ToStdString(value.error));
	}
}

void Expect(bool value, const char* message)
{
	if (!value)
	{
		throw std::runtime_error(message);
	}
}

class SpyComponent final : public Component
{
public:
	explicit SpyComponent(int& startCount, int& updateCount) : starts(startCount), updates(updateCount)
	{
	}

	Result<void> Start(const FrameContext&) override
	{
		++starts;
		return {};
	}

	Result<void> Update(const FrameContext&) override
	{
		++updates;
		return {};
	}

private:
	int& starts;
	int& updates;
};
} // namespace

int main()
{
	try
	{
		DeviceDesc desc;
#ifndef NDEBUG
		desc.validation = true;
#endif
		auto device = Take(CreateDevice(desc));
		auto preview = Take(Editor::EditorScenePreview::Create(device));
		preview->GetCamera().position = {0, 0, 5};
		preview->GetCamera().target = {0, 0, 0};
		Scene scene;
		const auto node = Take(scene.CreateGameObject());
		Take(scene.AddComponent<BoxRenderComponent>(node, Color{0.8f, 0.1f, 0.1f, 1.f}));
		int starts = 0;
		int updates = 0;
		Take(scene.AddComponent<SpyComponent>(node, starts, updates));
		Expect(!scene.CollectRenderData(1.f), "Game rendering should still require an active scene camera");
		constexpr Ursa::UInt32 size = 64;
		auto color = Take(device->CreateTexture(
		    {size, size, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		auto readback =
		    Take(device->CreateBuffer({size * size * 4, BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(preview->Record(scene, command, color, ResourceState::Undefined, ResourceState::TransferSource));
		done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		done(command->CopyTextureToBuffer(color, readback));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		Ursa::Array<Ursa::UInt8, size * size * 4> pixels{};
		done(readback->Read(0, pixels.data(), pixels.size()));
		const Ursa::Size center = (size / 2 * size + size / 2) * 4;
		Expect(pixels[center] > pixels[center + 1] * 2, "Preview camera did not render the box");
		Expect(starts == 0 && updates == 0, "Edit preview invoked a component lifecycle callback");

		auto cameraNode = Take(scene.CreateGameObject("Game Camera"));
		LocalTransform cameraTransform;
		cameraTransform.position = {0, 0, 5};
		done(scene.SetLocalTransform(cameraNode, cameraTransform));
		const ComponentId cameraId = Take(scene.AddComponent<CameraComponent>(cameraNode));
		done(scene.SetActiveCamera(cameraId));
		auto gamePreview = Take(Editor::EditorScenePreview::Create(device));
		auto gameColor = Take(device->CreateTexture(
		    {128, 72, Format::RgbA8Unorm,
		     TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSource}));
		auto sceneColor = Take(device->CreateTexture(
		    {96, 96, Format::RgbA8Unorm,
		     TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::TransferSource}));
		Expect(gameColor != sceneColor && gameColor->desc.width == 128 && sceneColor->desc.width == 96,
		       "Scene and Game preview targets are not independent");
		command = Take(device->CreateCommandList());
		done(command->Begin());
		done(preview->Record(scene, command, sceneColor, ResourceState::Undefined, ResourceState::ShaderRead));
		done(gamePreview->RecordGame(scene, command, gameColor, ResourceState::Undefined, ResourceState::ShaderRead));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		Expect(starts == 0 && updates == 0, "Game camera preview invoked a gameplay lifecycle callback");
		done(device->WaitIdle());
		std::cout << "Editor camera preview test passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
