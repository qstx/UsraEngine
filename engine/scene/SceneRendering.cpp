#include <scene/Scene.hpp>
#include <core/StringInterop.hpp>
#include <cmath>
#include <exception>
#include <string>

namespace Ursa
{
Result<void> Scene::SetActiveCamera(ComponentId id)
{
	if (id && (!ContainsComponent(id) || dynamic_cast<CameraComponent*>(components[id.index].object.get()) == nullptr))
	{
		return Result<void>::Failure("Active camera must refer to a CameraComponent in this scene");
	}
	activeCamera = id;
	return {};
}

Result<void> Scene::SetAmbientLight(Color color, float intensity)
{
	if (!ValidLightColor(color) || !std::isfinite(intensity) || intensity < 0.0f)
	{
		return Result<void>::Failure("Ambient light color or intensity must be finite and nonnegative");
	}
	ambientColor = color;
	ambientIntensity = intensity;
	return {};
}

Result<SceneRenderData> Scene::CollectRenderData(float aspect) const
{
	if (!ContainsComponent(activeCamera))
	{
		return Result<SceneRenderData>::Failure("Scene has no active camera");
	}
	const ComponentSlot& cameraSlot = components[activeCamera.index];
	const auto* camera = dynamic_cast<const CameraComponent*>(cameraSlot.object.get());
	if (!camera || !IsComponentActive(activeCamera).value)
	{
		return Result<SceneRenderData>::Failure("Scene active camera is disabled or invalid");
	}
	try
	{
		const Mat4 projection =
		    Mat4::Perspective(camera->verticalFovRadians, aspect, camera->nearPlane, camera->farPlane);
		const Mat4 viewProjection = projection * ComputeWorld(cameraSlot.node).InverseAffine();
		const Mat4 cameraWorld = ComputeWorld(cameraSlot.node);
		return CollectWithCamera(viewProjection, {cameraWorld.At(0, 3), cameraWorld.At(1, 3), cameraWorld.At(2, 3)});
	}
	catch (const std::exception& error)
	{
		return Result<SceneRenderData>::Failure(String("Cannot collect scene rendering data: ") + error.what());
	}
}

Result<SceneRenderData> Scene::CollectPreviewRenderData(const Camera& camera, float aspect) const
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
	try
	{
		SceneRenderData data;
		data.viewProjection = viewProjection;
		data.cameraPosition = cameraPosition;
		data.lighting.ambientColor = ambientColor;
		data.lighting.ambientIntensity = ambientIntensity;
		for (NodeId root : RootNodes())
		{
			CollectRendering(root, data);
		}
		if (data.lighting.point.size() > SceneLighting::maxPointLights)
		{
			return Result<SceneRenderData>::Failure(
			    ToUrsaString("Active point light count " + std::to_string(data.lighting.point.size()) +
			                 " exceeds renderer capacity " + std::to_string(SceneLighting::maxPointLights)));
		}
		if (data.lighting.directional.size() > SceneLighting::maxDirectionalLights)
		{
			return Result<SceneRenderData>::Failure(
			    ToUrsaString("Active directional light count " + std::to_string(data.lighting.directional.size()) +
			                 " exceeds renderer capacity " + std::to_string(SceneLighting::maxDirectionalLights)));
		}
		return Result<SceneRenderData>::Success(std::move(data));
	}
	catch (const std::exception& error)
	{
		return Result<SceneRenderData>::Failure(String("Cannot collect scene rendering data: ") + error.what());
	}
}

void Scene::CollectRendering(NodeId node, SceneRenderData& out) const
{
	if (!IsActive(node).value)
	{
		return;
	}
	const NodeSlot& slot = nodes[node.index];
	for (ComponentId id : slot.components)
	{
		if (!ContainsComponent(id) || !IsComponentActive(id).value)
		{
			continue;
		}
		const auto& object = components[id.index].object;
		if (const auto* box = dynamic_cast<const BoxRenderComponent*>(object.get()))
		{
			out.boxes.push_back({ComputeWorld(node), box->color, box->material});
		}
		if (const auto* light = dynamic_cast<const DirectionalLightComponent*>(object.get()))
		{
			const Mat4 world = ComputeWorld(node);
			out.lighting.directional.push_back(
			    {Normalized({-world.At(0, 2), -world.At(1, 2), -world.At(2, 2)}), light->Color(), light->Intensity()});
		}
		if (const auto* light = dynamic_cast<const PointLightComponent*>(object.get()))
		{
			const Mat4 world = ComputeWorld(node);
			out.lighting.point.push_back(
			    {{world.At(0, 3), world.At(1, 3), world.At(2, 3)}, light->Color(), light->Intensity(), light->Range()});
		}
	}
	for (NodeId child : slot.children)
	{
		CollectRendering(child, out);
	}
}
} // namespace Ursa
