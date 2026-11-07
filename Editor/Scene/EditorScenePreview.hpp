#pragma once

#include <Scene/EditorRenderViewport.hpp>
#include <Math/Camera.hpp>
#include <Scene/Scene.hpp>

namespace Ursa::Editor
{
class EditorScenePreview final
{
public:
	using RenderProvider =
	    std::function<Result<void>(Scene&, const std::shared_ptr<Rendering::RenderTextureResource>&, Rhi::ResourceState,
	                               std::optional<Rendering::CameraType>, std::optional<Rendering::CameraData>)>;
	static Result<std::unique_ptr<EditorScenePreview>> Create(Rhi::DevicePtr device, RenderProvider provider = {});

	Camera& GetCamera()
	{
		return camera;
	}

	const Camera& GetCamera() const
	{
		return camera;
	}

	Result<void> Render(Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                    Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead);

	Result<void> RenderGame(Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                        Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead);

private:
	explicit EditorScenePreview(RenderProvider provider) : renderer(std::move(provider))
	{
	}

	RenderProvider renderer;
	ResourceId cameraIdentity = ResourceId::Generate();
	Camera camera;
};
} // namespace Ursa::Editor
