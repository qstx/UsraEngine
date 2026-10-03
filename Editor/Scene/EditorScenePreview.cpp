#include <Scene/EditorScenePreview.hpp>

namespace Ursa::Editor
{
Result<std::unique_ptr<EditorScenePreview>> EditorScenePreview::Create(Rhi::DevicePtr device)
{
	auto renderer = Renderer3D::Create(std::move(device));
	if (!renderer)
	{
		return Result<std::unique_ptr<EditorScenePreview>>::Failure(renderer.error);
	}
	return Result<std::unique_ptr<EditorScenePreview>>::Success(
	    std::unique_ptr<EditorScenePreview>(new EditorScenePreview(std::move(renderer.value))));
}

Result<void> EditorScenePreview::Record(const Scene& scene, const Rhi::CommandListPtr& command,
                                        const Rhi::TexturePtr& color, Rhi::ResourceState initialState,
                                        Rhi::ResourceState finalState)
{
	if (!color || !color->desc.width || !color->desc.height)
	{
		return Result<void>::Failure("Editor preview needs a nonzero color target");
	}
	const float aspect = static_cast<float>(color->desc.width) / static_cast<float>(color->desc.height);
	auto frame = scene.CollectPreviewRenderData(camera, aspect);
	if (!frame)
	{
		return Result<void>::Failure(frame.error);
	}
	return renderer->RecordOffscreenModels(command, color, initialState, finalState, frame.value.viewProjection,
	                                       frame.value.boxes, frame.value.lighting, frame.value.cameraPosition);
}

Result<void> EditorScenePreview::RecordGame(const Scene& scene, const Rhi::CommandListPtr& command,
                                            const Rhi::TexturePtr& color, Rhi::ResourceState initialState,
                                            Rhi::ResourceState finalState)
{
	if (!color || !color->desc.width || !color->desc.height)
	{
		return Result<void>::Failure("Game preview needs a nonzero color target");
	}
	const float aspect = static_cast<float>(color->desc.width) / static_cast<float>(color->desc.height);
	auto frame = scene.CollectRenderData(aspect);
	if (!frame)
	{
		return Result<void>::Failure(frame.error);
	}
	return renderer->RecordOffscreenModels(command, color, initialState, finalState, frame.value.viewProjection,
	                                       frame.value.boxes, frame.value.lighting, frame.value.cameraPosition);
}
} // namespace Ursa::Editor
