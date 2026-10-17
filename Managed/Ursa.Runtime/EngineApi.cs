using System;
using System.Runtime.InteropServices;

namespace Ursa;

[StructLayout(LayoutKind.Sequential)]
[Ursa.Bindings.WireValue]
internal struct NativeHandle
{
	public ulong sessionId;
	public uint index;
	public uint generation;
	public uint kind;
	public uint reserved;
}

internal static unsafe partial class EngineApi
{
	internal static GameObject CreateGameObject(string name)
	{
		return GameObject.FromHandle(NativeRuntimeBindings.CreateGameObject(name));
	}

	internal static string GetName(NativeHandle handle)
	{
		return NativeRuntimeBindings.GetName(handle);
	}

	internal static void SetName(NativeHandle handle, string name)
	{
		NativeRuntimeBindings.SetName(handle, name);
	}

	internal static string GetGameObjectPersistentId(NativeHandle handle)
	{
		return NativeRuntimeBindings.GetGameObjectPersistentId(handle);
	}

	internal static NativeHandle ResolveGameObjectPersistentId(string id)
	{
		return NativeRuntimeBindings.ResolveGameObjectPersistentId(id);
	}

	internal static string GetComponentPersistentId(NativeHandle handle)
	{
		return NativeRuntimeBindings.GetComponentPersistentId(handle);
	}

	internal static NativeHandle ResolveComponentPersistentId(string id)
	{
		return NativeRuntimeBindings.ResolveComponentPersistentId(id);
	}

	internal static bool GetActive(NativeHandle handle)
	{
		return NativeRuntimeBindings.GetActive(handle);
	}

	internal static void SetActive(NativeHandle handle, bool active)
	{
		NativeRuntimeBindings.SetActive(handle, active);
	}

	internal static NativeHandle GetParent(NativeHandle handle)
	{
		return NativeRuntimeBindings.GetParent(handle);
	}

	internal static void SetParent(NativeHandle handle, NativeHandle parent)
	{
		NativeRuntimeBindings.SetParent(handle, parent);
	}

	internal static void DestroyGameObject(NativeHandle handle)
	{
		NativeRuntimeBindings.DestroyGameObject(handle);
	}

	internal static Vector3 GetWorldPosition(NativeHandle handle)
	{
		return NativeRuntimeBindings.GetWorldPosition(handle);
	}

	internal static void SetWorldPosition(NativeHandle handle, Vector3 value)
	{
		NativeRuntimeBindings.SetWorldPosition(handle, value);
	}

	internal static void SetWindowTitle(string title)
	{
		NativeRuntimeBindings.SetWindowTitle(title ?? throw new ArgumentNullException(nameof(title)));
	}

	internal static bool GetKey(uint key, uint phase)
	{
		return NativeRuntimeBindings.GetKey(key, phase);
	}

	internal static void WriteLog(uint level, string message)
	{
		NativeRuntimeBindings.WriteLog(level, message ?? string.Empty);
	}

	internal static byte[] ReadFile(string path)
	{
		return NativeRuntimeBindings.ReadFile(path);
	}

	internal static void WriteFile(string path, byte[] bytes)
	{
		ArgumentNullException.ThrowIfNull(bytes);
		NativeRuntimeBindings.WriteFile(path, bytes);
	}

	internal static NativeHandle AddComponent(NativeHandle owner, string typeId)
	{
		return NativeRuntimeBindings.AddComponent(owner, typeId);
	}

	internal static string GetComponentType(NativeHandle component)
	{
		return NativeRuntimeBindings.GetComponentType(component);
	}

	internal static NativeHandle GetComponentOwner(NativeHandle component)
	{
		return NativeRuntimeBindings.GetComponentOwner(component);
	}

	internal static bool GetComponentEnabled(NativeHandle component)
	{
		return NativeRuntimeBindings.GetComponentEnabled(component);
	}

	internal static void SetComponentEnabled(NativeHandle component, bool enabled)
	{
		NativeRuntimeBindings.SetComponentEnabled(component, enabled);
	}

	internal static bool IsComponentActive(NativeHandle component)
	{
		return NativeRuntimeBindings.IsComponentActive(component);
	}

	internal static void RemoveComponent(NativeHandle component)
	{
		NativeRuntimeBindings.RemoveComponent(component);
	}

	internal static NativeHandle LoadTexture(string guid, ulong localFileId)
	{
		return NativeResourceBindings.LoadTexture(new() { Guid = guid, LocalFileId = localFileId });
	}

	internal static NativeHandle LoadMaterial(string guid, ulong localFileId)
	{
		return NativeMaterialBindings.Load(new() { Guid = guid, LocalFileId = localFileId });
	}

	internal static NativeHandle AddBehaviour(NativeHandle owner, string typeName, ulong token)
	{
		return NativeRuntimeBindings.AddBehaviour(owner, typeName, token);
	}

}
