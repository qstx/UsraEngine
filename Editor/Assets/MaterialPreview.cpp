#include <Core/Types.hpp>
#include <Assets/MaterialPreview.hpp>

namespace Ursa::Editor
{
Result<std::unique_ptr<MaterialPreview>> MaterialPreview::Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& guiBridge,
                                                                 Ursa::UInt32 size)
{
	if (!selectedDevice || !size)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure("Material preview needs a device and size");
	}
	auto createdRenderer = Renderer3D::Create(selectedDevice);
	if (!createdRenderer)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(createdRenderer.error);
	}
	auto createdColor = selectedDevice->CreateTexture(
	    {size, size, Rhi::Format::RgbA8Unorm,
	     Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferSource});
	if (!createdColor)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(createdColor.error);
	}
	auto createdSampler = selectedDevice->CreateSampler({true, false});
	if (!createdSampler)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(createdSampler.error);
	}
	auto registeredImageId = guiBridge.RegisterTexture(createdColor.value, createdSampler.value);
	if (!registeredImageId)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(registeredImageId.error);
	}
	return Result<std::unique_ptr<MaterialPreview>>::Success(std::unique_ptr<MaterialPreview>(
	    new MaterialPreview(std::move(selectedDevice), guiBridge, std::move(createdRenderer.value),
	                        std::move(createdColor.value), std::move(createdSampler.value), registeredImageId.value)));
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
	    command.value, color, rendered ? Rhi::ResourceState::ShaderRead : Rhi::ResourceState::Undefined,
	    Rhi::ResourceState::ShaderRead, camera.ViewProjection(1.f), boxes, lighting, camera.position);
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
