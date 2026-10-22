#pragma once

#include <Scene/Scene.hpp>
#include <Rendering/Scene/RendererList.hpp>
#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Rhi/RHI.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>
#include <Scripting/Bindings/BindingRuntime.hpp>
#include <Scripting/ManagedBehaviourRuntime.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <functional>
#include <optional>
#include <thread>

namespace Ursa::Scripting
{
namespace Bindings
{
struct RuntimeServices;
struct MeshServices;
struct MaterialServices;
struct ResourceServices;
struct SerializationServices;
struct RenderingServices;
} // namespace Bindings

class ManagedSceneApi : public Bindings::BindingContext
{
public:
	using ManagedRenderTexture = Rendering::RenderTextureResource;
	using ManagedGraphicsBuffer = Rendering::GraphicsBufferResource;

	explicit ManagedSceneApi(Scene& scene, ResourceSession* resources = nullptr,
	                         std::function<void(const String&)> setWindowTitle = {});

	~ManagedSceneApi() override;
	UInt64 SessionIdentity() const override;
	Result<std::shared_ptr<void>> CandidateResource(const ManagedHandle& handle) const;

	virtual void RequireGameExecution() const
	{
		Require(0);
	}

	virtual void RequireResourceAccess() const
	{
		Require(0);
	}

	virtual void RequireRendering() const
	{
		Require(0);
	}

	virtual void RequireSceneMutation() const
	{
		Require(0);
	}

	virtual bool PersistentResourcesReadOnly() const
	{
		return false;
	}

	virtual bool LocalResourceReferencesReadOnly() const
	{
		return false;
	}

	virtual bool CpuBuiltinShaders() const
	{
		return false;
	}

	void RequireWritableResource(const ManagedHandle& handle) const;

	void SetResourceSession(ResourceSession* value)
	{
		Require(0);
		resources = value;
	}

	void SetResourceProvider(std::function<ResourceSession*()> provider)
	{
		Require(0);
		resourceProvider = std::move(provider);
	}

	ResourceSession* Resources()
	{
		RequireResourceAccess();
		if (!resources && resourceProvider)
		{
			resources = resourceProvider();
		}
		return resources;
	}

	void CheckHandle(const void* value, Size size, UInt32 kind) const override;

	Result<void> BindHostServices(void* token)
	{
		if (ownerThread != std::this_thread::get_id())
		{
			return Result<void>::Failure("Host services must bind on the Scene owner thread");
		}
		bindingHostToken = token;
		return {};
	}

	Result<ManagedHandle> Register(const GameObject::Ptr& object);
	Result<GameObject::Ptr> CreateObject(String name);

	Result<ManagedHandle> RegisterComponent(Component* component);
	Result<ManagedHandle> RegisterMaterial(const std::shared_ptr<MaterialInstance>& material, bool writable = false);
	Result<ManagedHandle> RegisterMesh(const MeshPtr& mesh, bool writable = false);
	Result<ManagedHandle> RegisterTexture(const MaterialTexturePtr& texture);

	Result<MeshPtr> ReadMesh(const ManagedHandle& handle) const
	{
		RequireResourceAccess();
		return ResolveMesh(handle);
	}

	Result<std::shared_ptr<MaterialInstance>> ReadMaterial(const ManagedHandle& handle) const
	{
		RequireResourceAccess();
		return ResolveMaterial(handle);
	}

	Result<MaterialTexturePtr> ReadTexture(const ManagedHandle& handle) const;
	void SetDetachedRoots(Vector<GameObject::Ptr> roots);
	Result<GameObject::Ptr> FindGameObject(ResourceId id) const;
	Result<Component*> FindComponent(ResourceId id) const;

	const FrameContext* SetFrameContext(const FrameContext* context);

	void SetBehaviourRuntime(ManagedBehaviourRuntime* runtime);
	GameObject::Ptr SetLifecycleOwner(GameObject::Ptr owner);

	void SetRenderDevice(Rhi::DevicePtr selectedDevice);
	void SetRenderOutput(std::shared_ptr<ManagedRenderTexture> output,
	                     Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead);
	void SetRenderCameraSelection(std::optional<Rendering::CameraType> filter,
	                              std::optional<Rendering::CameraData> cameraOverride = {});
	void SetRenderSceneOverride(Scene* source);
	void SetRenderWindow(Rhi::SwapchainPtr output);
	void SetFrameCapture(Rendering::FrameCapture* capture);

	UInt64 AcceptedRenderBatchCount() const
	{
		return acceptedRenderBatchCount;
	}

private:
	friend struct Bindings::RuntimeServices;
	friend struct Bindings::MeshServices;
	friend struct Bindings::MaterialServices;
	friend struct Bindings::ResourceServices;
	friend struct Bindings::SerializationServices;
	friend struct Bindings::RenderingServices;
	void* bindingHostToken = nullptr;
	Result<GameObject::Ptr> Resolve(const ManagedHandle& handle) const;

	Result<ManagedHandle> RegisterComponent(Component* component, const GameObject::Ptr& owner);
	bool Contains(const GameObject::Ptr& object) const;

	Result<Component*> ResolveComponent(const ManagedHandle& handle) const;

	Result<MeshPtr> ResolveMesh(const ManagedHandle& handle) const;

	Result<std::shared_ptr<MaterialInstance>> ResolveMaterial(const ManagedHandle& handle) const;

	Result<std::shared_ptr<ManagedRenderTexture>> ResolveRenderTexture(const ManagedHandle& handle) const;

	Result<std::shared_ptr<ManagedGraphicsBuffer>> ResolveGraphicsBuffer(const ManagedHandle& handle) const;

	Result<void> ExecuteRenderBatch(const UInt8* bytes, Size byteCount);

	UnorderedMap<UInt32, MeshPtr> ownedMeshes;
	UnorderedMap<UInt32, std::shared_ptr<MaterialInstance>> ownedMaterials;
	UnorderedMap<UInt32, MaterialTexturePtr> ownedTextures;
	Set<UInt32> readOnlyResources;
	UnorderedMap<UInt32, std::shared_ptr<ManagedRenderTexture>> ownedRenderTextures;
	UnorderedMap<UInt32, std::shared_ptr<ManagedGraphicsBuffer>> ownedGraphicsBuffers;
	Scene& scene;
	Scene* renderSceneOverride = nullptr;
	std::unique_ptr<ManagedHandleRegistry> handles;
	UnorderedMap<UInt32, std::weak_ptr<GameObject>> objects;
	UnorderedMap<UInt32, std::weak_ptr<GameObject>> componentOwners;
	UnorderedMap<UInt32, ResourceId> componentIdentities;
	Vector<GameObject::Ptr> detachedRoots;
	ComponentRegistry componentTypes;
	const FrameContext* frameContext = nullptr;
	ResourceSession* resources = nullptr;
	std::function<ResourceSession*()> resourceProvider;
	Rhi::DevicePtr device;
	std::unique_ptr<Rendering::RenderCommandExecutor> renderExecutor;
	std::shared_ptr<ManagedRenderTexture> renderOutput;
	Rhi::SwapchainPtr renderWindow;
	Rhi::ResourceState renderOutputFinalState = Rhi::ResourceState::ShaderRead;
	std::optional<Rendering::CameraType> renderCameraFilter;
	std::optional<Rendering::CameraData> renderCameraOverride;
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
	GameObject::Ptr lifecycleOwner;
};
} // namespace Ursa::Scripting
