#include <Assets/MaterialPreview.hpp>

namespace Ursa::Editor
{
Result<std::unique_ptr<MaterialPreview>> MaterialPreview::Create(Rhi::DevicePtr device, ImGuiBridge& gui, uint32_t size)
{
	if (!device || !size)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure("Material preview needs a device and size");
	}
	auto renderer = Renderer3D::Create(device);
	if (!renderer)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(renderer.error);
	}
	auto color = device->CreateTexture(
	    {size, size, Rhi::Format::rgbA8Unorm,
	     Rhi::TextureUsage::colorAttachment | Rhi::TextureUsage::sampled | Rhi::TextureUsage::transferSource});
	if (!color)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(color.error);
	}
	auto sampler = device->CreateSampler({true, false});
	if (!sampler)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(sampler.error);
	}
	auto imageId = gui.RegisterTexture(color.value, sampler.value);
	if (!imageId)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(imageId.error);
	}
	return Result<std::unique_ptr<MaterialPreview>>::Success(std::unique_ptr<MaterialPreview>(
	    new MaterialPreview(std::move(device), gui, std::move(renderer.value), std::move(color.value),
	                        std::move(sampler.value), imageId.value)));
}

MaterialPreview::~MaterialPreview()
{
	gui.UnregisterTexture(imageId);
}

Result<void> MaterialPreview::Render(const std::shared_ptr<MaterialInstance>& material)
{
	if (!material)
	{
		return Result<void>::Failure("Cannot preview an empty material");
	}
	auto command = device->CreateCommandList();
	if (!command)
	{
		return Result<void>::Failure(command.error);
	}
	auto result = command.value->Begin();
	if (!result)
	{
		return result;
	}
	Camera camera;
	camera.position = {2.5f, 1.8f, 3.5f};
	camera.target = {0, 0, 0};
	const Vector<ModelBoxInstance> boxes{{Mat4::Identity(), {}, material}};
	SceneLighting lighting;
	lighting.ambientIntensity = 0.5f;
	lighting.directional.push_back({{-0.4f, -0.7f, -0.5f}, {}, 2.f});
	result = renderer->RecordOffscreenModels(
	    command.value, color, rendered ? Rhi::ResourceState::shaderRead : Rhi::ResourceState::undefined,
	    Rhi::ResourceState::shaderRead, camera.ViewProjection(1.f), boxes, lighting, camera.position);
	if (result)
	{
		result = command.value->End();
	}
	if (!result)
	{
		return result;
	}
	auto submitted = device->Submit(command.value);
	if (!submitted)
	{
		return Result<void>::Failure(submitted.error);
	}
	result = submitted.value->Wait();
	if (result)
	{
		rendered = true;
	}
	return result;
}
} // namespace Ursa::Editor
