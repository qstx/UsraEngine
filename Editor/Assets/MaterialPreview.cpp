#include <Core/Types.hpp>
#include <Assets/MaterialPreview.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Mesh/Mesh.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <algorithm>
#include <array>

namespace Ursa::Editor
{
namespace
{
MeshPtr PreviewCube()
{
	struct Face
	{
		Vec3 normal;
		std::array<Vec3, 4> vertices;
	};

	const std::array<Face, 6> faces{{
	    {{0, 0, 1}, {{{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}}},
	    {{0, 0, -1}, {{{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}}}},
	    {{1, 0, 0}, {{{0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}}},
	    {{-1, 0, 0}, {{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}}}},
	    {{0, 1, 0}, {{{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}}},
	    {{0, -1, 0}, {{{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}}}},
	}};
	MeshData data;
	for (const Face& face : faces)
	{
		const UInt32 base = static_cast<UInt32>(data.positions.size());
		for (Vec3 vertex : face.vertices)
		{
			data.positions.push_back(vertex);
			data.normals.push_back(face.normal);
		}
		data.indices.insert(data.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	data.subMeshes.push_back({0, static_cast<UInt32>(data.indices.size())});
	auto mesh = Mesh::Create(std::move(data));
	if (!mesh || !mesh.value->RecalculateBounds())
	{
		return {};
	}
	return std::move(mesh.value);
}
} // namespace

Result<std::unique_ptr<MaterialPreview>> MaterialPreview::Create(Rhi::DevicePtr selectedDevice, ImGuiBridge& guiBridge,
                                                                 Ursa::UInt32 size)
{
	if (!selectedDevice || !size)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure("Material preview needs a device and size");
	}
	auto createdOutput =
	    Rendering::CreateRenderTexture(*selectedDevice, {size, size, static_cast<UInt32>(Rhi::Format::RgbA8Unorm), 32});
	if (!createdOutput)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(createdOutput.error);
	}
	auto createdSampler = selectedDevice->CreateSampler({true, false});
	if (!createdSampler)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(createdSampler.error);
	}
	auto registeredImageId = guiBridge.RegisterTexture(createdOutput.value->color, createdSampler.value);
	if (!registeredImageId)
	{
		return Result<std::unique_ptr<MaterialPreview>>::Failure(registeredImageId.error);
	}
	auto preview = std::unique_ptr<MaterialPreview>(
	    new MaterialPreview(std::move(selectedDevice), guiBridge, createdOutput.value->color,
	                        std::move(createdSampler.value), registeredImageId.value));
	preview->output = std::move(createdOutput.value);
	auto initialized = preview->InitializePreviewScene(size);
	return initialized ? Result<std::unique_ptr<MaterialPreview>>::Success(std::move(preview))
	                   : Result<std::unique_ptr<MaterialPreview>>::Failure(initialized.error);
}

MaterialPreview::~MaterialPreview()
{
	gui.UnregisterTexture(imageId);
}

Result<void> MaterialPreview::InitializePreviewScene(UInt32 size)
{
	auto object = previewScene.CreateGameObject("Material Preview Object");
	if (!object)
	{
		return Result<void>::Failure(object.error);
	}
	auto mesh = PreviewCube();
	if (!mesh)
	{
		return Result<void>::Failure("Cannot create the Material Preview mesh");
	}
	auto filter = object.value->AddComponent<MeshFilterComponent>();
	if (!filter)
	{
		return Result<void>::Failure(filter.error);
	}
	auto assigned = filter.value->SetSharedMesh(std::move(mesh));
	if (!assigned)
	{
		return assigned;
	}
	auto meshRenderer = object.value->AddComponent<MeshRendererComponent>();
	if (!meshRenderer)
	{
		return Result<void>::Failure(meshRenderer.error);
	}
	previewRenderer = meshRenderer.value;
	auto cameraObject = previewScene.CreateGameObject("Material Preview Camera");
	if (!cameraObject)
	{
		return Result<void>::Failure(cameraObject.error);
	}
	LocalTransform cameraTransform;
	cameraTransform.position = {0, 0, 3.5f};
	auto cameraSet = cameraObject.value->SetLocalTransform(cameraTransform);
	if (!cameraSet)
	{
		return cameraSet;
	}
	auto camera = cameraObject.value->AddComponent<CameraComponent>();
	if (!camera)
	{
		return Result<void>::Failure(camera.error);
	}
	camera.value->rendering.type = Rendering::CameraType::Preview;
	camera.value->rendering.targetWidth = size;
	camera.value->rendering.targetHeight = size;
	auto activeCamera = previewScene.SetActiveCamera(camera.value->Id());
	if (!activeCamera)
	{
		return activeCamera;
	}
	auto lightObject = previewScene.CreateGameObject("Material Preview Light");
	if (!lightObject)
	{
		return Result<void>::Failure(lightObject.error);
	}
	auto light = lightObject.value->AddComponent<DirectionalLightComponent>();
	if (!light)
	{
		return Result<void>::Failure(light.error);
	}
	auto intensity = light.value->SetIntensity(2.0f);
	if (!intensity)
	{
		return intensity;
	}
	auto ambient = previewScene.SetAmbientLight({1, 1, 1, 1}, 0.5f);
	if (!ambient)
	{
		return ambient;
	}
	return previewScene.CommitChanges();
}

Result<void> MaterialPreview::Render(const std::shared_ptr<MaterialInstance>& material,
                                     const EditorRenderViewport& renderViewport)
{
	if (!material || !renderViewport || !output || !previewRenderer)
	{
		return Result<void>::Failure("Material preview needs a material and active pipeline viewport");
	}
	auto setMaterial = previewRenderer->SetSharedMaterial(material);
	if (!setMaterial)
	{
		return setMaterial;
	}
	return renderViewport(previewScene, output, Rendering::CameraType::Preview, {});
}
} // namespace Ursa::Editor
