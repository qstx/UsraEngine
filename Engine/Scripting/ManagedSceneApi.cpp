#include <Core/StringInterop.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>
#include <cstring>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Ursa::Scripting
{
Result<ManagedHandle> ManagedSceneApi::RegisterTexture(const MaterialTexturePtr& texture)
{
	RequireResourceAccess();
	if (!texture)
	{
		return Result<ManagedHandle>::Failure("Cannot register a null texture");
	}
	auto handle = handles->Register(const_cast<MaterialTexture*>(texture.get()), ManagedHandleKind::Asset);
	if (handle)
	{
		ownedTextures[handle.value.index] = texture;
	}
	return handle;
}

Result<MaterialTexturePtr> ManagedSceneApi::ReadTexture(const ManagedHandle& handle) const
{
	RequireResourceAccess();
	auto checked = handles->Resolve(handle, ManagedHandleKind::Asset);
	if (!checked)
	{
		return Result<MaterialTexturePtr>::Failure(checked.error);
	}
	const auto found = ownedTextures.find(handle.index);
	return found != ownedTextures.end() ? Result<MaterialTexturePtr>::Success(found->second)
	                                    : Result<MaterialTexturePtr>::Failure("Handle does not identify a texture");
}

namespace
{
std::atomic<UInt64> nextRenderSessionId{1};

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

ManagedSceneApi::~ManagedSceneApi()
{
	Deactivate();
}

const FrameContext* ManagedSceneApi::SetFrameContext(const FrameContext* context)
{
	const auto* previous = frameContext;
	frameContext = context;
	return previous;
}

void ManagedSceneApi::SetBehaviourRuntime(ManagedBehaviourRuntime* runtime)
{
	behaviourRuntime = runtime;
}

GameObject::Ptr ManagedSceneApi::SetLifecycleOwner(GameObject::Ptr owner)
{
	return std::exchange(lifecycleOwner, std::move(owner));
}

UInt64 ManagedSceneApi::SessionIdentity() const
{
	return handles->SessionId();
}

void ManagedSceneApi::CheckHandle(const void* value, Size size, UInt32 kind) const
{
	if (!value || size != sizeof(ManagedHandle))
	{
		throw std::runtime_error("Invalid managed handle layout");
	}
	ManagedHandle handle;
	std::memcpy(&handle, value, sizeof(handle));
	const auto resolved = handles->Resolve(handle, static_cast<ManagedHandleKind>(kind));
	if (!resolved)
	{
		throw std::runtime_error(ToStdString(resolved.error));
	}
}

void ManagedSceneApi::SetRenderDevice(Rhi::DevicePtr selectedDevice)
{
	auto executor = Rendering::RenderCommandExecutor::Create(selectedDevice);
	if (!executor)
	{
		throw std::runtime_error(ToStdString(executor.error));
	}
	device = std::move(selectedDevice);
	renderExecutor = std::move(executor.value);
}

void ManagedSceneApi::SetRenderOutput(std::shared_ptr<ManagedRenderTexture> output, Rhi::ResourceState finalState)
{
	if (activeRenderToken || ownerThread != std::this_thread::get_id())
	{
		throw std::runtime_error("Render output can only change between contexts on their owner thread");
	}
	renderOutput = std::move(output);
	renderWindow.reset();
	renderOutputFinalState = finalState;
}

void ManagedSceneApi::SetRenderCameraSelection(std::optional<Rendering::CameraType> filter,
                                               std::optional<Rendering::CameraData> cameraOverride)
{
	if (activeRenderToken || ownerThread != std::this_thread::get_id())
	{
		throw std::runtime_error("Render camera selection can only change between contexts on their owner thread");
	}
	renderCameraFilter = filter;
	renderCameraOverride = std::move(cameraOverride);
}

void ManagedSceneApi::SetRenderSceneOverride(Scene* source)
{
	if (activeRenderToken || ownerThread != std::this_thread::get_id())
	{
		throw std::runtime_error("Render scene can only change between contexts on its owner thread");
	}
	renderSceneOverride = source;
}

void ManagedSceneApi::SetRenderWindow(Rhi::SwapchainPtr output)
{
	if (activeRenderToken || ownerThread != std::this_thread::get_id())
	{
		throw std::runtime_error("Render window can only change between contexts on their owner thread");
	}
	renderWindow = std::move(output);
	renderOutput.reset();
	renderOutputFinalState = Rhi::ResourceState::Present;
}

void ManagedSceneApi::SetFrameCapture(Rendering::FrameCapture* capture)
{
	if (renderExecutor)
	{
		renderExecutor->SetFrameCapture(capture);
	}
}

Result<ManagedHandle> ManagedSceneApi::Register(const GameObject::Ptr& object)
{
	if (!Contains(object))
	{
		return Result<ManagedHandle>::Failure("Cannot expose a GameObject outside this Scene to managed code");
	}
	auto handle = handles->Register(object.get(), ManagedHandleKind::GameObject);
	if (handle)
	{
		const auto previous = objects.find(handle.value.index);
		if (previous != objects.end() && previous->second.lock() != object)
		{
			handles->Invalidate(handle.value);
			handle = handles->Register(object.get(), ManagedHandleKind::GameObject);
			if (!handle)
			{
				return handle;
			}
		}
		objects[handle.value.index] = object;
	}
	return handle;
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
	if (!object || object.get() != resolved.value || !Contains(object))
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
	if (!Contains(owner) || component->TryGetScene() != owner->TryGetScene())
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
		const auto previous = componentIdentities.find(handle.value.index);
		if (previous != componentIdentities.end() && previous->second != component->PersistentId())
		{
			handles->Invalidate(handle.value);
			handle = handles->Register(component, ManagedHandleKind::Component);
			if (!handle)
			{
				return handle;
			}
		}
		componentOwners[handle.value.index] = owner;
		componentIdentities[handle.value.index] = component->PersistentId();
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
	if (!Contains(owner))
	{
		handles->Invalidate(handle);
		return Result<Component*>::Failure("Managed Component handle no longer belongs to this Scene");
	}
	for (Component* component : owner->Components())
	{
		if (component == resolved.value)
		{
			const auto identity = componentIdentities.find(handle.index);
			if (identity == componentIdentities.end() || identity->second != component->PersistentId() ||
			    component->TryGetScene() != owner->TryGetScene() || component->GetGameObject() != owner)
			{
				handles->Invalidate(handle);
				return Result<Component*>::Failure("Managed Component was removed, replaced or moved");
			}
			return resolved;
		}
	}
	handles->Invalidate(handle);
	return Result<Component*>::Failure("Managed Component handle is stale");
}

void ManagedSceneApi::SetDetachedRoots(Vector<GameObject::Ptr> roots)
{
	Require(0);
	for (const auto& root : roots)
	{
		if (!root || !root->IsValid() || root->TryGetScene())
		{
			throw std::runtime_error("Detached managed graph roots must be live assets outside a Scene");
		}
	}
	detachedRoots = std::move(roots);
}

Result<GameObject::Ptr> ManagedSceneApi::CreateObject(String name)
{
	RequireSceneMutation();
	if (detachedRoots.empty())
	{
		return scene.CreateGameObject(std::move(name));
	}
	auto object = GameObject::Create(std::move(name));
	detachedRoots.push_back(object);
	return Result<GameObject::Ptr>::Success(std::move(object));
}

bool ManagedSceneApi::Contains(const GameObject::Ptr& object) const
{
	if (!object || !object->IsValid())
	{
		return false;
	}
	if (scene.Contains(object))
	{
		return true;
	}
	// Pending destruction removes the object from normal Scene queries before its
	// OnDisable/OnDestroy callbacks run. Only that callback's live owner remains accessible.
	if (object == lifecycleOwner && object->TryGetScene() == &scene)
	{
		return true;
	}
	if (object->TryGetScene())
	{
		return false;
	}
	auto root = object;
	while (root->Parent())
	{
		root = root->Parent();
	}
	return std::find(detachedRoots.begin(), detachedRoots.end(), root) != detachedRoots.end();
}

Result<GameObject::Ptr> ManagedSceneApi::FindGameObject(ResourceId id) const
{
	const auto attachedObject = scene.GameObjectById(id);
	if (attachedObject)
	{
		return attachedObject;
	}
	Vector<GameObject::Ptr> pending = detachedRoots;
	while (!pending.empty())
	{
		auto object = std::move(pending.back());
		pending.pop_back();
		if (!Contains(object))
		{
			continue;
		}
		if (object->Id() == id)
		{
			return Result<GameObject::Ptr>::Success(std::move(object));
		}
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
	}
	return Result<GameObject::Ptr>::Failure("GameObject is not present in this managed graph");
}

Result<Component*> ManagedSceneApi::FindComponent(ResourceId id) const
{
	const auto attachedComponent = scene.ComponentByPersistentId(id);
	if (attachedComponent)
	{
		return Result<Component*>::Success(scene.GetComponent(attachedComponent.value));
	}
	Vector<GameObject::Ptr> pending = detachedRoots;
	while (!pending.empty())
	{
		auto object = std::move(pending.back());
		pending.pop_back();
		if (!Contains(object))
		{
			continue;
		}
		for (auto* component : object->Components())
		{
			if (component->PersistentId() == id)
			{
				return Result<Component*>::Success(component);
			}
		}
		for (const auto& child : object->Children())
		{
			pending.push_back(child);
		}
	}
	return Result<Component*>::Failure("Component is not present in this managed graph");
}
} // namespace Ursa::Scripting
