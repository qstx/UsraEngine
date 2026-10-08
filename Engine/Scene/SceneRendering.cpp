#include <Core/Types.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Scene/Scene.hpp>
#include <Core/StringInterop.hpp>
#include <cmath>
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
	try
	{
		SceneRenderData data;
		data.viewProjection = viewProjection;
		data.cameraPosition = cameraPosition;
		data.lighting.ambientColor = ambientColor;
		data.lighting.ambientIntensity = ambientIntensity;
		for (const GameObject::Ptr& root : RootGameObjects())
		{
			CollectRendering(HandleOf(root.get()), data);
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

void Scene::CollectRendering(ObjectHandle node, SceneRenderData& out) const
{
	if (!Contains(node))
	{
		return;
	}
	GameObject* gameObject = objects[node.index].object;
	for (GameObject* current = gameObject; current; current = current->parent)
	{
		if (!current->activeSelf)
		{
			return;
		}
	}
	const Mat4 world = gameObject->WorldTransform().value;
	for (const auto& component : gameObject->components)
	{
		const ComponentId id = component->id;
		if (!ContainsComponent(id) || !IsComponentActive(id).value)
		{
			continue;
		}
		const auto* object = components[id.index].object;
		if (const auto* box = dynamic_cast<const BoxRenderComponent*>(object))
		{
			out.boxes.push_back({world, box->color, box->material});
		}
		if (const auto* meshRenderer = dynamic_cast<const MeshRendererComponent*>(object))
		{
			const ModelMeshPtr mesh = meshRenderer->Mesh();
			if (mesh)
			{
				out.boxes.push_back({world, {}, mesh->material, mesh});
			}
		}
		if (const auto* skinnedRenderer = dynamic_cast<const SkinnedMeshRendererComponent*>(object))
		{
			const ModelMeshPtr mesh = skinnedRenderer->Mesh();
			if (mesh)
			{
				Vector<Mat4> palette;
				auto owner = skinnedRenderer->GetGameObject();
				for (GameObject::Ptr current = owner; current; current = current->Parent())
				{
					const AnimatorComponent* animator = current->FindComponent<AnimatorComponent>();
					if (animator && animator->ModelGuid() == mesh->modelGuid &&
					    animator->SkeletonLocalFileId() == mesh->skeletonLocalFileId)
					{
						palette = animator->JointPalette();
						break;
					}
				}
				if (palette.empty() && mesh->skeleton)
				{
					Vector<Mat4> bindTransforms;
					bindTransforms.reserve(mesh->skeleton->joints.size());
					for (Ursa::Size jointIndex = 0; jointIndex < mesh->skeleton->joints.size(); ++jointIndex)
					{
						const SkeletonJoint& joint = mesh->skeleton->joints[jointIndex];
						const Mat4 global = joint.parentJointIndex < 0
						                        ? joint.bindLocalTransform.Matrix()
						                        : bindTransforms[static_cast<Ursa::Size>(joint.parentJointIndex)] *
						                              joint.bindLocalTransform.Matrix();
						bindTransforms.push_back(global);
						palette.push_back(global * joint.inverseBindMatrix);
					}
				}
				if (mesh->skeleton)
				{
					const Mat4 inverseMeshWorld = world.InverseAffine();
					for (Mat4& jointMatrix : palette)
					{
						jointMatrix = inverseMeshWorld * jointMatrix;
					}
				}
				out.boxes.push_back({world, {}, mesh->material, mesh, std::move(palette)});
			}
		}
		if (const auto* light = dynamic_cast<const DirectionalLightComponent*>(object))
		{
			out.lighting.directional.push_back(
			    {Normalized({-world.At(0, 2), -world.At(1, 2), -world.At(2, 2)}), light->Color(), light->Intensity()});
		}
		if (const auto* light = dynamic_cast<const PointLightComponent*>(object))
		{
			out.lighting.point.push_back(
			    {{world.At(0, 3), world.At(1, 3), world.At(2, 3)}, light->Color(), light->Intensity(), light->Range()});
		}
	}
	for (const auto& child : gameObject->children)
	{
		CollectRendering(HandleOf(child.get()), out);
	}
}
} // namespace Ursa
