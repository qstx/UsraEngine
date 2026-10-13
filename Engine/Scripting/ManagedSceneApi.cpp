#include <Core/StringInterop.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <cstring>
#include <cmath>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace Ursa::Scripting
{
namespace
{
std::atomic<UInt64> nextRenderSessionId{1};

bool MatchesRenderToken(const Rendering::RenderContextToken& left, const Rendering::RenderContextToken& right)
{
	return left.sessionId == right.sessionId && left.frameId == right.frameId && left.index == right.index &&
	       left.generation == right.generation;
}
} // namespace

ManagedSceneApi::ManagedSceneApi(Scene& scene, ResourceSession* resources,
                                 std::function<void(const String&)> setWindowTitle)
    : scene(scene), resources(resources), setWindowTitle(std::move(setWindowTitle))
{
	renderSessionId = nextRenderSessionId.fetch_add(1, std::memory_order_relaxed);
	if (renderSessionId == 0)
	{
		throw std::runtime_error("Render session identity space is exhausted");
	}
	auto registry = ManagedHandleRegistry::Create();
	if (!registry)
	{
		throw std::runtime_error(ToStdString(registry.error));
	}
	handles = std::move(registry.value);
	auto registered = RegisterBuiltinComponents(componentTypes);
	if (!registered)
	{
		throw std::runtime_error(ToStdString(registered.error));
	}
}

Result<ManagedHandle> ManagedSceneApi::Register(const GameObject::Ptr& object)
{
	if (!scene.Contains(object))
	{
		return Result<ManagedHandle>::Failure("Cannot expose a GameObject outside this Scene to managed code");
	}
	auto handle = handles->Register(object.get(), ManagedHandleKind::GameObject);
	if (handle)
	{
		objects[handle.value.index] = object;
	}
	return handle;
}

NativeApiV6 ManagedSceneApi::Bind(NativeApiV6 api)
{
	api.sceneContext = this;
	api.createGameObject = &ManagedSceneApi::CreateGameObject;
	api.getGameObjectName = &ManagedSceneApi::GetGameObjectName;
	api.setGameObjectName = &ManagedSceneApi::SetGameObjectName;
	api.getGameObjectActive = &ManagedSceneApi::GetGameObjectActive;
	api.setGameObjectActive = &ManagedSceneApi::SetGameObjectActive;
	api.getGameObjectParent = &ManagedSceneApi::GetGameObjectParent;
	api.setGameObjectParent = &ManagedSceneApi::SetGameObjectParent;
	api.getLocalTransform = &ManagedSceneApi::GetLocalTransform;
	api.setLocalTransform = &ManagedSceneApi::SetLocalTransform;
	api.getInputState = &ManagedSceneApi::GetInputState;
	api.getTimeState = &ManagedSceneApi::GetTimeState;
	api.addComponent = &ManagedSceneApi::AddComponent;
	api.getComponentCount = &ManagedSceneApi::GetComponentCount;
	api.getComponentAt = &ManagedSceneApi::GetComponentAt;
	api.getComponentType = &ManagedSceneApi::GetComponentType;
	api.getComponentOwner = &ManagedSceneApi::GetComponentOwner;
	api.getComponentEnabled = &ManagedSceneApi::GetComponentEnabled;
	api.setComponentEnabled = &ManagedSceneApi::SetComponentEnabled;
	api.isComponentActive = &ManagedSceneApi::IsComponentActive;
	api.removeComponent = &ManagedSceneApi::RemoveComponent;
	api.loadTexture = &ManagedSceneApi::LoadTexture;
	api.loadMaterial = &ManagedSceneApi::LoadMaterial;
	api.addBehaviour = &ManagedSceneApi::AddBehaviour;
	api.getGameObjectPersistentId = &ManagedSceneApi::GetGameObjectPersistentId;
	api.resolveGameObjectPersistentId = &ManagedSceneApi::ResolveGameObjectPersistentId;
	api.getComponentPersistentId = &ManagedSceneApi::GetComponentPersistentId;
	api.resolveComponentPersistentId = &ManagedSceneApi::ResolveComponentPersistentId;
	api.destroyGameObject = &ManagedSceneApi::DestroyGameObject;
	api.getWorldPosition = &ManagedSceneApi::GetWorldPosition;
	api.setWorldPosition = &ManagedSceneApi::SetWorldPosition;
	api.getCameraProjection = &ManagedSceneApi::GetCameraProjection;
	api.setCameraProjection = &ManagedSceneApi::SetCameraProjection;
	api.setWindowTitle = &ManagedSceneApi::SetWindowTitle;
	api.resourceRequest = &ManagedSceneApi::ResourceRequest;
	api.submitRenderBatch = &ManagedSceneApi::SubmitRenderBatch;
	return api;
}

Int32 ManagedSceneApi::SubmitRenderBatch(void* context, const UInt8* tokenBytes, UInt64 tokenByteCount,
                                         const UInt8* batch, UInt64 batchByteCount)
{
	try
	{
		if (!context)
		{
			return 1;
		}
		auto* api = FromContext(context);
		if (api->ownerThread != std::this_thread::get_id())
		{
			return 5;
		}
		if (!tokenBytes || tokenByteCount != sizeof(Rendering::RenderContextToken) || !batch)
		{
			return 1;
		}
		Rendering::RenderContextToken token;
		std::memcpy(&token, tokenBytes, sizeof(token));
		if (!api->activeRenderToken || token.sessionId == 0 || token.frameId == 0 || token.index == 0 ||
		    token.generation == 0 || token.sessionId != api->activeRenderToken->sessionId ||
		    token.frameId != api->activeRenderToken->frameId || token.index != api->activeRenderToken->index ||
		    token.generation != api->activeRenderToken->generation)
		{
			return 6;
		}
		auto valid = Rendering::ValidateRenderCommandBatch(batch, static_cast<Size>(batchByteCount));
		if (!valid)
		{
			return 7;
		}
		Rendering::RenderCommandBatchHeader header;
		std::memcpy(&header, batch, sizeof(header));
		Vector<std::shared_ptr<void>> retained;
		Size offset = sizeof(header);
		for (UInt32 index = 0; index < header.commandCount; ++index)
		{
			Rendering::RenderCommandRecordHeader record;
			std::memcpy(&record, batch + offset, sizeof(record));
			if (record.opcode == Rendering::RenderCommandOpcode::SetRenderTarget &&
			    record.byteSize == sizeof(record) + sizeof(ManagedHandle))
			{
				ManagedHandle targetHandle{};
				std::memcpy(&targetHandle, batch + offset + sizeof(record), sizeof(targetHandle));
				auto target = api->ResolveRenderTexture(targetHandle);
				if (!target)
				{
					return 8;
				}
				retained.push_back(target.value);
			}
			else if (record.opcode == Rendering::RenderCommandOpcode::DrawRendererList)
			{
				Rendering::RenderContextToken listToken;
				if (record.byteSize < sizeof(record) + sizeof(listToken))
				{
					return 7;
				}
				std::memcpy(&listToken, batch + offset + sizeof(record), sizeof(listToken));
				if (!api->activeRenderToken || !MatchesRenderToken(listToken, *api->activeRenderToken))
				{
					return 6;
				}
			}
			else if (record.opcode == Rendering::RenderCommandOpcode::DrawMesh)
			{
				constexpr Size drawMeshPayloadBytes = sizeof(ManagedHandle) * 2 + sizeof(Float32) * 16 + sizeof(UInt32);
				if (record.byteSize < sizeof(record) + drawMeshPayloadBytes)
				{
					return 7;
				}
				ManagedHandle meshHandle{}, materialHandle{};
				std::memcpy(&meshHandle, batch + offset + sizeof(record), sizeof(meshHandle));
				std::memcpy(&materialHandle, batch + offset + sizeof(record) + sizeof(meshHandle),
				            sizeof(materialHandle));
				auto mesh = api->ResolveMesh(meshHandle);
				auto material = api->ResolveMaterial(materialHandle);
				if (!mesh || !material || !mesh.value || !material.value)
				{
					return 8;
				}
				retained.push_back(mesh.value);
				retained.push_back(material.value);
			}
			else if (record.opcode == Rendering::RenderCommandOpcode::CopyTexture ||
			         record.opcode == Rendering::RenderCommandOpcode::Blit)
			{
				ManagedHandle sourceHandle{}, destinationHandle{};
				std::memcpy(&sourceHandle, batch + offset + sizeof(record), sizeof(sourceHandle));
				std::memcpy(&destinationHandle, batch + offset + sizeof(record) + sizeof(sourceHandle),
				            sizeof(destinationHandle));
				auto source = api->ResolveRenderTexture(sourceHandle);
				auto destination = api->ResolveRenderTexture(destinationHandle);
				if (!source || !destination)
				{
					return 8;
				}
				retained.push_back(source.value);
				retained.push_back(destination.value);
				if (record.opcode == Rendering::RenderCommandOpcode::Blit)
				{
					ManagedHandle materialHandle{};
					std::memcpy(&materialHandle, batch + offset + sizeof(record) + sizeof(sourceHandle) * 2,
					            sizeof(materialHandle));
					auto material = api->ResolveMaterial(materialHandle);
					if (!material || !material.value)
					{
						return 8;
					}
					retained.push_back(material.value);
				}
			}
			else if (record.opcode == Rendering::RenderCommandOpcode::CopyBuffer)
			{
				ManagedHandle sourceHandle{}, destinationHandle{};
				std::memcpy(&sourceHandle, batch + offset + sizeof(record), sizeof(sourceHandle));
				std::memcpy(&destinationHandle, batch + offset + sizeof(record) + sizeof(sourceHandle),
				            sizeof(destinationHandle));
				auto source = api->ResolveGraphicsBuffer(sourceHandle);
				auto destination = api->ResolveGraphicsBuffer(destinationHandle);
				if (!source || !destination)
				{
					return 8;
				}
				retained.push_back(source.value);
				retained.push_back(destination.value);
			}
			else if (record.opcode == Rendering::RenderCommandOpcode::SetGlobalParameters)
			{
				UInt32 kind = 0, nameBytes = 0;
				std::memcpy(&kind, batch + offset + sizeof(record), sizeof(kind));
				std::memcpy(&nameBytes, batch + offset + sizeof(record) + sizeof(kind), sizeof(nameBytes));
				if (kind == 3)
				{
					ManagedHandle textureHandle{};
					std::memcpy(&textureHandle,
					            batch + offset + sizeof(record) + sizeof(kind) + sizeof(nameBytes) + nameBytes,
					            sizeof(textureHandle));
					auto texture = api->ResolveRenderTexture(textureHandle);
					if (!texture)
					{
						return 8;
					}
					retained.push_back(texture.value);
				}
			}
			offset += record.byteSize;
		}
		api->renderBatches.emplace_back(batch, batch + static_cast<Size>(batchByteCount));
		api->renderBatchResources.push_back(std::move(retained));
		++api->acceptedRenderBatchCount;
		return 0;
	}
	catch (...)
	{
		return 8;
	}
}

ManagedSceneApi* ManagedSceneApi::FromContext(void* context)
{
	return static_cast<ManagedSceneApi*>(context);
}

Int32 ManagedSceneApi::CreateGameObject(void* context, const UInt8* name, UInt64 length, ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !output || (!name && length != 0) || length > 256)
	{
		return 1;
	}
	try
	{
		const String objectName = length == 0 ? String{} : String(reinterpret_cast<const char*>(name), length);
		auto object = api->scene.CreateGameObject(objectName);
		if (!object)
		{
			return 2;
		}
		auto handle = api->Register(object.value);
		if (!handle)
		{
			api->scene.DestroyGameObject(object.value);
			return 3;
		}
		*output = handle.value;
		return 0;
	}
	catch (...)
	{
		return 4;
	}
}

Int32 ManagedSceneApi::GetGameObjectName(void* context, ManagedHandle object, UInt8* output, UInt64 capacity,
                                         UInt64* required)
{
	auto* api = FromContext(context);
	if (!api || !required || (!output && capacity != 0))
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	auto name = resolved.value->Name();
	if (!name)
	{
		return 3;
	}
	*required = name.value.size();
	if (capacity < name.value.size())
	{
		return 4;
	}
	if (!name.value.empty())
	{
		std::memcpy(output, name.value.data(), name.value.size());
	}
	return 0;
}

Int32 ManagedSceneApi::SetGameObjectName(void* context, ManagedHandle object, const UInt8* name, UInt64 length)
{
	auto* api = FromContext(context);
	if (!api || (!name && length != 0) || length > 256)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	const String value = length == 0 ? String{} : String(reinterpret_cast<const char*>(name), length);
	return resolved.value->SetName(value) ? 0 : 3;
}

Int32 ManagedSceneApi::SetGameObjectActive(void* context, ManagedHandle object, UInt32 active)
{
	auto* api = FromContext(context);
	if (!api || active > 1)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	return api->scene.SetActive(resolved.value, active != 0) ? 0 : 3;
}

Int32 ManagedSceneApi::GetGameObjectActive(void* context, ManagedHandle object, UInt32* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	auto active = api->scene.ActiveSelf(resolved.value);
	if (!active)
	{
		return 3;
	}
	*output = active.value ? 1u : 0u;
	return 0;
}

Int32 ManagedSceneApi::GetGameObjectParent(void* context, ManagedHandle object, ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	const auto parent = resolved.value->Parent();
	if (!parent)
	{
		*output = {};
		return 0;
	}
	auto handle = api->Register(parent);
	if (!handle)
	{
		return 3;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::SetGameObjectParent(void* context, ManagedHandle object, ManagedHandle parent)
{
	auto* api = FromContext(context);
	if (!api)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	GameObject::Ptr parentObject;
	if (parent.sessionId != 0)
	{
		auto resolvedParent = api->Resolve(parent);
		if (!resolvedParent)
		{
			return 3;
		}
		parentObject = std::move(resolvedParent.value);
	}
	return api->scene.SetParent(resolved.value, std::move(parentObject)) ? 0 : 4;
}

Int32 ManagedSceneApi::DestroyGameObject(void* context, ManagedHandle object)
{
	auto* api = FromContext(context);
	if (!api)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	return api->scene.DestroyGameObject(resolved.value) ? 0 : 3;
}

Int32 ManagedSceneApi::GetWorldPosition(void* context, ManagedHandle object, Float32* output, UInt64 count)
{
	auto* api = FromContext(context);
	if (!api || !output || count != 3)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	auto world = resolved.value->WorldTransform();
	if (!world)
	{
		return 3;
	}
	for (int row = 0; row < 3; ++row)
	{
		output[row] = world.value.At(row, 3);
	}
	return 0;
}

Int32 ManagedSceneApi::SetWorldPosition(void* context, ManagedHandle object, const Float32* values, UInt64 count)
{
	auto* api = FromContext(context);
	if (!api || !values || count != 3)
	{
		return 1;
	}
	for (int index = 0; index < 3; ++index)
	{
		if (!std::isfinite(values[index]))
		{
			return 1;
		}
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	auto local = resolved.value->GetLocalTransform();
	if (!local)
	{
		return 3;
	}
	Vec3 position{values[0], values[1], values[2]};
	if (auto parent = resolved.value->Parent())
	{
		auto world = parent->WorldTransform();
		if (!world)
		{
			return 3;
		}
		try
		{
			const Mat4 inverse = world.value.InverseAffine();
			position = {inverse.At(0, 0) * values[0] + inverse.At(0, 1) * values[1] + inverse.At(0, 2) * values[2] +
			                inverse.At(0, 3),
			            inverse.At(1, 0) * values[0] + inverse.At(1, 1) * values[1] + inverse.At(1, 2) * values[2] +
			                inverse.At(1, 3),
			            inverse.At(2, 0) * values[0] + inverse.At(2, 1) * values[1] + inverse.At(2, 2) * values[2] +
			                inverse.At(2, 3)};
		}
		catch (const std::exception&)
		{
			return 4;
		}
	}
	local.value.position = position;
	return resolved.value->SetLocalTransform(local.value) ? 0 : 5;
}

Int32 ManagedSceneApi::GetCameraProjection(void* context, ManagedHandle handle, Float32* output, UInt64 count)
{
	auto* api = FromContext(context);
	if (!api || !output || count != 3)
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(handle);
	if (!resolved)
	{
		return 2;
	}
	auto* camera = dynamic_cast<CameraComponent*>(resolved.value);
	if (!camera)
	{
		return 3;
	}
	output[0] = camera->verticalFovRadians;
	output[1] = camera->nearPlane;
	output[2] = camera->farPlane;
	return 0;
}

Int32 ManagedSceneApi::SetCameraProjection(void* context, ManagedHandle handle, const Float32* values, UInt64 count)
{
	auto* api = FromContext(context);
	if (!api || !values || count != 3 || !std::isfinite(values[0]) || !std::isfinite(values[1]) ||
	    !std::isfinite(values[2]) || values[0] <= 0.0f || values[0] >= 3.14159265f || values[1] <= 0.0f ||
	    values[2] <= values[1])
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(handle);
	if (!resolved)
	{
		return 2;
	}
	auto* camera = dynamic_cast<CameraComponent*>(resolved.value);
	if (!camera)
	{
		return 3;
	}
	camera->verticalFovRadians = values[0];
	camera->nearPlane = values[1];
	camera->farPlane = values[2];
	return 0;
}

Int32 ManagedSceneApi::SetWindowTitle(void* context, const UInt8* title, UInt64 length)
{
	auto* api = FromContext(context);
	if (!api || (!title && length != 0) || length > 1024)
	{
		return 1;
	}
	if (!api->setWindowTitle)
	{
		return 2;
	}
	const String value =
	    length == 0 ? String{} : String(reinterpret_cast<const char*>(title), static_cast<Size>(length));
	if (value.find('\0') != String::npos)
	{
		return 1;
	}
	try
	{
		api->setWindowTitle(value);
		return 0;
	}
	catch (...)
	{
		return 3;
	}
}

Int32 ManagedSceneApi::GetLocalTransform(void* context, ManagedHandle object, Float32* output, UInt64 count)
{
	auto* api = FromContext(context);
	if (!api || !output || count < 10)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	auto transform = api->scene.GetLocalTransform(resolved.value);
	if (!transform)
	{
		return 3;
	}
	output[0] = transform.value.position.x;
	output[1] = transform.value.position.y;
	output[2] = transform.value.position.z;
	output[3] = transform.value.rotation.x;
	output[4] = transform.value.rotation.y;
	output[5] = transform.value.rotation.z;
	output[6] = transform.value.rotation.w;
	output[7] = transform.value.scale.x;
	output[8] = transform.value.scale.y;
	output[9] = transform.value.scale.z;
	return 0;
}

Int32 ManagedSceneApi::SetLocalTransform(void* context, ManagedHandle object, const Float32* values, UInt64 count)
{
	auto* api = FromContext(context);
	if (!api || !values || count < 10)
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	if (!resolved)
	{
		return 2;
	}
	LocalTransform transform;
	transform.position = {values[0], values[1], values[2]};
	transform.rotation = {values[3], values[4], values[5], values[6]};
	transform.scale = {values[7], values[8], values[9]};
	return api->scene.SetLocalTransform(resolved.value, transform) ? 0 : 3;
}

Int32 ManagedSceneApi::GetInputState(void* context, UInt32 key, UInt32 phase, UInt32* output)
{
	const auto* api = FromContext(context);
	if (!api || !api->frameContext || !output || key >= FrameInput::keyCount || phase > 2)
	{
		return 1;
	}
	const Key keyCode = static_cast<Key>(key);
	if (keyCode == Key::Unknown)
	{
		*output = 0;
		return 0;
	}
	const FrameInput& input = api->frameContext->input;
	*output = phase == 0
	              ? (input.KeyDown(keyCode) ? 1u : 0u)
	              : (phase == 1 ? (input.KeyPressed(keyCode) ? 1u : 0u) : (input.KeyReleased(keyCode) ? 1u : 0u));
	return 0;
}

Int32 ManagedSceneApi::GetTimeState(void* context, Float32* delta, Float32* fixedDelta, Float64* time,
                                    Float64* fixedTime, UInt64* frame)
{
	const auto* api = FromContext(context);
	if (!api || !api->frameContext || !delta || !fixedDelta || !time || !fixedTime || !frame)
	{
		return 1;
	}
	*delta = api->frameContext->deltaSeconds;
	*fixedDelta = api->frameContext->fixedDeltaSeconds;
	*time = api->frameContext->elapsedSeconds;
	*fixedTime = api->frameContext->fixedElapsedSeconds;
	*frame = api->frameContext->frameIndex;
	return 0;
}

Int32 ManagedSceneApi::AddComponent(void* context, ManagedHandle object, const UInt8* type, UInt64 length,
                                    ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !type || length == 0 || length > 256 || !output)
	{
		return 1;
	}
	auto owner = api->Resolve(object);
	if (!owner)
	{
		return 2;
	}
	const String typeName(reinterpret_cast<const char*>(type), static_cast<Size>(length));
	Result<ComponentId> added;
	if (typeName == "ursa.Camera")
	{
		added = api->scene.AddComponent<CameraComponent>(owner.value);
	}
	else if (typeName == "ursa.MeshFilter")
	{
		added = api->scene.AddComponent<MeshFilterComponent>(owner.value);
	}
	else if (typeName == "ursa.MeshRenderer")
	{
		added = api->scene.AddComponent<MeshRendererComponent>(owner.value);
	}
	else if (typeName == "ursa.SkinnedMeshRenderer")
	{
		added = api->scene.AddComponent<SkinnedMeshRendererComponent>(owner.value);
	}
	else if (typeName == "ursa.Animator")
	{
		added = api->scene.AddComponent<AnimatorComponent>(owner.value);
	}
	else if (typeName == "ursa.DirectionalLight")
	{
		added = api->scene.AddComponent<DirectionalLightComponent>(owner.value);
	}
	else if (typeName == "ursa.PointLight")
	{
		added = api->scene.AddComponent<PointLightComponent>(owner.value);
	}
	else
	{
		return 3;
	}
	if (!added)
	{
		return 4;
	}
	auto handle = api->RegisterComponent(api->scene.GetComponent(added.value), owner.value);
	if (!handle)
	{
		api->scene.RemoveComponent(added.value);
		return 5;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::GetComponentCount(void* context, ManagedHandle object, UInt32* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto owner = api->Resolve(object);
	if (!owner)
	{
		return 2;
	}
	auto components = api->scene.ComponentsOf(owner.value);
	if (!components || components.value.size() > std::numeric_limits<UInt32>::max())
	{
		return 3;
	}
	*output = static_cast<UInt32>(components.value.size());
	return 0;
}

Int32 ManagedSceneApi::GetComponentAt(void* context, ManagedHandle object, UInt32 index, ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto owner = api->Resolve(object);
	if (!owner)
	{
		return 2;
	}
	auto components = api->scene.ComponentsOf(owner.value);
	if (!components || index >= components.value.size())
	{
		return 3;
	}
	auto handle = api->RegisterComponent(api->scene.GetComponent(components.value[index]), owner.value);
	if (!handle)
	{
		return 4;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::GetComponentType(void* context, ManagedHandle component, UInt8* output, UInt64 capacity,
                                        UInt64* required)
{
	auto* api = FromContext(context);
	if (!api || !required || (!output && capacity != 0))
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	if (!resolved)
	{
		return 2;
	}
	String typeName;
	if (const auto* behaviour = dynamic_cast<const ScriptBehaviourComponent*>(resolved.value))
	{
		typeName = behaviour->TypeName();
	}
	else
	{
		auto codec = api->componentTypes.FindFor(*resolved.value);
		typeName = codec ? codec.value->type : resolved.value->SerializationType();
		if (typeName.empty())
		{
			typeName = "ursa.Unexposed";
		}
	}
	*required = typeName.size();
	if (capacity < typeName.size())
	{
		return 4;
	}
	if (!typeName.empty())
	{
		std::memcpy(output, typeName.data(), typeName.size());
	}
	return 0;
}

Int32 ManagedSceneApi::GetComponentOwner(void* context, ManagedHandle component, ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	if (!resolved)
	{
		return 2;
	}
	const auto ownerEntry = api->componentOwners.find(component.index);
	if (ownerEntry == api->componentOwners.end())
	{
		return 3;
	}
	auto owner = ownerEntry->second.lock();
	if (!owner)
	{
		return 4;
	}
	auto handle = api->Register(owner);
	if (!handle)
	{
		return 5;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::GetComponentEnabled(void* context, ManagedHandle component, UInt32* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	if (!resolved)
	{
		return 2;
	}
	auto enabled = api->scene.IsComponentEnabled(resolved.value->Id());
	if (!enabled)
	{
		return 3;
	}
	*output = enabled.value ? 1u : 0u;
	return 0;
}

Int32 ManagedSceneApi::SetComponentEnabled(void* context, ManagedHandle component, UInt32 enabled)
{
	auto* api = FromContext(context);
	if (!api || enabled > 1)
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	if (!resolved)
	{
		return 2;
	}
	return api->scene.SetComponentEnabled(resolved.value->Id(), enabled != 0) ? 0 : 3;
}

Int32 ManagedSceneApi::IsComponentActive(void* context, ManagedHandle component, UInt32* output)
{
	auto* api = FromContext(context);
	if (!api || !output)
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	if (!resolved)
	{
		return 2;
	}
	auto active = api->scene.IsComponentActive(resolved.value->Id());
	if (!active)
	{
		return 3;
	}
	*output = active.value ? 1u : 0u;
	return 0;
}

Int32 ManagedSceneApi::RemoveComponent(void* context, ManagedHandle component)
{
	auto* api = FromContext(context);
	if (!api)
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	if (!resolved)
	{
		return 2;
	}
	return api->scene.RemoveComponent(resolved.value->Id()) ? 0 : 3;
}

Int32 ManagedSceneApi::LoadTexture(void* context, const UInt8* guidBytes, UInt64 guidLength, UInt64 localFileId,
                                   ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !api->resources || !guidBytes || guidLength != 36 || !output)
	{
		return 1;
	}
	auto guid = ResourceId::Parse(String(reinterpret_cast<const char*>(guidBytes), static_cast<Size>(guidLength)));
	if (!guid)
	{
		return 2;
	}
	auto texture = api->resources->LoadTexture(guid.value, localFileId);
	if (!texture)
	{
		return 3;
	}
	auto handle = api->handles->Register(const_cast<MaterialTexture*>(texture.value.get()), ManagedHandleKind::Asset);
	if (!handle)
	{
		return 4;
	}
	*output = handle.value;
	api->ownedTextures[handle.value.index] = texture.value;
	return 0;
}

Int32 ManagedSceneApi::LoadMaterial(void* context, const UInt8* guidBytes, UInt64 guidLength, UInt64 localFileId,
                                    ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !api->resources || !guidBytes || guidLength != 36 || !output)
	{
		return 1;
	}
	auto guid = ResourceId::Parse(String(reinterpret_cast<const char*>(guidBytes), static_cast<Size>(guidLength)));
	if (!guid)
	{
		return 2;
	}
	auto material = api->resources->LoadMaterial(guid.value, localFileId);
	if (!material)
	{
		return 3;
	}
	auto handle = api->RegisterMaterial(material.value);
	if (!handle)
	{
		return 4;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::AddBehaviour(void* context, ManagedHandle object, const UInt8* typeNameBytes,
                                    UInt64 typeNameLength, UInt64 instanceToken, ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !api->behaviourRuntime || !typeNameBytes || typeNameLength == 0 || typeNameLength > 4096 || !output)
	{
		return 1;
	}
	auto owner = api->Resolve(object);
	if (!owner)
	{
		return 2;
	}
	const String typeName(reinterpret_cast<const char*>(typeNameBytes), static_cast<Size>(typeNameLength));
	auto added = instanceToken == 0 ? api->behaviourRuntime->CreateAndAdd(owner.value, typeName)
	                                : api->behaviourRuntime->AddExisting(owner.value, typeName, instanceToken);
	if (!added)
	{
		return 3;
	}
	auto cached = api->behaviourRuntime->BoundHandle(instanceToken);
	auto handle = cached ? cached : api->RegisterComponent(api->scene.GetComponent(added.value), owner.value);
	if (!handle)
	{
		api->scene.RemoveComponent(added.value);
		return 4;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::GetGameObjectPersistentId(void* context, ManagedHandle object, UInt8* output, UInt64 capacity,
                                                 UInt64* required)
{
	auto* api = FromContext(context);
	if (!api || !required || (!output && capacity != 0))
	{
		return 1;
	}
	auto resolved = api->Resolve(object);
	auto id = resolved ? api->scene.PersistentId(resolved.value) : Result<ResourceId>::Failure(resolved.error);
	if (!id)
	{
		return 2;
	}
	const String text = id.value.ToString();
	*required = text.size();
	if (capacity < text.size())
	{
		return 4;
	}
	std::memcpy(output, text.data(), text.size());
	return 0;
}

Int32 ManagedSceneApi::ResolveGameObjectPersistentId(void* context, const UInt8* idBytes, UInt64 length,
                                                     ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !idBytes || length != 36 || !output)
	{
		return 1;
	}
	auto id = ResourceId::Parse(String(reinterpret_cast<const char*>(idBytes), static_cast<Size>(length)));
	if (!id)
	{
		return 2;
	}
	auto object = api->scene.GameObjectById(id.value);
	if (!object)
	{
		return 3;
	}
	auto handle = api->Register(object.value);
	if (!handle)
	{
		return 4;
	}
	*output = handle.value;
	return 0;
}

Int32 ManagedSceneApi::GetComponentPersistentId(void* context, ManagedHandle component, UInt8* output, UInt64 capacity,
                                                UInt64* required)
{
	auto* api = FromContext(context);
	if (!api || !required || (!output && capacity != 0))
	{
		return 1;
	}
	auto resolved = api->ResolveComponent(component);
	auto id =
	    resolved ? api->scene.ComponentPersistentId(resolved.value->Id()) : Result<ResourceId>::Failure(resolved.error);
	if (!id)
	{
		return 2;
	}
	const String text = id.value.ToString();
	*required = text.size();
	if (capacity < text.size())
	{
		return 4;
	}
	std::memcpy(output, text.data(), text.size());
	return 0;
}

Int32 ManagedSceneApi::ResolveComponentPersistentId(void* context, const UInt8* idBytes, UInt64 length,
                                                    ManagedHandle* output)
{
	auto* api = FromContext(context);
	if (!api || !idBytes || length != 36 || !output)
	{
		return 1;
	}
	auto id = ResourceId::Parse(String(reinterpret_cast<const char*>(idBytes), static_cast<Size>(length)));
	if (!id)
	{
		return 2;
	}
	auto componentId = api->scene.ComponentByPersistentId(id.value);
	if (!componentId)
	{
		return 3;
	}
	auto owner = api->scene.ComponentOwner(componentId.value);
	Component* component = api->scene.GetComponent(componentId.value);
	if (!owner || !component)
	{
		return 4;
	}
	auto handle = api->RegisterComponent(component, owner.value);
	if (!handle)
	{
		return 5;
	}
	*output = handle.value;
	return 0;
}

Result<GameObject::Ptr> ManagedSceneApi::Resolve(const ManagedHandle& handle) const
{
	auto resolved = handles->ResolveAs<GameObject>(handle, ManagedHandleKind::GameObject);
	if (!resolved)
	{
		return Result<GameObject::Ptr>::Failure(resolved.error);
	}
	const auto objectEntry = objects.find(handle.index);
	if (objectEntry == objects.end())
	{
		return Result<GameObject::Ptr>::Failure("Managed GameObject handle has no tracked Scene object");
	}
	GameObject::Ptr object = objectEntry->second.lock();
	if (!object || object.get() != resolved.value || !object->IsValid() || object->TryGetScene() != &scene)
	{
		handles->Invalidate(handle);
		return Result<GameObject::Ptr>::Failure("Managed GameObject handle no longer belongs to this Scene");
	}
	return Result<GameObject::Ptr>::Success(std::move(object));
}

Result<ManagedHandle> ManagedSceneApi::RegisterComponent(Component* component, const GameObject::Ptr& owner)
{
	if (!component)
	{
		return Result<ManagedHandle>::Failure("Cannot expose a null Component to managed code");
	}
	if (component->TryGetScene() != &scene || !owner || !owner->IsValid())
	{
		return Result<ManagedHandle>::Failure("Cannot expose a Component that is detached from this Scene");
	}
	if (component->GetGameObject() != owner)
	{
		return Result<ManagedHandle>::Failure("Cannot expose a Component through a GameObject that does not own it");
	}
	auto handle = handles->Register(component, ManagedHandleKind::Component);
	if (handle)
	{
		componentOwners[handle.value.index] = owner;
	}
	return handle;
}

Result<ManagedHandle> ManagedSceneApi::RegisterComponent(Component* component)
{
	if (!component)
	{
		return Result<ManagedHandle>::Failure("Cannot expose a null Component to managed code");
	}
	return RegisterComponent(component, component->GetGameObject());
}

Result<Component*> ManagedSceneApi::ResolveComponent(const ManagedHandle& handle) const
{
	auto resolved = handles->ResolveAs<Component>(handle, ManagedHandleKind::Component);
	if (!resolved)
	{
		return resolved;
	}
	const auto ownerEntry = componentOwners.find(handle.index);
	if (ownerEntry == componentOwners.end())
	{
		return Result<Component*>::Failure("Managed Component handle has no tracked owner");
	}
	auto owner = ownerEntry->second.lock();
	if (!owner || !owner->IsValid() || owner->TryGetScene() != &scene || resolved.value->TryGetScene() != &scene ||
	    resolved.value->GetGameObject() != owner)
	{
		handles->Invalidate(handle);
		return Result<Component*>::Failure("Managed Component handle no longer belongs to this Scene");
	}
	auto ids = scene.ComponentsOf(owner);
	if (!ids)
	{
		return Result<Component*>::Failure(ids.error);
	}
	for (ComponentId id : ids.value)
	{
		if (scene.GetComponent(id) == resolved.value)
		{
			return resolved;
		}
	}
	handles->Invalidate(handle);
	return Result<Component*>::Failure("Managed Component handle is stale");
}
} // namespace Ursa::Scripting
