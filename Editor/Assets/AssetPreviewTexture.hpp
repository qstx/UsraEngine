#pragma once

#include <Material/Material.hpp>
#include <Ui/ImGuiBridge.hpp>

namespace Ursa::Editor
{
enum class TexturePreviewChannel
{
	Rgba,
	Rgb,
	Red,
	Green,
	Blue,
	Alpha
};

// A display-only view of a selected mip. Source pixels and import settings are never modified.
Result<MaterialTexturePtr> CreateTexturePreviewView(const MaterialTexturePtr& source, UInt32 mip,
                                                    TexturePreviewChannel channel);
Result<void> UploadTexturePreview(Rhi::DevicePtr device, ImGuiBridge& gui, const MaterialTexturePtr& texture,
                                  ImTextureID& id, Rhi::TexturePtr& gpuTexture, Rhi::SamplerPtr& gpuSampler);

struct AssetPreviewTextureInput
{
	VirtualPath path;
	MaterialTexturePtr source;
	MaterialTexturePtr imported;
};
} // namespace Ursa::Editor
