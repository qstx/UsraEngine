#pragma once

#include <Scene/Scene.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>
#include <Scripting/ManagedBehaviourRuntime.hpp>
#include <Scripting/NativeApi.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <functional>

namespace Ursa::Scripting
{
class ManagedSceneApi final
{
public:
	explicit ManagedSceneApi(Scene& scene, ResourceSession* resources = nullptr,
	                         std::function<void(const String&)> setWindowTitle = {});

	Result<ManagedHandle> Register(const GameObject::Ptr& object);

	Result<ManagedHandle> RegisterComponent(Component* component);

	const FrameContext* SetFrameContext(const FrameContext* context)
	{
		const FrameContext* previous = frameContext;
		frameContext = context;
		return previous;
	}

	void SetBehaviourRuntime(ManagedBehaviourRuntime* runtime)
	{
		behaviourRuntime = runtime;
	}

	NativeApiV4 Bind(NativeApiV4 api);

private:
	static ManagedSceneApi* FromContext(void* context);

	static Int32 CreateGameObject(void* context, const UInt8* name, UInt64 length, ManagedHandle* output);

	static Int32 GetGameObjectName(void* context, ManagedHandle object, UInt8* output, UInt64 capacity,
	                               UInt64* required);

	static Int32 SetGameObjectName(void* context, ManagedHandle object, const UInt8* name, UInt64 length);

	static Int32 GetGameObjectActive(void* context, ManagedHandle object, UInt32* output);

	static Int32 SetGameObjectActive(void* context, ManagedHandle object, UInt32 active);

	static Int32 GetGameObjectParent(void* context, ManagedHandle object, ManagedHandle* output);

	static Int32 SetGameObjectParent(void* context, ManagedHandle object, ManagedHandle parent);

	static Int32 DestroyGameObject(void* context, ManagedHandle object);

	static Int32 GetWorldPosition(void* context, ManagedHandle object, Float32* output, UInt64 count);

	static Int32 SetWorldPosition(void* context, ManagedHandle object, const Float32* values, UInt64 count);

	static Int32 GetCameraProjection(void* context, ManagedHandle component, Float32* output, UInt64 count);

	static Int32 SetCameraProjection(void* context, ManagedHandle component, const Float32* values, UInt64 count);

	static Int32 GetBoxRenderColor(void* context, ManagedHandle component, Float32* output, UInt64 count);

	static Int32 SetBoxRenderColor(void* context, ManagedHandle component, const Float32* values, UInt64 count);

	static Int32 GetComponentAssetReference(void* context, ManagedHandle component, UInt8* output, UInt64 capacity,
	                                        UInt64* required, UInt64* localFileId);

	static Int32 SetComponentAssetReference(void* context, ManagedHandle component, const UInt8* guid, UInt64 length,
	                                        UInt64 localFileId);

	static Int32 SetWindowTitle(void* context, const UInt8* title, UInt64 length);

	static Int32 GetLocalTransform(void* context, ManagedHandle object, Float32* output, UInt64 count);

	static Int32 SetLocalTransform(void* context, ManagedHandle object, const Float32* values, UInt64 count);

	static Int32 GetInputState(void* context, UInt32 key, UInt32 phase, UInt32* output);

	static Int32 GetTimeState(void* context, Float32* delta, Float32* fixedDelta, Float64* time, Float64* fixedTime,
	                          UInt64* frame);

	static Int32 AddComponent(void* context, ManagedHandle object, const UInt8* type, UInt64 length,
	                          ManagedHandle* output);

	static Int32 GetComponentCount(void* context, ManagedHandle object, UInt32* output);

	static Int32 GetComponentAt(void* context, ManagedHandle object, UInt32 index, ManagedHandle* output);

	static Int32 GetComponentType(void* context, ManagedHandle component, UInt8* output, UInt64 capacity,
	                              UInt64* required);

	static Int32 GetComponentOwner(void* context, ManagedHandle component, ManagedHandle* output);

	static Int32 GetComponentEnabled(void* context, ManagedHandle component, UInt32* output);

	static Int32 SetComponentEnabled(void* context, ManagedHandle component, UInt32 enabled);

	static Int32 IsComponentActive(void* context, ManagedHandle component, UInt32* output);

	static Int32 RemoveComponent(void* context, ManagedHandle component);

	static Int32 LoadTexture(void* context, const UInt8* guid, UInt64 guidLength, UInt64 localFileId,
	                         ManagedHandle* output);

	static Int32 LoadMaterial(void* context, const UInt8* guid, UInt64 guidLength, UInt64 localFileId,
	                          ManagedHandle* output);

	static Int32 AddBehaviour(void* context, ManagedHandle object, const UInt8* typeName, UInt64 typeNameLength,
	                          UInt64 instanceToken, ManagedHandle* output);

	static Int32 GetGameObjectPersistentId(void* context, ManagedHandle object, UInt8* output, UInt64 capacity,
	                                       UInt64* required);

	static Int32 ResolveGameObjectPersistentId(void* context, const UInt8* id, UInt64 length, ManagedHandle* output);

	static Int32 GetComponentPersistentId(void* context, ManagedHandle component, UInt8* output, UInt64 capacity,
	                                      UInt64* required);

	static Int32 ResolveComponentPersistentId(void* context, const UInt8* id, UInt64 length, ManagedHandle* output);

	Result<GameObject::Ptr> Resolve(const ManagedHandle& handle) const;

	Result<ManagedHandle> RegisterComponent(Component* component, const GameObject::Ptr& owner);

	Result<Component*> ResolveComponent(const ManagedHandle& handle) const;

	Scene& scene;
	std::unique_ptr<ManagedHandleRegistry> handles;
	UnorderedMap<UInt32, std::weak_ptr<GameObject>> objects;
	UnorderedMap<UInt32, std::weak_ptr<GameObject>> componentOwners;
	ComponentRegistry componentTypes;
	const FrameContext* frameContext = nullptr;
	ResourceSession* resources = nullptr;
	ManagedBehaviourRuntime* behaviourRuntime = nullptr;
	std::function<void(const String&)> setWindowTitle;
};
} // namespace Ursa::Scripting
