#include <Core/Types.hpp>
#include <Animation/AnimatorComponent.hpp>
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
		if (const auto* renderer = dynamic_cast<const RendererComponent*>(object))
		{
			MeshPtr mesh;
			Vector<Mat4> palette;
			if (dynamic_cast<const MeshRendererComponent*>(renderer))
			{
				auto* filter = gameObject->FindComponent<MeshFilterComponent>();
				if (filter && (!filter->Id() || ContainsComponent(filter->Id())))
				{
					mesh = filter->SharedMesh();
				}
			}
			else if (const auto* skin = dynamic_cast<const SkinnedMeshRendererComponent*>(renderer))
			{
				mesh = skin->SharedMesh();
				if (mesh)
				{
					auto snapshot = mesh->Snapshot();
					if (!snapshot)
					{
						throw std::runtime_error("Skinned renderer Mesh was destroyed");
					}
					const auto& binding = snapshot->data;
					for (GameObject::Ptr current = skin->GetGameObject(); current; current = current->Parent())
					{
						const auto* animator = current->FindComponent<AnimatorComponent>();
						if (animator && animator->ModelGuid() == binding.skeletonReference.guid &&
						    animator->SkeletonLocalFileId() == binding.skeletonReference.localFileId)
						{
							palette = animator->JointPalette();
							break;
						}
					}
					if (palette.empty() && binding.skeleton)
					{
						Vector<Mat4> bindTransforms;
						for (const auto& joint : binding.skeleton->joints)
						{
							const Mat4 global =
							    joint.parentJointIndex < 0
							        ? joint.bindLocalTransform.Matrix()
							        : bindTransforms[Size(joint.parentJointIndex)] * joint.bindLocalTransform.Matrix();
							bindTransforms.push_back(global);
							palette.push_back(global * joint.inverseBindMatrix);
						}
					}
					const Mat4 inverseMeshWorld = world.InverseAffine();
					for (Mat4& joint : palette)
					{
						joint = inverseMeshWorld * joint;
					}
				}
			}
			if (mesh)
			{
				const auto snapshot = mesh->Snapshot();
				if (!snapshot)
				{
					throw std::runtime_error("Renderer Mesh was destroyed");
				}
				if (!snapshot->data.positions.empty() && !snapshot->data.indices.empty() &&
				    !snapshot->data.subMeshes.empty())
				{
					const auto materials = renderer->SharedMaterials();
					const Size draws = std::max(snapshot->data.subMeshes.size(), materials.size());
					for (Size slot = 0; slot < draws; ++slot)
					{
						const Size subMesh = std::min(slot, snapshot->data.subMeshes.size() - 1);
						if (!snapshot->data.subMeshes[subMesh].indexCount)
						{
							continue;
						}
						auto material = slot < materials.size() ? materials[slot] : nullptr;
						if (material && !material->IsValid())
						{
							throw std::runtime_error("Renderer material was destroyed");
						}
						out.boxes.push_back({world, {}, std::move(material), mesh, palette, snapshot, UInt32(subMesh)});
					}
				}
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
