#include <Core/Types.hpp>
#include <Scene/EditorScenePreview.hpp>

namespace Ursa::Editor
{
Result<std::unique_ptr<EditorScenePreview>> EditorScenePreview::Create(Rhi::DevicePtr device, RenderProvider provider)
{
	if (!device)
	{
		return Result<std::unique_ptr<EditorScenePreview>>::Failure("Editor preview requires a render device");
	}
	return Result<std::unique_ptr<EditorScenePreview>>::Success(
	    std::unique_ptr<EditorScenePreview>(new EditorScenePreview(std::move(provider))));
}

Result<void> EditorScenePreview::Render(Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
                                        Rhi::ResourceState finalState)
{
	if (!target || !target->description.width || !target->description.height)
	{
		return Result<void>::Failure("Editor preview needs a nonzero color target");
	}
	if (!renderer)
	{
		return Result<void>::Failure("Editor preview requires the current SRP render provider");
	}
	Rendering::CameraData data;
	data.identity = cameraIdentity;
	data.settings.type = Rendering::CameraType::SceneView;
	data.debugName = "Scene View Preview";
	data.position = camera.position;
	const Float32 aspect = Float32(target->description.width) / Float32(target->description.height);
	data.view = camera.View();
	data.projection = camera.Projection(aspect);
	data.viewProjection = data.projection * data.view;
	data.inverseViewProjection = data.viewProjection.Inverse();
	data.nearPlane = camera.nearPlane;
	data.farPlane = camera.farPlane;
	data.orthographic = camera.orthographic;
	return renderer(scene, target, finalState, Rendering::CameraType::SceneView, data);
}

Result<void> EditorScenePreview::RenderGame(Scene& scene,
                                            const std::shared_ptr<Rendering::RenderTextureResource>& target,
                                            Rhi::ResourceState finalState)
{
	if (!target || !target->description.width || !target->description.height)
	{
		return Result<void>::Failure("Game preview needs a nonzero color target");
	}
	if (!renderer)
	{
		return Result<void>::Failure("Game preview requires the current SRP render provider");
	}
	return renderer(scene, target, finalState, Rendering::CameraType::Game, {});
}
} // namespace Ursa::Editor
