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
	if (!scene.Contains(object))
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
		const auto previous = componentIdentities.find(handle.value.index);
		if (previous != componentIdentities.end() && previous->second != component->Id())
		{
			handles->Invalidate(handle.value);
			handle = handles->Register(component, ManagedHandleKind::Component);
			if (!handle)
			{
				return handle;
			}
		}
		componentOwners[handle.value.index] = owner;
		componentIdentities[handle.value.index] = component->Id();
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
	if (!owner || !owner->IsValid() || owner->TryGetScene() != &scene)
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
			const auto identity = componentIdentities.find(handle.index);
			if (identity == componentIdentities.end() || identity->second != id ||
			    resolved.value->TryGetScene() != &scene || resolved.value->GetGameObject() != owner)
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
} // namespace Ursa::Scripting
