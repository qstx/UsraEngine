using Ursa.Bindings;

namespace Ursa;

[BindingModule("Runtime", Required = true)]
[NativeHeader("Scripting/Bindings/RuntimeServices.hpp")]
internal static partial class NativeRuntimeBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::CreateGameObject")]
	internal static partial NativeHandle CreateGameObject(string name);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetName")]
	internal static partial string GetName([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetName")]
	internal static partial void SetName([HandleKind(1)] NativeHandle handle, string name);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetActive")]
	internal static partial bool GetActive([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetActive")]
	internal static partial void SetActive([HandleKind(1)] NativeHandle handle, bool active);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetParent")]
	internal static partial NativeHandle GetParent([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetChildren")]
	internal static partial NativeHandle[] GetChildren([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetParent")]
	internal static partial void SetParent([HandleKind(1)] NativeHandle handle, NativeHandle parent);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::DestroyGameObject")]
	internal static partial void DestroyGameObject([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetLayer")]
	internal static partial int GetLayer([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetLayer")]
	internal static partial void SetLayer([HandleKind(1)] NativeHandle handle, int layer);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::CreatePrimitive")]
	internal static partial NativeHandle CreatePrimitive(PrimitiveType type);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetGameObjectPersistentId")]
	internal static partial string GetGameObjectPersistentId([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::ResolveGameObjectPersistentId")]
	internal static partial NativeHandle ResolveGameObjectPersistentId(string id);

	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::AddComponent")]
	internal static partial NativeHandle AddComponent([HandleKind(1)] NativeHandle owner, string typeId);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetComponents")]
	internal static partial NativeHandle[] GetComponents([HandleKind(1)] NativeHandle owner);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetComponentType")]
	internal static partial string GetComponentType([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetComponentOwner")]
	internal static partial NativeHandle GetComponentOwner([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetComponentEnabled")]
	internal static partial bool GetComponentEnabled([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetComponentEnabled")]
	internal static partial void SetComponentEnabled([HandleKind(2)] NativeHandle component, bool enabled);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::IsComponentActive")]
	internal static partial bool IsComponentActive([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::RemoveComponent")]
	internal static partial void RemoveComponent([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::AddBehaviour")]
	internal static partial NativeHandle AddBehaviour([HandleKind(1)] NativeHandle owner, string typeName, ulong token);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetComponentPersistentId")]
	internal static partial string GetComponentPersistentId([HandleKind(2)] NativeHandle component);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::ResolveComponentPersistentId")]
	internal static partial NativeHandle ResolveComponentPersistentId(string id);

	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetLocalTransform")]
	internal static partial LocalTransformValue GetLocalTransform([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetLocalTransform")]
	internal static partial void SetLocalTransform([HandleKind(1)] NativeHandle handle, LocalTransformValue value);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetWorldPosition")]
	internal static partial Vector3 GetWorldPosition([HandleKind(1)] NativeHandle handle);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetWorldPosition")]
	internal static partial void SetWorldPosition([HandleKind(1)] NativeHandle handle, Vector3 value);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetCameraProjection")]
	internal static partial CameraProjection GetCameraProjection([HandleKind(2)] NativeHandle camera);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetCameraProjection")]
	internal static partial void SetCameraProjection([HandleKind(2)] NativeHandle camera, CameraProjection value);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetCameraRenderingSettings")]
	internal static partial CameraRenderingSettings GetCameraRenderingSettings([HandleKind(2)] NativeHandle camera);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetCameraRenderingSettings")]
	internal static partial void SetCameraRenderingSettings([HandleKind(2)] NativeHandle camera, CameraRenderingSettings value);

	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetKey")]
	internal static partial bool GetKey(uint key, uint phase);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::GetTime")]
	internal static partial TimeState GetTime();
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::WriteLog")]
	internal static partial void WriteLog(uint level, string message);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::ReportHostMessage")]
	internal static partial void ReportHostMessage(uint level, string message);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::ReadFile")]
	internal static partial byte[] ReadFile(string path);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::WriteFile")]
	internal static partial void WriteFile(string path, System.ReadOnlySpan<byte> bytes);
	[NativeMethod("Ursa::Scripting::Bindings::RuntimeServices::SetWindowTitle")]
	internal static partial void SetWindowTitle(string title);
}
