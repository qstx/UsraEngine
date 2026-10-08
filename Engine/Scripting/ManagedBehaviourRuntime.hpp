#pragma once

#include <Core/Result.hpp>
#include <Scene/Component.hpp>
#include <Scene/GameObject.hpp>
#include <Scene/Scene.hpp>
#include <Scripting/ManagedHandleRegistry.hpp>

namespace Ursa::Scripting
{
enum class BehaviourEvent : UInt32
{
	Awake,
	OnEnable,
	Start,
	FixedUpdate,
	Update,
	LateUpdate,
	OnDisable,
	OnDestroy
};

class ManagedBehaviourRuntime
{
public:
	virtual ~ManagedBehaviourRuntime() = default;

	virtual Result<ComponentId> AddExisting(const GameObject::Ptr& owner, const String& typeName,
	                                        UInt64 instanceToken) = 0;

	virtual Result<ComponentId> CreateAndAdd(const GameObject::Ptr& owner, const String& typeName) = 0;

	virtual Result<ManagedHandle> BoundHandle(UInt64) const
	{
		return Result<ManagedHandle>::Failure("No cached native binding is available");
	}

	virtual Result<void> BindComponent(UInt64, Component&)
	{
		return {};
	}

	virtual Result<void> Dispatch(UInt64 instanceToken, BehaviourEvent event, const FrameContext* context,
	                              Component* component) = 0;

	virtual Result<void> Destroy(UInt64 instanceToken) = 0;

	virtual Result<String> SerializeFields(UInt64)
	{
		return Result<String>::Failure("Managed Behaviour field serialization is unavailable");
	}

	virtual Result<void> DeserializeFields(UInt64, const String&)
	{
		return Result<void>::Failure("Managed Behaviour field deserialization is unavailable");
	}
};
} // namespace Ursa::Scripting
