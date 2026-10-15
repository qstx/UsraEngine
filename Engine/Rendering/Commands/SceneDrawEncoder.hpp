#pragma once

#include <Rendering/Commands/SceneDrawRecord.hpp>

namespace Ursa::Rendering
{
// Encodes resolved scene draws into the caller-owned frame command list.
// Submission, attachment states and GPU lifetime belong to RenderCommandExecutor.
class SceneDrawEncoder final
{
public:
	static Result<std::unique_ptr<SceneDrawEncoder>> Create(Rhi::DevicePtr device);
	~SceneDrawEncoder();
	void BeginFrame();
	Result<void> RecordSceneDraws(const Rhi::CommandListPtr& command, const Vector<SceneDrawRecord>& draws,
	                              const CameraData& camera, const SceneLighting& lighting,
	                              const SceneDrawEncoding& encoding);
	Result<std::shared_ptr<const Shader::ShaderAsset>> ResolveBuiltinShader(ResourceId id) const;
	Size MeshCacheSize() const;

private:
	struct Impl;
	explicit SceneDrawEncoder(std::unique_ptr<Impl> implementation);
	std::unique_ptr<Impl> impl;
};
} // namespace Ursa::Rendering
