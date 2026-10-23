#include <Rendering/Scene/RenderScene.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <cmath>

namespace Ursa::Rendering
{
namespace
{
Vec3 TransformPoint(const Mat4& matrix, Vec3 value)
{
	return {matrix.At(0, 0) * value.x + matrix.At(0, 1) * value.y + matrix.At(0, 2) * value.z + matrix.At(0, 3),
	        matrix.At(1, 0) * value.x + matrix.At(1, 1) * value.y + matrix.At(1, 2) * value.z + matrix.At(1, 3),
	        matrix.At(2, 0) * value.x + matrix.At(2, 1) * value.y + matrix.At(2, 2) * value.z + matrix.At(2, 3)};
}

void Include(Bounds& bounds, Vec3 value)
{
	if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
	{
		throw std::runtime_error("Rendering bounds overflowed or contain nonfinite coordinates");
	}
	if (bounds.empty)
	{
		bounds = {value, value, false};
		return;
	}
	bounds.minimum = {std::min(bounds.minimum.x, value.x), std::min(bounds.minimum.y, value.y),
	                  std::min(bounds.minimum.z, value.z)};
	bounds.maximum = {std::max(bounds.maximum.x, value.x), std::max(bounds.maximum.y, value.y),
	                  std::max(bounds.maximum.z, value.z)};
}

Bounds WorldBounds(const MeshSnapshot& snapshot, const Vector<Mat4>& palette, const Mat4& world)
{
	Bounds bounds;
	const auto& data = snapshot.data;
	// Transform actual deformed positions so an animation cannot leave the static bind-pose bounds.
	if (!palette.empty() && data.skinInfluences.size() == data.positions.size())
	{
		for (Size index = 0; index < data.positions.size(); ++index)
		{
			Vec3 position{};
			Float32 totalWeight = 0;
			const auto& influence = data.skinInfluences[index];
			for (Size joint = 0; joint < 4; ++joint)
			{
				if (influence.weights[joint] <= 0)
				{
					continue;
				}
				if (influence.jointIndices[joint] >= palette.size())
				{
					throw std::runtime_error("Skin influence is outside the rendering joint palette");
				}
				const Vec3 transformed = TransformPoint(palette[influence.jointIndices[joint]], data.positions[index]);
				position.x += transformed.x * influence.weights[joint];
				position.y += transformed.y * influence.weights[joint];
				position.z += transformed.z * influence.weights[joint];
				totalWeight += influence.weights[joint];
			}
			if (totalWeight == 0)
			{
				position = data.positions[index];
			}
			Include(bounds, TransformPoint(world, position));
		}
		return bounds;
	}
	if (!data.bounds.empty)
	{
		for (UInt32 corner = 0; corner < 8; ++corner)
		{
			Include(bounds, TransformPoint(world, {corner & 1 ? data.bounds.maximum.x : data.bounds.minimum.x,
			                                       corner & 2 ? data.bounds.maximum.y : data.bounds.minimum.y,
			                                       corner & 4 ? data.bounds.maximum.z : data.bounds.minimum.z}));
		}
	}
	return bounds;
}
} // namespace

Result<void> RenderScene::Capture(const Scene& scene, Float32 aspect, std::optional<CameraType> cameraFilter,
                                  std::optional<CameraData> cameraOverride, const BuiltinShaderResolver& resolveBuiltin)
{
	valid = false;
	objectCount = 0;
	cameras.clear();
	lighting.directional.clear();
	lighting.point.clear();
	lighting.ambientColor = scene.AmbientLightColor();
	lighting.ambientIntensity = scene.AmbientLightIntensity();
	++version;
	if (!std::isfinite(aspect) || aspect <= 0)
	{
		return Result<void>::Failure("RenderScene requires a finite positive camera aspect");
	}
	try
	{
		for (const auto& root : scene.rootObjects)
		{
			if (!scene.Contains(root) || scene.objects[scene.HandleOf(root.get()).index].pendingCreate)
			{
				continue;
			}
			auto result = Collect(scene, root, aspect, resolveBuiltin);
			if (!result)
			{
				objects.clear();
				cameras.clear();
				return result;
			}
		}
		if (cameraFilter)
		{
			cameras.erase(std::remove_if(cameras.begin(), cameras.end(), [&](const CameraData& camera)
			                             { return camera.settings.type != *cameraFilter; }),
			              cameras.end());
		}
		if (cameraOverride)
		{
			auto settingsValid = cameraOverride->settings.Validate();
			if (!settingsValid || !cameraOverride->identity ||
			    (cameraFilter && cameraOverride->settings.type != *cameraFilter) ||
			    std::any_of(cameras.begin(), cameras.end(),
			                [&](const CameraData& camera) { return camera.identity == cameraOverride->identity; }))
			{
				return Result<void>::Failure(settingsValid ? "RenderScene camera override is invalid or duplicated"
				                                           : settingsValid.error);
			}
			cameras.push_back(*cameraOverride);
		}
		std::sort(cameras.begin(), cameras.end(),
		          [](const CameraData& a, const CameraData& b)
		          {
			          return a.settings.depth != b.settings.depth
			                     ? a.settings.depth < b.settings.depth
			                     : std::lexicographical_compare(a.identity.bytes.begin(), a.identity.bytes.end(),
			                                                    b.identity.bytes.begin(), b.identity.bytes.end());
		          });
		if (lighting.point.size() > SceneLighting::maxPointLights)
		{
			return Result<void>::Failure(
			    "Active point light count " + ToUrsaString(std::to_string(lighting.point.size())) +
			    " exceeds renderer capacity " + ToUrsaString(std::to_string(SceneLighting::maxPointLights)));
		}
		if (lighting.directional.size() > SceneLighting::maxDirectionalLights)
		{
			return Result<void>::Failure(
			    "Active directional light count " + ToUrsaString(std::to_string(lighting.directional.size())) +
			    " exceeds renderer capacity " + ToUrsaString(std::to_string(SceneLighting::maxDirectionalLights)));
		}
		valid = true;
		objects.resize(objectCount);
		return {};
	}
	catch (const std::exception& error)
	{
		objects.clear();
		cameras.clear();
		return Result<void>::Failure(String("Cannot capture RenderScene: ") + error.what());
	}
}

Result<void> RenderScene::Collect(const Scene& scene, const GameObject::Ptr& object, Float32 aspect,
                                  const BuiltinShaderResolver& resolveBuiltin)
{
	auto active = object->ActiveInHierarchy();
	if (!active || !active.value)
	{
		return {};
	}
	auto transform = object->WorldTransform();
	auto layer = object->Layer();
	if (!transform || !layer)
	{
		return Result<void>::Failure("RenderScene object transform or layer is invalid");
	}
	const Mat4 world = transform.value;
	for (const auto& ownedComponent : object->components)
	{
		Component* component = ownedComponent.get();
		if (!component->Id() || !scene.ContainsComponent(component->Id()) ||
		    !scene.IsComponentActive(component->Id()).value)
		{
			continue;
		}
		if (auto* camera = dynamic_cast<CameraComponent*>(component))
		{
			auto settingsValid = camera->rendering.Validate();
			if (!settingsValid)
			{
				return settingsValid;
			}
			const auto& viewport = camera->rendering.viewport;
			const Float32 targetAspect = camera->rendering.targetWidth ? Float32(camera->rendering.targetWidth) /
			                                                                 Float32(camera->rendering.targetHeight)
			                                                           : aspect;
			const Mat4 view = world.InverseAffine();
			const Float32 cameraAspect = targetAspect * viewport.width / viewport.height;
			const Mat4 projection =
			    camera->orthographic
			        ? Mat4::Orthographic(camera->orthographicSize * 2.0f, cameraAspect, camera->nearPlane,
			                             camera->farPlane)
			        : Mat4::Perspective(camera->verticalFovRadians, cameraAspect, camera->nearPlane, camera->farPlane);
			CameraData frameCamera;
			frameCamera.identity = camera->PersistentId();
			frameCamera.view = view;
			frameCamera.projection = projection;
			frameCamera.viewProjection = projection * view;
			frameCamera.inverseViewProjection = world * projection.Inverse();
			frameCamera.position = {world.At(0, 3), world.At(1, 3), world.At(2, 3)};
			frameCamera.nearPlane = camera->nearPlane;
			frameCamera.farPlane = camera->farPlane;
			frameCamera.orthographic = camera->orthographic;
			frameCamera.settings = camera->rendering;
			cameras.push_back(std::move(frameCamera));
			if (const auto gameObject = camera->GetGameObject())
			{
				auto name = gameObject->Name();
				if (name)
				{
					cameras.back().debugName = std::move(name.value);
				}
			}
		}
		if (auto* renderer = dynamic_cast<RendererComponent*>(component))
		{
			MeshPtr mesh;
			auto& palette = scratchPalette;
			AnimatorComponent* poseAnimator = nullptr;
			Mat4 poseRootWorld = Mat4::Identity();
			palette.clear();
			if (dynamic_cast<MeshRendererComponent*>(renderer))
			{
				auto* filter = object->FindComponent<MeshFilterComponent>();
				if (filter)
				{
					mesh = filter->SharedMesh();
				}
			}
			else if (auto* skin = dynamic_cast<SkinnedMeshRendererComponent*>(renderer))
			{
				mesh = skin->SharedMesh();
				if (mesh)
				{
					auto snapshot = mesh->Snapshot();
					if (!snapshot)
					{
						return Result<void>::Failure("Skinned mesh was destroyed");
					}
					const auto& binding = snapshot->data;
					for (auto ancestor = object; ancestor; ancestor = ancestor->Parent())
					{
						auto* animator = ancestor->FindComponent<AnimatorComponent>();
						if (animator && animator->ModelGuid() == binding.skeletonReference.guid &&
						    animator->SkeletonLocalFileId() == binding.skeletonReference.localFileId)
						{
							poseAnimator = animator;
							auto animatorObject = animator->GetGameObject();
							auto animatorWorld = animatorObject ? animatorObject->WorldTransform()
							                                    : Result<Mat4>::Failure("Animator has no scene object");
							if (!animatorWorld)
							{
								return Result<void>::Failure("Cannot resolve Animator root transform: " +
								                             animatorWorld.error);
							}
							poseRootWorld = animatorWorld.value;
							palette = animator->JointPalette();
							break;
						}
					}
					if (binding.skeleton)
					{
						Vector<Mat4> bindTransforms;
						const Vector<Mat4>* jointTransforms = nullptr;
						if (poseAnimator)
						{
							jointTransforms = &poseAnimator->JointGlobalTransforms();
						}
						else
						{
							auto modelRoot = object;
							while (modelRoot->Parent())
							{
								modelRoot = modelRoot->Parent();
							}
							auto rootTransform = modelRoot->WorldTransform();
							if (!rootTransform)
							{
								return Result<void>::Failure("Cannot resolve skin model root transform: " +
								                             rootTransform.error);
							}
							poseRootWorld = rootTransform.value;
							for (const auto& joint : binding.skeleton->joints)
							{
								const Mat4 global = joint.parentJointIndex < 0
								                        ? joint.bindLocalTransform.Matrix()
								                        : bindTransforms[Size(joint.parentJointIndex)] *
								                              joint.bindLocalTransform.Matrix();
								bindTransforms.push_back(global);
							}
							jointTransforms = &bindTransforms;
						}
						if (!binding.skinInverseBindMatrices.empty() &&
						    binding.skinInverseBindMatrices.size() != (binding.skinJointIndices.empty()
						                                                   ? binding.skeleton->joints.size()
						                                                   : binding.skinJointIndices.size()))
						{
							return Result<void>::Failure(
							    "Skinned mesh inverse-bind data does not match its joint binding");
						}
						const Size bindingCount = binding.skinJointIndices.empty() ? jointTransforms->size()
						                                                           : binding.skinJointIndices.size();
						palette.resize(bindingCount);
						for (Size joint = 0; joint < bindingCount; ++joint)
						{
							const Size poseJoint =
							    binding.skinJointIndices.empty() ? joint : binding.skinJointIndices[joint];
							if (poseJoint >= jointTransforms->size())
							{
								return Result<void>::Failure(
								    "Skinned mesh binding refers outside the active node pose");
							}
							const Mat4& inverseBind = binding.skinInverseBindMatrices.empty()
							                              ? binding.skeleton->joints[poseJoint].inverseBindMatrix
							                              : binding.skinInverseBindMatrices[joint];
							palette[joint] = (*jointTransforms)[poseJoint] * inverseBind;
						}
					}
					const Mat4 inverse = world.InverseAffine();
					for (Mat4& joint : palette)
					{
						joint = inverse * poseRootWorld * joint;
					}
				}
			}
			if (mesh)
			{
				auto snapshot = mesh->Snapshot();
				if (!snapshot)
				{
					return Result<void>::Failure("Renderer mesh was destroyed");
				}
				const auto& materials = renderer->materials;
				const auto& data = snapshot->data;
				if (data.positions.empty() || data.indices.empty() || data.subMeshes.empty())
				{
					continue;
				}
				const Bounds bounds = WorldBounds(*snapshot, palette, world);
				for (Size slot = 0, count = std::max(data.subMeshes.size(), materials.size()); slot < count; ++slot)
				{
					const UInt32 subMesh = UInt32(std::min(slot, data.subMeshes.size() - 1));
					if (data.subMeshes[subMesh].indexCount == 0)
					{
						continue;
					}
					auto material = slot < materials.size() ? materials[slot] : nullptr;
					if (material && !material->IsValid())
					{
						return Result<void>::Failure("Renderer material was destroyed");
					}
					if (objectCount == objects.size())
					{
						objects.emplace_back();
					}
					auto& output = objects[objectCount++];
					output.objectId = object->Id();
					output.rendererId = renderer->PersistentId();
					output.layer = layer.value;
					output.world = world;
					output.worldBounds = bounds;
					output.mesh = mesh;
					output.meshSnapshot = snapshot;
					output.material = material;
					output.shader = material ? material->Shader() : nullptr;
					if (resolveBuiltin && material && material->IsShaderBacked() &&
					    (material->ShaderId() == Shader::BuiltinUnlitShaderId() ||
					     material->ShaderId() == Shader::BuiltinPbrShaderId()) &&
					    (!output.shader || output.shader->subShaders.empty()))
					{
						auto resolved = resolveBuiltin(material->ShaderId());
						if (!resolved)
						{
							return Result<void>::Failure(resolved.error);
						}
						output.shader = std::move(resolved.value);
					}
					output.materialVersion = material ? material->Version() : 0;
					output.parameters = material ? material->Parameters() : MaterialParameters{};
					output.jointPalette = palette;
					output.subMesh = subMesh;
					output.castShadows = renderer->CastShadows();
					output.receiveShadows = renderer->ReceiveShadows();
				}
			}
		}
		if (auto* directional = dynamic_cast<DirectionalLightComponent*>(component))
		{
			DirectionalLightData light;
			light.direction = Normalized({-world.At(0, 2), -world.At(1, 2), -world.At(2, 2)});
			light.color = directional->Color();
			light.intensity = directional->Intensity();
			light.castsShadows = directional->CastsShadows();
			light.shadowStrength = directional->ShadowStrength();
			light.shadowBias = directional->ShadowBias();
			light.shadowNormalBias = directional->ShadowNormalBias();
			light.softShadows = directional->SoftShadows();
			light.identity = directional->PersistentId();
			light.layer = layer.value;
			lighting.directional.push_back(std::move(light));
		}
		if (auto* point = dynamic_cast<PointLightComponent*>(component))
		{
			PointLightData light;
			light.position = {world.At(0, 3), world.At(1, 3), world.At(2, 3)};
			light.color = point->Color();
			light.intensity = point->Intensity();
			light.range = point->Range();
			light.identity = point->PersistentId();
			light.layer = layer.value;
			lighting.point.push_back(std::move(light));
		}
	}
	for (const auto& child : object->children)
	{
		auto collected = Collect(scene, child, aspect, resolveBuiltin);
		if (!collected)
		{
			return collected;
		}
	}
	return {};
}
} // namespace Ursa::Rendering
