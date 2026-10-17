#pragma once

#include <BindingTypes.generated.hpp>
#include <Scripting/Bindings/BindingHostServices.hpp>

namespace Ursa::Scripting
{
class ManagedSceneApi;
}

namespace Ursa::Scripting::Bindings
{
struct RuntimeServices
{
	using Handle = Wire::Value_Ursa_NativeHandle;
	using LocalValue = Wire::Value_Ursa_LocalTransformValue;
	using VectorValue = Wire::Value_Ursa_Vector3;
	using Projection = Wire::Value_Ursa_CameraProjection;
	using CameraSettingsValue = Wire::Value_Ursa_CameraRenderingSettings;
	static ManagedSceneApi& SceneApi(BindingCall& call);
	static Result<Handle> CreateGameObject(BindingCall& call, const Utf8View& name);
	static Result<Utf8View> GetName(BindingCall& call, const Handle& handle);
	static Result<void> SetName(BindingCall& call, const Handle& handle, const Utf8View& name);
	static Result<bool> GetActive(BindingCall& call, const Handle& handle);
	static Result<void> SetActive(BindingCall& call, const Handle& handle, bool active);
	static Result<Handle> GetParent(BindingCall& call, const Handle& handle);
	static Result<void> SetParent(BindingCall& call, const Handle& handle, const Handle& parent);
	static Result<void> DestroyGameObject(BindingCall& call, const Handle& handle);
	static Result<Int32> GetLayer(BindingCall& call, const Handle& handle);
	static Result<void> SetLayer(BindingCall& call, const Handle& handle, Int32 layer);
	static Result<Handle> CreatePrimitive(BindingCall& call, Int32 type);
	static Result<Utf8View> GetGameObjectPersistentId(BindingCall& call, const Handle& handle);
	static Result<Handle> ResolveGameObjectPersistentId(BindingCall& call, const Utf8View& id);
	static Result<Handle> AddComponent(BindingCall& call, const Handle& owner, const Utf8View& typeId);
	static Result<DataView> GetComponents(BindingCall& call, const Handle& owner);
	static Result<Utf8View> GetComponentType(BindingCall& call, const Handle& component);
	static Result<Handle> GetComponentOwner(BindingCall& call, const Handle& component);
	static Result<bool> GetComponentEnabled(BindingCall& call, const Handle& component);
	static Result<void> SetComponentEnabled(BindingCall& call, const Handle& component, bool enabled);
	static Result<bool> IsComponentActive(BindingCall& call, const Handle& component);
	static Result<void> RemoveComponent(BindingCall& call, const Handle& component);
	static Result<Handle> AddBehaviour(BindingCall& call, const Handle& owner, const Utf8View& typeName, UInt64 token);
	static Result<Utf8View> GetComponentPersistentId(BindingCall& call, const Handle& component);
	static Result<Handle> ResolveComponentPersistentId(BindingCall& call, const Utf8View& id);
	static Result<LocalValue> GetLocalTransform(BindingCall& call, const Handle& handle);
	static Result<void> SetLocalTransform(BindingCall& call, const Handle& handle, const LocalValue& value);
	static Result<VectorValue> GetWorldPosition(BindingCall& call, const Handle& handle);
	static Result<void> SetWorldPosition(BindingCall& call, const Handle& handle, const VectorValue& value);
	static Result<Projection> GetCameraProjection(BindingCall& call, const Handle& camera);
	static Result<void> SetCameraProjection(BindingCall& call, const Handle& camera, const Projection& value);
	static Result<CameraSettingsValue> GetCameraRenderingSettings(BindingCall& call, const Handle& camera);
	static Result<void> SetCameraRenderingSettings(BindingCall& call, const Handle& camera,
	                                               const CameraSettingsValue& value);
	static BindingHostAccess HostApi(BindingCall& call);
	static Result<bool> GetKey(BindingCall& call, UInt32 key, UInt32 phase);
	static Result<Wire::Value_Ursa_TimeState> GetTime(BindingCall& call);
	static Result<void> WriteLog(BindingCall& call, UInt32 level, const Utf8View& message);
	static Result<void> ReportHostMessage(BindingCall& call, UInt32 level, const Utf8View& message);
	static Result<DataView> ReadFile(BindingCall& call, const Utf8View& path);
	static Result<void> WriteFile(BindingCall& call, const Utf8View& path, const DataView& bytes);
	static Result<void> SetWindowTitle(BindingCall& call, const Utf8View& title);
};
} // namespace Ursa::Scripting::Bindings
