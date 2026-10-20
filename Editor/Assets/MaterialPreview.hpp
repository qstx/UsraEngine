#pragma once

#include <Core/Types.hpp>
#include <Scene/EditorRenderViewport.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Ui/ImGuiBridge.hpp>

namespace Ursa::Editor
{
enum class MaterialPreviewShape
{
	Sphere,
	Cube,
	Plane
};

Result<MeshPtr> CreateMaterialPreviewMesh(MaterialPreviewShape shape);

class MaterialPreview final
{
public:
	static Result<std::unique_ptr<MaterialPreview>> Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& guiBridge,
	                                                       Ursa::UInt32 size = 256);

	~MaterialPreview();

	Result<void> Render(const std::shared_ptr<MaterialInstance>& material, const EditorRenderViewport& renderViewport);
	Result<void> Render(const std::shared_ptr<MaterialInstance>& material, const Camera& camera,
	                    const EditorRenderViewport& renderViewport, ResourceId identity = {});
	Result<void> RenderScene(Scene& scene, const Camera& camera, const EditorRenderViewport& renderViewport,
	                         ResourceId identity = {});
	Result<void> Resize(UInt32 width, UInt32 height);
	Result<void> SetShape(MaterialPreviewShape shape);

	ImTextureID ImageId() const
	{
		return imageId;
	}

	const Rhi::TexturePtr& ColorTexture() const
	{
		return color;
	}

private:
	MaterialPreview(Rhi::DevicePtr selectedDevice, ImGuiBridge& guiBridge, Rhi::TexturePtr colorTexture,
	                Rhi::SamplerPtr textureSampler, ImTextureID registeredImageId)
	    : device(std::move(selectedDevice)), gui(guiBridge), color(std::move(colorTexture)),
	      sampler(std::move(textureSampler)), imageId(registeredImageId)
	{
	}

	Result<void> InitializePreviewScene(UInt32 size);

	Rhi::DevicePtr device;
	ImGuiBridge& gui;
	Rhi::TexturePtr color;
	std::shared_ptr<Rendering::RenderTextureResource> output;
	Rhi::SamplerPtr sampler;
	ImTextureID imageId = ImTextureID_Invalid;
	Scene previewScene;
	MeshRendererComponent* previewRenderer = nullptr;
	MeshFilterComponent* previewFilter = nullptr;
	MaterialPreviewShape shape = MaterialPreviewShape::Cube;
	ResourceId cameraIdentity = ResourceId::Generate();
};
} // namespace Ursa::Editor
