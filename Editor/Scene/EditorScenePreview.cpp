#include <Core/Types.hpp>
#include <Scene/EditorScenePreview.hpp>

namespace Ursa::Editor
{
Result<std::unique_ptr<EditorScenePreview>> EditorScenePreview::Create(Rhi::DevicePtr device)
{
	if (!device)
	{
		return Result<std::unique_ptr<EditorScenePreview>>::Failure("Editor preview requires a render device");
	}
	return Result<std::unique_ptr<EditorScenePreview>>::Success(
	    std::unique_ptr<EditorScenePreview>(new EditorScenePreview(std::move(device))));
}

Result<void> EditorScenePreview::Render(const Scene& scene,
                                        const std::shared_ptr<Rendering::RenderTextureResource>& target,
                                        Rhi::ResourceState finalState)
{
	if (!target || !target->description.width || !target->description.height)
	{
		return Result<void>::Failure("Editor preview needs a nonzero color target");
	}
	if (!renderer)
	{
		auto created = Rendering::NativeForwardPipeline::Create(device);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		renderer = std::move(created.value);
	}
	Rendering::CameraData data;
	data.identity = cameraIdentity;
	data.settings.type = Rendering::CameraType::SceneView;
	data.debugName = "Scene View Preview";
	data.position = camera.position;
	data.viewProjection =
	    camera.ViewProjection(Float32(target->description.width) / Float32(target->description.height));
	return renderer->RenderOffscreen(scene, target, finalState, Rendering::CameraType::SceneView, data);
}

Result<void> EditorScenePreview::RenderGame(const Scene& scene,
                                            const std::shared_ptr<Rendering::RenderTextureResource>& target,
                                            Rhi::ResourceState finalState)
{
	if (!target || !target->description.width || !target->description.height)
	{
		return Result<void>::Failure("Game preview needs a nonzero color target");
	}
	if (!renderer)
	{
		auto created = Rendering::NativeForwardPipeline::Create(device);
		if (!created)
		{
			return Result<void>::Failure(created.error);
		}
		renderer = std::move(created.value);
	}
	return renderer->RenderOffscreen(scene, target, finalState, Rendering::CameraType::Game);
}
} // namespace Ursa::Editor
