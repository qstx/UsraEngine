#pragma once

#include <Core/Types.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Ui/ImGuiBridge.hpp>

namespace Ursa::Editor
{
class MaterialPreview final
{
public:
	static Result<std::unique_ptr<MaterialPreview>> Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& guiBridge,
	                                                       Ursa::UInt32 size = 256);

	~MaterialPreview();

	Result<void> Render(const std::shared_ptr<MaterialInstance>& material);

	ImTextureID ImageId() const
	{
		return imageId;
	}

	const Rhi::TexturePtr& ColorTexture() const
	{
		return color;
	}

private:
	MaterialPreview(Rhi::DevicePtr selectedDevice, ImGuiBridge& guiBridge, std::unique_ptr<Renderer3D> selectedRenderer,
	                Rhi::TexturePtr colorTexture, Rhi::SamplerPtr textureSampler, ImTextureID registeredImageId)
	    : device(std::move(selectedDevice)), gui(guiBridge), renderer(std::move(selectedRenderer)),
	      color(std::move(colorTexture)), sampler(std::move(textureSampler)), imageId(registeredImageId)
	{
	}

	Rhi::DevicePtr device;
	ImGuiBridge& gui;
	std::unique_ptr<Renderer3D> renderer;
	Rhi::TexturePtr color;
	Rhi::SamplerPtr sampler;
	ImTextureID imageId = ImTextureID_Invalid;
	bool rendered = false;
};
} // namespace Ursa::Editor
