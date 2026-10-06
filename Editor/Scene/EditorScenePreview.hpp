#pragma once

#include <Renderer3D/Renderer3D.hpp>
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

	Result<void> Record(const Scene& scene, const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color,
	                    Rhi::ResourceState initialState, Rhi::ResourceState finalState);

	Result<void> RecordGame(const Scene& scene, const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color,
	                        Rhi::ResourceState initialState, Rhi::ResourceState finalState);

private:
	explicit EditorScenePreview(std::unique_ptr<Renderer3D> sceneRenderer) : renderer(std::move(sceneRenderer))
	{
	}

	std::unique_ptr<Renderer3D> renderer;
	Camera camera;
};
} // namespace Ursa::Editor
