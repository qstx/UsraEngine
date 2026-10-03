#pragma once

#include <renderer3d/Renderer3D.hpp>
#include <ui/ImGuiBridge.hpp>

namespace Ursa::Editor
{
class MaterialPreview final
{
public:
	static Result<std::unique_ptr<MaterialPreview>> Create(Rhi::DevicePtr device, ImGuiBridge& gui,
	                                                       uint32_t size = 256);

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
	MaterialPreview(Rhi::DevicePtr device, ImGuiBridge& gui, std::unique_ptr<Renderer3D> renderer,
	                Rhi::TexturePtr color, Rhi::SamplerPtr sampler, ImTextureID imageId)
	    : device(std::move(device)), gui(gui), renderer(std::move(renderer)), color(std::move(color)),
	      sampler(std::move(sampler)), imageId(imageId)
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
