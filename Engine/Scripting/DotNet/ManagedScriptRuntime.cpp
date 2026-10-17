#include <Scripting/DotNet/ManagedScriptRuntime.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>

namespace Ursa::Scripting
{
ManagedScriptRuntime::ManagedScriptRuntime(Scene& scene, DotNetHost& host, ManagedSceneApi& sceneApi, UInt64 sessionId)
    : scene(scene), host(host), sceneApi(sceneApi), sessionId(sessionId)
{
	sceneApi.SetBehaviourRuntime(this);
	revokePreparation = scene.SetRuntimePreparation([this] { return AttachSerializedComponents(); }, true);
}

Result<ComponentId> ManagedScriptRuntime::AddExisting(const GameObject::Ptr& owner, const String& typeName,
                                                      UInt64 instanceToken)
{
	if (!instanceToken || !scene.Contains(owner))
	{
		return Result<ComponentId>::Failure(
		    "Cannot attach a managed Behaviour without a live object and instance token");
	}
	auto added = scene.AddComponent<ScriptBehaviourComponent>(owner, *this, typeName, instanceToken);
	if (!added)
	{
		return added;
	}
	return added;
}

Result<ComponentId> ManagedScriptRuntime::CreateAndAdd(const GameObject::Ptr& owner, const String& typeName)
{
	if (!scene.Contains(owner))
	{
		return Result<ComponentId>::Failure("Cannot attach a managed Behaviour outside its Scene");
	}
	auto ownerHandle = sceneApi.Register(owner);
	if (!ownerHandle)
	{
		return Result<ComponentId>::Failure(ownerHandle.error);
	}
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return Result<ComponentId>::Failure(selected.error);
		}
	}
	auto instanceToken = host.CreateBehaviour(typeName, ownerHandle.value);
	if (!instanceToken)
	{
		return Result<ComponentId>::Failure(instanceToken.error);
	}
	auto added = AddExisting(owner, typeName, instanceToken.value);
	if (!added)
	{
		host.DestroyBehaviour(instanceToken.value);
		return added;
	}
	return added;
}

Result<void> ManagedScriptRuntime::AttachSerializedComponents()
{
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return selected;
		}
	}
	bool attachedAny = false;
	Vector<GameObject::Ptr> pending = scene.RootGameObjects();
	while (!pending.empty())
	{
		GameObject::Ptr owner = std::move(pending.back());
		pending.pop_back();
		auto children = scene.ChildrenOf(owner);
		if (!children)
		{
			return Result<void>::Failure(children.error);
		}
		for (auto& child : children.value)
		{
			pending.push_back(std::move(child));
		}
		auto components = scene.ComponentsOf(owner);
		if (!components)
		{
			return Result<void>::Failure(components.error);
		}
		for (ComponentId componentId : components.value)
		{
			auto* script = dynamic_cast<ScriptBehaviourComponent*>(scene.GetComponent(componentId));
			if (!script || script->InstanceToken() != 0)
			{
				continue;
			}
			auto ownerHandle = sceneApi.Register(owner);
			if (!ownerHandle)
			{
				return Result<void>::Failure(ownerHandle.error);
			}
			auto type = host.ResolveBehaviourType(sessionId, script->SerializedPayload(), script->TypeName());
			if (!type)
			{
				return Result<void>::Failure(type.error);
			}
			auto token = host.CreateBehaviour(type.value, ownerHandle.value);
			if (!token)
			{
				return Result<void>::Failure(token.error);
			}
			auto attached = script->AttachManagedRuntime(*this, token.value);
			if (!attached)
			{
				host.DestroyBehaviour(token.value);
				return attached;
			}
			script->SetTypeName(std::move(type.value));
			auto componentHandle = sceneApi.RegisterComponent(script);
			if (!componentHandle)
			{
				return Result<void>::Failure(componentHandle.error);
			}
			auto bound = host.BindBehaviour(token.value, componentHandle.value);
			if (!bound)
			{
				return bound;
			}
			attachedAny = true;
			boundHandles[token.value] = componentHandle.value;
			auto restored = script->RestoreSerializedPayload();
			if (!restored)
			{
				return restored;
			}
		}
	}
	return attachedAny && sessionId ? host.ValidateScriptSession(sessionId) : Result<void>{};
}

ManagedScriptRuntime::~ManagedScriptRuntime()
{
	if (revokePreparation)
	{
		revokePreparation();
	}
	sceneApi.SetBehaviourRuntime(nullptr);
}

Result<ManagedHandle> ManagedScriptRuntime::BoundHandle(UInt64 instanceToken) const
{
	auto found = boundHandles.find(instanceToken);
	return found == boundHandles.end() ? Result<ManagedHandle>::Failure("Managed Behaviour has no native binding")
	                                   : Result<ManagedHandle>::Success(found->second);
}

Result<void> ManagedScriptRuntime::BindComponent(UInt64 instanceToken, Component& component)
{
	if (component.TryGetScene() != &scene)
	{
		return Result<void>::Failure("Cannot bind a Behaviour to another Scene");
	}
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return selected;
		}
	}
	auto handle = sceneApi.RegisterComponent(&component);
	if (!handle)
	{
		return Result<void>::Failure(handle.error);
	}
	auto bound = host.BindBehaviour(instanceToken, handle.value);
	if (bound)
	{
		boundHandles[instanceToken] = handle.value;
	}
	return bound;
}

Result<void> ManagedScriptRuntime::Dispatch(UInt64 instanceToken, BehaviourEvent event, const FrameContext* context,
                                            Component* component)
{
	if (!component || component->TryGetScene() != &scene)
	{
		return Result<void>::Failure("Managed script Component is detached from its Scene");
	}
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return selected;
		}
	}
	if (context)
	{
		lastFrameContext = *context;
		hasFrameContext = true;
	}
	const FrameContext* previous = sceneApi.SetFrameContext(context           ? context
	                                                        : hasFrameContext ? &lastFrameContext
	                                                                          : nullptr);
	const auto result = host.InvokeBehaviour(instanceToken, static_cast<ManagedBehaviourEvent>(event));
	sceneApi.SetFrameContext(previous);
	return result;
}

Result<void> ManagedScriptRuntime::Destroy(UInt64 instanceToken)
{
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return selected;
		}
	}
	boundHandles.erase(instanceToken);
	return host.DestroyBehaviour(instanceToken);
}

Result<String> ManagedScriptRuntime::SerializeFields(UInt64 instanceToken)
{
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return Result<String>::Failure(selected.error);
		}
	}
	return host.SerializeBehaviour(instanceToken);
}

Result<void> ManagedScriptRuntime::DeserializeFields(UInt64 instanceToken, const String& payload)
{
	if (sessionId)
	{
		auto selected = host.SelectScriptSession(sessionId);
		if (!selected)
		{
			return selected;
		}
	}
	return host.DeserializeBehaviour(instanceToken, payload);
}
} // namespace Ursa::Scripting
