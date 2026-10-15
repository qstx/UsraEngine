#pragma once

#include <Rendering/Scene/CameraData.hpp>
#include <Rendering/Scene/RenderObject.hpp>
#include <Scene/Scene.hpp>
#include <optional>
#include <functional>

namespace Ursa::Rendering
{
class RenderScene final
{
public:
	using BuiltinShaderResolver = std::function<Result<std::shared_ptr<const Shader::ShaderAsset>>(ResourceId)>;
	Result<void> Capture(const Scene& scene, Float32 aspect, std::optional<CameraType> cameraFilter = {},
	                     std::optional<CameraData> cameraOverride = {},
	                     const BuiltinShaderResolver& resolveBuiltin = {});

	const Vector<RenderObject>& Objects() const
	{
		return objects;
	}

	const Vector<CameraData>& Cameras() const
	{
		return cameras;
	}

	const SceneLighting& Lighting() const
	{
		return lighting;
	}

	UInt64 Version() const
	{
		return version;
	}

	bool IsValid() const
	{
		return valid;
	}

private:
	Result<void> Collect(const Scene& scene, const GameObject::Ptr& object, Float32 aspect,
	                     const BuiltinShaderResolver& resolveBuiltin);

	Vector<RenderObject> objects;
	Vector<CameraData> cameras;
	SceneLighting lighting;
	UInt64 version = 0;
	bool valid = false;
	Size objectCount = 0;
	Vector<Mat4> scratchPalette;
	Vector<Mat4> scratchTransforms;
};
} // namespace Ursa::Rendering
