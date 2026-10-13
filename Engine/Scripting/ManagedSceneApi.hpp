#pragma once

#include <Scene/Scene.hpp>
#include <Rendering/Scene/RendererList.hpp>
#include <Rhi/RHI.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>
#include <Scripting/ManagedBehaviourRuntime.hpp>
#include <Scripting/NativeApi.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <functional>
#include <optional>
#include <thread>

namespace Ursa::Scripting
{
class ManagedSceneApi final
{
public:
	struct ManagedRenderTexture
	{
		Rhi::TexturePtr color;
		Rhi::TexturePtr depth;
	};

	struct ManagedGraphicsBuffer
	{
		Rhi::BufferPtr buffer;
	};

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

	NativeApiV6 Bind(NativeApiV6 api);

	void SetRenderContextToken(Rendering::RenderContextToken token)
	{
		activeRenderToken = token;
	}

	void ClearRenderContextToken()
	{
		activeRenderToken.reset();
	}

	void SetRenderDevice(Rhi::DevicePtr selectedDevice)
	{
		device = std::move(selectedDevice);
	}

	UInt64 AcceptedRenderBatchCount() const
	{
		return acceptedRenderBatchCount;
	}

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

	static Int32 ResourceRequest(void* context, const UInt8* input, UInt64 length, UInt8* output, UInt64 capacity,
	                             UInt64* required);

	static Int32 SubmitRenderBatch(void* context, const UInt8* tokenBytes, UInt64 tokenByteCount, const UInt8* batch,
	                               UInt64 batchByteCount);

	nlohmann::json DispatchResourceRequest(const nlohmann::json& request);

	Result<ManagedHandle> RegisterMesh(const MeshPtr& mesh);

	Result<ManagedHandle> RegisterMaterial(const std::shared_ptr<MaterialInstance>& material);

	Result<MeshPtr> ResolveMesh(const ManagedHandle& handle) const;

	Result<std::shared_ptr<MaterialInstance>> ResolveMaterial(const ManagedHandle& handle) const;

	Result<std::shared_ptr<ManagedRenderTexture>> ResolveRenderTexture(const ManagedHandle& handle) const;

	Result<std::shared_ptr<ManagedGraphicsBuffer>> ResolveGraphicsBuffer(const ManagedHandle& handle) const;

	String resourceResponse;
	UnorderedMap<UInt32, MeshPtr> ownedMeshes;
	UnorderedMap<UInt32, std::shared_ptr<MaterialInstance>> ownedMaterials;
	UnorderedMap<UInt32, MaterialTexturePtr> ownedTextures;
	UnorderedMap<UInt32, std::shared_ptr<ManagedRenderTexture>> ownedRenderTextures;
	UnorderedMap<UInt32, std::shared_ptr<ManagedGraphicsBuffer>> ownedGraphicsBuffers;
	Scene& scene;
	std::unique_ptr<ManagedHandleRegistry> handles;
	UnorderedMap<UInt32, std::weak_ptr<GameObject>> objects;
	UnorderedMap<UInt32, std::weak_ptr<GameObject>> componentOwners;
	ComponentRegistry componentTypes;
	const FrameContext* frameContext = nullptr;
	ResourceSession* resources = nullptr;
	Rhi::DevicePtr device;
	ManagedBehaviourRuntime* behaviourRuntime = nullptr;
	std::function<void(const String&)> setWindowTitle;
	std::thread::id ownerThread = std::this_thread::get_id();
	std::optional<Rendering::RenderContextToken> activeRenderToken;
	Rendering::RenderScene renderScene;
	Rendering::RenderSceneContext renderSceneContext;
	UInt64 renderSessionId = 0;
	String activeRenderCamera;
	Vector<Vector<UInt8>> renderBatches;
	Vector<Vector<std::shared_ptr<void>>> renderBatchResources;
	UInt64 acceptedRenderBatchCount = 0;
};
} // namespace Ursa::Scripting
