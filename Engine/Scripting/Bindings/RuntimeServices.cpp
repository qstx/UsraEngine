#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Mesh/Primitive.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Scripting/ScriptBehaviourComponent.hpp>

namespace Ursa::Scripting::Bindings
{
ManagedSceneApi& RuntimeServices::SceneApi(BindingCall& call)
{
	auto* api = dynamic_cast<ManagedSceneApi*>(&call.context);
	if (!api)
	{
		throw std::runtime_error("This Runtime binding requires an active Scene");
	}
	api->RequireResourceAccess();
	return *api;
}

Result<RuntimeServices::Handle> RuntimeServices::CreateGameObject(BindingCall& call, const Utf8View& name)
{
	if (name.length > 256)
	{
		return Result<Handle>::Failure("GameObject name exceeds 256 UTF-8 bytes");
	}
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	auto object = TakeBindingResult(api.CreateObject(ReadString(name)));
	auto handle = api.Register(object);
	if (!handle)
	{
		api.scene.DestroyGameObject(object);
		return Result<Handle>::Failure(handle.error);
	}
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle.value));
}

Result<Utf8View> RuntimeServices::GetName(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	const auto object = TakeBindingResult(api.Resolve(ReadManagedHandle(handle)));
	return Result<Utf8View>::Success(call.arena.StringValue(TakeBindingResult(object->Name())));
}

Result<void> RuntimeServices::SetName(BindingCall& call, const Handle& handle, const Utf8View& name)
{
	if (name.length > 256)
	{
		return Result<void>::Failure("GameObject name exceeds 256 UTF-8 bytes");
	}
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	return TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->SetName(ReadString(name));
}

Result<bool> RuntimeServices::GetActive(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	return TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->ActiveSelf();
}

Result<void> RuntimeServices::SetActive(BindingCall& call, const Handle& handle, bool active)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	return TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->SetActive(active);
}

Result<RuntimeServices::Handle> RuntimeServices::GetParent(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	const auto parent = TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->Parent();
	return Result<Handle>::Success(parent ? WriteManagedHandle<Handle>(TakeBindingResult(api.Register(parent)))
	                                      : Handle{});
}

Result<void> RuntimeServices::SetParent(BindingCall& call, const Handle& handle, const Handle& parent)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	const auto object = TakeBindingResult(api.Resolve(ReadManagedHandle(handle)));
	GameObject::Ptr parentObject;
	if (parent.F_sessionId != 0)
	{
		parentObject = TakeBindingResult(api.Resolve(ReadManagedHandle(parent)));
	}
	else if (parent.F_index || parent.F_generation || parent.F_kind || parent.F_reserved)
	{
		return Result<void>::Failure("Null parent requires an empty handle");
	}
	return object->SetParent(std::move(parentObject));
}

Result<DataView> RuntimeServices::GetChildren(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	const auto children = TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->Children();
	auto* values = static_cast<Handle*>(call.arena.Allocate(children.size(), sizeof(Handle)));
	for (Size index = 0; index < children.size(); ++index)
	{
		values[index] = WriteManagedHandle<Handle>(TakeBindingResult(api.Register(children[index])));
	}
	return Result<DataView>::Success({values, children.size(), 1, 0});
}

Result<void> RuntimeServices::DestroyGameObject(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	return TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->Destroy();
}

Result<Int32> RuntimeServices::GetLayer(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	return Result<Int32>::Success(
	    Int32(TakeBindingResult(TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->Layer())));
}

Result<void> RuntimeServices::SetLayer(BindingCall& call, const Handle& handle, Int32 layer)
{
	if (layer < 0 || layer > 31)
	{
		return Result<void>::Failure("GameObject Layer must be between 0 and 31");
	}
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	return TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->SetLayer(UInt32(layer));
}

Result<RuntimeServices::Handle> RuntimeServices::CreatePrimitive(BindingCall& call, Int32 type)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	if (type != 0 || !api.Resources())
	{
		return Result<Handle>::Failure("Primitive creation requires Cube and a ResourceSession");
	}
	auto object = TakeBindingResult(Ursa::CreatePrimitive(api.scene, *api.Resources(), PrimitiveType::Cube));
	auto handle = api.Register(object);
	if (!handle)
	{
		api.scene.DestroyGameObject(object);
		return Result<Handle>::Failure(handle.error);
	}
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle.value));
}

Result<Utf8View> RuntimeServices::GetGameObjectPersistentId(BindingCall& call, const Handle& handle)
{
	auto& api = SceneApi(call);
	const auto id = TakeBindingResult(api.Resolve(ReadManagedHandle(handle)))->Id();
	return Result<Utf8View>::Success(call.arena.StringValue(id.ToString()));
}

Result<RuntimeServices::Handle> RuntimeServices::ResolveGameObjectPersistentId(BindingCall& call, const Utf8View& id)
{
	auto& api = SceneApi(call);
	const auto identity = TakeBindingResult(ResourceId::Parse(ReadString(id)));
	const auto object = TakeBindingResult(api.FindGameObject(identity));
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(api.Register(object))));
}

Result<RuntimeServices::Handle> RuntimeServices::AddComponent(BindingCall& call, const Handle& owner,
                                                              const Utf8View& typeId)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	if (typeId.length == 0 || typeId.length > 256)
	{
		return Result<Handle>::Failure("Invalid native Component type identity");
	}
	const auto object = TakeBindingResult(api.Resolve(ReadManagedHandle(owner)));
	const auto type = ReadString(typeId);
	Result<Component*> added;
	if (type == "ursa.Camera")
	{
		auto value = object->AddComponent<CameraComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else if (type == "ursa.MeshFilter")
	{
		auto value = object->AddComponent<MeshFilterComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else if (type == "ursa.MeshRenderer")
	{
		auto value = object->AddComponent<MeshRendererComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else if (type == "ursa.SkinnedMeshRenderer")
	{
		auto value = object->AddComponent<SkinnedMeshRendererComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else if (type == "ursa.Animator")
	{
		auto value = object->AddComponent<AnimatorComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else if (type == "ursa.DirectionalLight")
	{
		auto value = object->AddComponent<DirectionalLightComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else if (type == "ursa.PointLight")
	{
		auto value = object->AddComponent<PointLightComponent>();
		added = value ? Result<Component*>::Success(value.value) : Result<Component*>::Failure(value.error);
	}
	else
	{
		return Result<Handle>::Failure("Unsupported native Component type identity");
	}
	if (!added)
	{
		return Result<Handle>::Failure(added.error);
	}
	auto handle = api.RegisterComponent(added.value, object);
	if (!handle)
	{
		object->RemoveComponent(added.value);
		return Result<Handle>::Failure(handle.error);
	}
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle.value));
}

Result<DataView> RuntimeServices::GetComponents(BindingCall& call, const Handle& owner)
{
	auto& api = SceneApi(call);
	const auto object = TakeBindingResult(api.Resolve(ReadManagedHandle(owner)));
	const auto components = object->Components();
	auto* result = static_cast<Handle*>(call.arena.Allocate(components.size(), sizeof(Handle)));
	for (Size index = 0; index < components.size(); ++index)
	{
		result[index] = WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterComponent(components[index], object)));
	}
	return Result<DataView>::Success({result, components.size(), 1, 0});
}

Result<Utf8View> RuntimeServices::GetComponentType(BindingCall& call, const Handle& component)
{
	auto& api = SceneApi(call);
	const auto* value = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)));
	String type;
	if (const auto* behaviour = dynamic_cast<const ScriptBehaviourComponent*>(value))
	{
		type = behaviour->TypeName();
	}
	else
	{
		auto codec = api.componentTypes.FindFor(*value);
		type = codec ? codec.value->type : value->SerializationType();
		if (type.empty())
		{
			type = "ursa.Unexposed";
		}
	}
	return Result<Utf8View>::Success(call.arena.StringValue(type));
}

Result<RuntimeServices::Handle> RuntimeServices::GetComponentOwner(BindingCall& call, const Handle& component)
{
	auto& api = SceneApi(call);
	const auto* value = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)));
	const auto owner = value->GetGameObject();
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(api.Register(owner))));
}

Result<bool> RuntimeServices::GetComponentEnabled(BindingCall& call, const Handle& component)
{
	auto& api = SceneApi(call);
	return Result<bool>::Success(TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)))->IsEnabled());
}

Result<void> RuntimeServices::SetComponentEnabled(BindingCall& call, const Handle& component, bool enabled)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	return TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)))->SetEnabled(enabled);
}

Result<bool> RuntimeServices::IsComponentActive(BindingCall& call, const Handle& component)
{
	auto& api = SceneApi(call);
	const auto* value = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)));
	if (value->TryGetScene())
	{
		return api.scene.IsComponentActive(value->Id());
	}
	return Result<bool>::Success(value->IsEnabled() && TakeBindingResult(value->GetGameObject()->ActiveInHierarchy()));
}

Result<void> RuntimeServices::RemoveComponent(BindingCall& call, const Handle& component)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	const auto* value = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)));
	return value->GetGameObject()->RemoveComponent(const_cast<Component*>(value));
}

Result<RuntimeServices::Handle> RuntimeServices::AddBehaviour(BindingCall& call, const Handle& owner,
                                                              const Utf8View& typeName, UInt64 token)
{
	auto& api = SceneApi(call);
	api.RequireSceneMutation();
	api.RequireGameExecution();
	if (!api.behaviourRuntime || typeName.length == 0 || typeName.length > 4096)
	{
		return Result<Handle>::Failure("Behaviour creation requires a runtime and valid type identity");
	}
	const auto object = TakeBindingResult(api.Resolve(ReadManagedHandle(owner)));
	const auto type = ReadString(typeName);
	auto added = token == 0 ? api.behaviourRuntime->CreateAndAdd(object, type)
	                        : api.behaviourRuntime->AddExisting(object, type, token);
	if (!added)
	{
		return Result<Handle>::Failure(added.error);
	}
	auto cached = api.behaviourRuntime->BoundHandle(token);
	auto handle = cached ? cached : api.RegisterComponent(api.scene.GetComponent(added.value), object);
	if (!handle)
	{
		api.scene.RemoveComponent(added.value);
		return Result<Handle>::Failure(handle.error);
	}
	return Result<Handle>::Success(WriteManagedHandle<Handle>(handle.value));
}

Result<Utf8View> RuntimeServices::GetComponentPersistentId(BindingCall& call, const Handle& component)
{
	auto& api = SceneApi(call);
	const auto id = TakeBindingResult(api.ResolveComponent(ReadManagedHandle(component)))->PersistentId();
	return Result<Utf8View>::Success(call.arena.StringValue(id.ToString()));
}

Result<RuntimeServices::Handle> RuntimeServices::ResolveComponentPersistentId(BindingCall& call, const Utf8View& id)
{
	auto& api = SceneApi(call);
	const auto identity = TakeBindingResult(ResourceId::Parse(ReadString(id)));
	const auto component = TakeBindingResult(api.FindComponent(identity));
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterComponent(component))));
}
} // namespace Ursa::Scripting::Bindings
