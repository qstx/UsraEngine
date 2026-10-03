#include <Scene/EditorScenePreview.hpp>
#include <Core/StringInterop.hpp>
#include <array>
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
	explicit SpyComponent(int& starts, int& updates) : starts(starts), updates(updates)
	{
	}

	Result<void> OnStart(const FrameContext&) override
	{
		++starts;
		return {};
	}

	Result<void> OnUpdate(const FrameContext&) override
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
		const auto node = Take(scene.CreateNode());
		Take(scene.AddComponent<BoxRenderComponent>(node, Color{0.8f, 0.1f, 0.1f, 1.f}));
		int starts = 0;
		int updates = 0;
		Take(scene.AddComponent<SpyComponent>(node, starts, updates));
		Expect(!scene.CollectRenderData(1.f), "Game rendering should still require an active scene camera");
		constexpr uint32_t size = 64;
		auto color = Take(device->CreateTexture(
		    {size, size, Format::rgbA8Unorm, TextureUsage::colorAttachment | TextureUsage::transferSource}));
		auto readback =
		    Take(device->CreateBuffer({size * size * 4, BufferUsage::transferDestination, Memory::readback}));
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(preview->Record(scene, command, color, ResourceState::undefined, ResourceState::transferSource));
		done(command->Barrier(readback, ResourceState::undefined, ResourceState::transferDestination));
		done(command->CopyTextureToBuffer(color, readback));
		done(command->End());
		done(Take(device->Submit(command))->Wait());
		std::array<uint8_t, size * size * 4> pixels{};
		done(readback->Read(0, pixels.data(), pixels.size()));
		const size_t center = (size / 2 * size + size / 2) * 4;
		Expect(pixels[center] > pixels[center + 1] * 2, "Preview camera did not render the box");
		Expect(starts == 0 && updates == 0, "Edit preview invoked a component lifecycle callback");
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
