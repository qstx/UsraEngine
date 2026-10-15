#pragma once

#include <Rendering/Pipeline/NativeForwardPipeline.hpp>
#include <Math/Camera.hpp>
#include <Scene/Scene.hpp>

namespace Ursa::Editor
{
class EditorScenePreview final
{
public:
	static Result<std::unique_ptr<EditorScenePreview>> Create(Rhi::DevicePtr device);

	Camera& GetCamera()
	{
		return camera;
	}

	const Camera& GetCamera() const
	{
		return camera;
	}

	Result<void> Render(const Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                    Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead);

	Result<void> RenderGame(const Scene& scene, const std::shared_ptr<Rendering::RenderTextureResource>& target,
	                        Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead);

private:
	explicit EditorScenePreview(Rhi::DevicePtr selectedDevice) : device(std::move(selectedDevice))
	{
	}

	Rhi::DevicePtr device;
	std::unique_ptr<Rendering::NativeForwardPipeline> renderer;
	ResourceId cameraIdentity = ResourceId::Generate();
	Camera camera;
};
} // namespace Ursa::Editor
