#include <Core/Types.hpp>
#include <Rendering/Scene/RenderScene.hpp>
#include <Scene/Scene.hpp>
#include <Core/StringInterop.hpp>
#include <cmath>
#include <algorithm>
#include <exception>
#include <string>

namespace Ursa
{
Result<void> Scene::SetActiveCamera(ComponentId id)
{
	if (id && (!ContainsComponent(id) || dynamic_cast<CameraComponent*>(components[id.index].object) == nullptr))
	{
		return Result<void>::Failure("Active camera must refer to a CameraComponent in this scene");
	}
	activeCamera = id;
	return {};
}

Result<void> Scene::SetAmbientLight(Color color, Ursa::Float32 intensity)
{
	if (!ValidLightColor(color) || !std::isfinite(intensity) || intensity < 0.0f)
	{
		return Result<void>::Failure("Ambient light color or intensity must be finite and nonnegative");
	}
	ambientColor = color;
	ambientIntensity = intensity;
	return {};
}

Result<SceneRenderData> Scene::CollectRenderData(Ursa::Float32 aspect) const
{
	if (!ContainsComponent(activeCamera))
	{
		return Result<SceneRenderData>::Failure("Scene has no active camera");
	}
	const ComponentSlot& cameraSlot = components[activeCamera.index];
	const auto* camera = dynamic_cast<const CameraComponent*>(cameraSlot.object);
	if (!camera || !IsComponentActive(activeCamera).value)
	{
		return Result<SceneRenderData>::Failure("Scene active camera is disabled or invalid");
	}
	try
	{
		const Mat4 projection =
		    Mat4::Perspective(camera->verticalFovRadians, aspect, camera->nearPlane, camera->farPlane);
		const Mat4 cameraWorld = objects[cameraSlot.owner.index].object->WorldTransform().value;
		const Mat4 viewProjection = projection * cameraWorld.InverseAffine();
		return CollectWithCamera(viewProjection, {cameraWorld.At(0, 3), cameraWorld.At(1, 3), cameraWorld.At(2, 3)});
	}
	catch (const std::exception& error)
	{
		return Result<SceneRenderData>::Failure(String("Cannot collect scene rendering data: ") + error.what());
	}
}

Result<SceneRenderData> Scene::CollectPreviewRenderData(const Camera& camera, Ursa::Float32 aspect) const
{
	try
	{
		return CollectWithCamera(camera.ViewProjection(aspect), camera.position);
	}
	catch (const std::exception& error)
	{
		return Result<SceneRenderData>::Failure(String("Cannot collect scene preview: ") + error.what());
	}
}

Result<SceneRenderData> Scene::CollectWithCamera(const Mat4& viewProjection, Vec3 cameraPosition) const
{
	Rendering::RenderScene snapshot;
	auto captured = snapshot.Capture(*this, 1.0f);
	if (!captured)
	{
		return Result<SceneRenderData>::Failure(captured.error);
	}
	return Result<SceneRenderData>::Success({viewProjection, cameraPosition, snapshot.Lighting(), snapshot.Objects()});
}
} // namespace Ursa
