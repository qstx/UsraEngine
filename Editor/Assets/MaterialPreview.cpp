#include <Core/Types.hpp>
#include <Core/Log.hpp>
#include <Assets/MaterialPreview.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Mesh/Mesh.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Scene/ViewportLayout.hpp>
#include <algorithm>
#include <array>
#include <cmath>

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
		data.uv0.insert(data.uv0.end(), {{0, 1}, {1, 1}, {1, 0}, {0, 0}});
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

Result<MeshPtr> CreateMaterialPreviewMesh(MaterialPreviewShape shape)
{
	if (shape == MaterialPreviewShape::Cube)
	{
		auto cube = PreviewCube();
		return cube ? Result<MeshPtr>::Success(std::move(cube))
		            : Result<MeshPtr>::Failure("Cannot create preview cube");
	}
	MeshData data;
	if (shape == MaterialPreviewShape::Plane)
	{
		data.positions = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}};
		data.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
		data.uv0 = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
		data.indices = {0, 1, 2, 0, 2, 3};
	}
	else
	{
		constexpr UInt32 rings = 24, sectors = 48;
		constexpr Float32 pi = 3.14159265358979323846f;
		for (UInt32 ring = 0; ring <= rings; ++ring)
		{
			const Float32 v = Float32(ring) / Float32(rings);
			const Float32 latitude = pi * v;
			for (UInt32 sector = 0; sector <= sectors; ++sector)
			{
				const Float32 u = Float32(sector) / Float32(sectors);
				const Float32 longitude = 2 * pi * u;
				const Vec3 normal{std::sin(latitude) * std::cos(longitude), std::cos(latitude),
				                  std::sin(latitude) * std::sin(longitude)};
				data.positions.push_back({normal.x * 0.5f, normal.y * 0.5f, normal.z * 0.5f});
				data.normals.push_back(normal);
				data.uv0.push_back({u, v});
			}
		}
		for (UInt32 ring = 0; ring < rings; ++ring)
		{
			for (UInt32 sector = 0; sector < sectors; ++sector)
			{
				const UInt32 a = ring * (sectors + 1) + sector;
				const UInt32 b = a + sectors + 1;
				data.indices.insert(data.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
			}
		}
	}
	data.subMeshes.push_back({0, static_cast<UInt32>(data.indices.size())});
	auto mesh = Mesh::Create(std::move(data));
	if (!mesh)
	{
		return mesh;
	}
	auto bounds = mesh.value->RecalculateBounds();
	return bounds ? std::move(mesh) : Result<MeshPtr>::Failure(bounds.error);
}

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
	auto idle = device->WaitIdle();
	if (!idle)
	{
		URSA_LOG_ERROR("Editor", "Preview target teardown after device failure: " + idle.error);
	}
	gui.RetireTexture(imageId);
}

Result<void> MaterialPreview::Resize(UInt32 width, UInt32 height)
{
	if (width == color->desc.width && height == color->desc.height)
	{
		return {};
	}
	auto valid = ResolveViewportSize({width, height}, GameViewSizeMode::FixedResolution, width, height,
	                                 device->Capabilities().maxTextureSize, 16'777'216);
	if (!valid)
	{
		return Result<void>::Failure(valid.error);
	}
	auto created =
	    Rendering::CreateRenderTexture(*device, {width, height, static_cast<UInt32>(Rhi::Format::RgbA8Unorm), 32});
	if (!created)
	{
		return Result<void>::Failure(created.error);
	}
	auto registered = gui.RegisterTexture(created.value->color, sampler);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	auto idle = device->WaitIdle();
	if (!idle)
	{
		gui.UnregisterTexture(registered.value);
		return idle;
	}
	gui.RetireTexture(imageId);
	output = std::move(created.value);
	color = output->color;
	imageId = registered.value;
	return {};
}

Result<void> MaterialPreview::SetShape(MaterialPreviewShape selectedShape)
{
	if (shape == selectedShape)
	{
		return {};
	}
	auto mesh = CreateMaterialPreviewMesh(selectedShape);
	if (!mesh)
	{
		return Result<void>::Failure(mesh.error);
	}
	auto assigned = previewFilter->SetSharedMesh(std::move(mesh.value));
	if (assigned)
	{
		shape = selectedShape;
	}
	return assigned;
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
	previewFilter = filter.value;
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

Result<void> MaterialPreview::Render(const std::shared_ptr<MaterialInstance>& material, const Camera& camera,
                                     const EditorRenderViewport& renderViewport, ResourceId identity)
{
	if (!material)
	{
		return Result<void>::Failure("Material preview has no material");
	}
	auto assigned = previewRenderer->SetSharedMaterial(material);
	return assigned ? RenderScene(previewScene, camera, renderViewport, identity) : assigned;
}

Result<void> MaterialPreview::RenderScene(Scene& scene, const Camera& camera,
                                          const EditorRenderViewport& renderViewport, ResourceId identity)
{
	if (!renderViewport || !output)
	{
		return Result<void>::Failure("Preview has no active render pipeline or target");
	}
	Rendering::CameraSettings settings;
	settings.type = Rendering::CameraType::Preview;
	settings.background = {0.035f, 0.045f, 0.065f, 1};
	settings.targetWidth = color->desc.width;
	settings.targetHeight = color->desc.height;
	Rendering::CameraData data{identity ? identity : cameraIdentity,
	                           camera.ViewProjection(Float32(color->desc.width) / Float32(color->desc.height)),
	                           camera.position, settings};
	return renderViewport(scene, output, Rendering::CameraType::Preview, data);
}
} // namespace Ursa::Editor
