using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Ursa;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeHandle
{
	public ulong sessionId;
	public uint index;
	public uint generation;
	public uint kind;
	public uint reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeApiV6
{
	public uint version;
	public uint size;
	public nint context;
	public delegate* unmanaged[Cdecl]<nint, uint, byte*, ulong, void> logUtf8;
	public nint sceneContext;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, NativeHandle*, int> createGameObject;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, ulong*, int> getGameObjectName;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, int> setGameObjectName;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint*, int> getGameObjectActive;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint, int> setGameObjectActive;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, NativeHandle*, int> getGameObjectParent;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, NativeHandle, int> setGameObjectParent;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, float*, ulong, int> getLocalTransform;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, float*, ulong, int> setLocalTransform;
	public delegate* unmanaged[Cdecl]<nint, uint, uint, uint*, int> getInputState;
	public delegate* unmanaged[Cdecl]<nint, float*, float*, double*, double*, ulong*, int> getTimeState;
	public delegate* unmanaged[Cdecl]<nint, uint, byte*, ulong, int> writeLog;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, byte*, ulong, ulong*, int> readFile;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, byte*, ulong, int> writeFile;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, NativeHandle*, int> addComponent;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint*, int> getComponentCount;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint, NativeHandle*, int> getComponentAt;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, ulong*, int> getComponentType;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, NativeHandle*, int> getComponentOwner;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint*, int> getComponentEnabled;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint, int> setComponentEnabled;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, uint*, int> isComponentActive;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, int> removeComponent;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, ulong, NativeHandle*, int> loadTexture;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, ulong, NativeHandle*, int> loadMaterial;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, ulong, NativeHandle*, int> addBehaviour;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, ulong*, int> getGameObjectPersistentId;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, NativeHandle*, int> resolveGameObjectPersistentId;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, ulong*, int> getComponentPersistentId;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, NativeHandle*, int> resolveComponentPersistentId;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, int> destroyGameObject;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, float*, ulong, int> getWorldPosition;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, float*, ulong, int> setWorldPosition;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, float*, ulong, int> getCameraProjection;
	public delegate* unmanaged[Cdecl]<nint, NativeHandle, float*, ulong, int> setCameraProjection;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, int> setWindowTitle;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, byte*, ulong, ulong*, int> resourceRequest;
	public delegate* unmanaged[Cdecl]<nint, byte*, ulong, byte*, ulong, int> submitRenderBatch;
}

internal static unsafe partial class EngineApi
{
	[ThreadStatic]
	private static NativeApiV6 api;
	[ThreadStatic]
	private static bool initialized;

	internal static void Clear()
	{
		api = default;
		initialized = false;
	}

	internal static void Initialize(nint nativeApi)
	{
		api = *(NativeApiV6*)nativeApi;
		initialized = true;
	}

	internal static void SubmitRenderBatch(ReadOnlySpan<byte> token, ReadOnlySpan<byte> batch)
	{
		RequireInitialized();
		if (api.submitRenderBatch == null)
		{
			throw new InvalidOperationException("This host does not have a native render-batch API.");
		}
		fixed (byte* tokenData = token)
		fixed (byte* batchData = batch)
		{
			int status = api.submitRenderBatch(api.sceneContext, tokenData, (ulong)token.Length, batchData,
			                                   (ulong)batch.Length);
			Check(status, "submit render batch");
		}
	}

	internal static GameObject CreateGameObject(string name)
	{
		RequireInitialized();
		if (api.createGameObject == null)
		{
			throw new InvalidOperationException("This host does not have an active Scene API.");
		}
		byte[] bytes = Encoding.UTF8.GetBytes(name);
		fixed(byte* data = bytes)
		{
			NativeHandle handle;
			int status = api.createGameObject(api.sceneContext, data, (ulong)bytes.Length, &handle);
			Check(status, "create GameObject");
			return GameObject.FromHandle(handle);
		}
	}

	internal static string GetName(NativeHandle handle)
	{
		RequireInitialized();
		ulong required = 0;
		int status = api.getGameObjectName(api.sceneContext, handle, null, 0, &required);
		if (status != 0 && status != 4)
		{
			Check(status, "get GameObject name");
		}
		if (required > 1024 * 1024)
		{
			throw new InvalidOperationException("Native GameObject name exceeds the managed string limit.");
		}
		byte[] bytes = new byte[(int)required];
		fixed(byte* data = bytes)
		{
			Check(api.getGameObjectName(api.sceneContext, handle, data, required, &required), "get GameObject name");
		}
		return Encoding.UTF8.GetString(bytes);
	}

	internal static void SetName(NativeHandle handle, string name)
	{
		RequireInitialized();
		byte[] bytes = Encoding.UTF8.GetBytes(name);
		fixed(byte* data = bytes)
		{
			Check(api.setGameObjectName(api.sceneContext, handle, data, (ulong)bytes.Length), "set GameObject name");
		}
	}

	internal static string GetGameObjectPersistentId(NativeHandle handle)
	{
		return GetPersistentId(api.getGameObjectPersistentId, handle, "get GameObject persistent ID");
	}

	internal static NativeHandle ResolveGameObjectPersistentId(string id)
	{
		return ResolvePersistentId(api.resolveGameObjectPersistentId, id, "resolve GameObject persistent ID");
	}

	internal static string GetComponentPersistentId(NativeHandle handle)
	{
		return GetPersistentId(api.getComponentPersistentId, handle, "get Component persistent ID");
	}

	internal static NativeHandle ResolveComponentPersistentId(string id)
	{
		return ResolvePersistentId(api.resolveComponentPersistentId, id, "resolve Component persistent ID");
	}

	internal static bool GetActive(NativeHandle handle)
	{
		RequireInitialized();
		uint active = 0;
		Check(api.getGameObjectActive(api.sceneContext, handle, &active), "get GameObject active state");
		return active != 0;
	}

	internal static void SetActive(NativeHandle handle, bool active)
	{
		RequireInitialized();
		Check(api.setGameObjectActive(api.sceneContext, handle, active ? 1u : 0u), "set GameObject active state");
	}

	internal static NativeHandle GetParent(NativeHandle handle)
	{
		RequireInitialized();
		NativeHandle parent;
		Check(api.getGameObjectParent(api.sceneContext, handle, &parent), "get GameObject parent");
		return parent;
	}

	internal static void SetParent(NativeHandle handle, NativeHandle parent)
	{
		RequireInitialized();
		Check(api.setGameObjectParent(api.sceneContext, handle, parent), "set GameObject parent");
	}

	internal static void DestroyGameObject(NativeHandle handle)
	{
		RequireInitialized();
		Check(api.destroyGameObject(api.sceneContext, handle), "destroy a GameObject");
	}

	internal static Vector3 GetWorldPosition(NativeHandle handle)
	{
		RequireInitialized();
		float* values = stackalloc float[3];
		Check(api.getWorldPosition(api.sceneContext, handle, values, 3), "read world position");
		return new Vector3(values[0], values[1], values[2]);
	}

	internal static void SetWorldPosition(NativeHandle handle, Vector3 value)
	{
		RequireInitialized();
		float* values = stackalloc float[] { value.X, value.Y, value.Z };
		Check(api.setWorldPosition(api.sceneContext, handle, values, 3), "write world position");
	}

	internal static void GetCameraProjection(NativeHandle handle, float* values)
	{
		RequireInitialized();
		if (api.getCameraProjection == null)
			throw new InvalidOperationException("Camera access is unavailable.");
		Check(api.getCameraProjection(api.sceneContext, handle, values, 3), "read Camera projection");
	}

	internal static void SetCameraProjection(NativeHandle handle, float* values)
	{
		RequireInitialized();
		if (api.setCameraProjection == null)
			throw new InvalidOperationException("Camera access is unavailable.");
		Check(api.setCameraProjection(api.sceneContext, handle, values, 3), "set Camera projection");
	}









	internal static void SetWindowTitle(string title)
	{
		RequireInitialized();
		if (api.setWindowTitle == null)
			throw new InvalidOperationException("Window title access is unavailable.");
		byte[] bytes = Encoding.UTF8.GetBytes(title ?? throw new ArgumentNullException(nameof(title)));
		fixed(byte* text = bytes)
		{
			Check(api.setWindowTitle(api.sceneContext, text, (ulong)bytes.Length), "set application window title");
		}
	}

	internal static void GetTransform(NativeHandle handle, float* values)
	{
		RequireInitialized();
		Check(api.getLocalTransform(api.sceneContext, handle, values, 10), "get Transform");
	}

	internal static void SetTransform(NativeHandle handle, float* values)
	{
		RequireInitialized();
		Check(api.setLocalTransform(api.sceneContext, handle, values, 10), "set Transform");
	}

	internal static bool GetKey(uint key, uint phase)
	{
		RequireInitialized();
		uint state = 0;
		Check(api.getInputState(api.sceneContext, key, phase, &state), "read Input state");
		return state != 0;
	}

	internal static void GetTime(float* delta, float* fixedDelta, double* time, double* fixedTime, ulong* frame)
	{
		RequireInitialized();
		Check(api.getTimeState(api.sceneContext, delta, fixedDelta, time, fixedTime, frame), "read Time state");
	}

	internal static void WriteLog(uint level, string message)
	{
		RequireInitialized();
		byte[] bytes = Encoding.UTF8.GetBytes(message ?? string.Empty);
		fixed(byte* data = bytes)
		{
			Check(api.writeLog(api.context, level, data, (ulong)bytes.Length), "write a log message");
		}
	}

	internal static byte[] ReadFile(string path)
	{
		RequireInitialized();
		byte[] pathBytes = Encoding.UTF8.GetBytes(path);
		fixed(byte* pathData = pathBytes)
		{
			ulong required = 0;
			int status = api.readFile(api.context, pathData, (ulong)pathBytes.Length, null, 0, &required);
			if (status != 0 && status != 4)
			{
				Check(status, "read a VFS file");
			}
			if (required > 256ul * 1024ul * 1024ul)
			{
				throw new InvalidOperationException("VFS file exceeds the managed read limit.");
			}
			byte[] bytes = new byte[(int)required];
			fixed(byte* output = bytes)
			{
				Check(api.readFile(api.context, pathData, (ulong)pathBytes.Length, output, required, &required),
				      "read a VFS file");
			}
			return bytes;
		}
	}

	internal static void WriteFile(string path, byte[] bytes)
	{
		RequireInitialized();
		byte[] pathBytes = Encoding.UTF8.GetBytes(path);
		fixed(byte* pathData = pathBytes) fixed(byte* data = bytes)
		{
			Check(api.writeFile(api.context, pathData, (ulong)pathBytes.Length, data, (ulong)bytes.Length),
			      "write a VFS file");
		}
	}

	internal static NativeHandle AddComponent(NativeHandle owner, string typeId)
	{
		RequireInitialized();
		byte[] bytes = Encoding.UTF8.GetBytes(typeId);
		fixed(byte* data = bytes)
		{
			NativeHandle component;
			Check(api.addComponent(api.sceneContext, owner, data, (ulong)bytes.Length, &component), "add a Component");
			return component;
		}
	}

	internal static uint GetComponentCount(NativeHandle owner)
	{
		RequireInitialized();
		uint count = 0;
		Check(api.getComponentCount(api.sceneContext, owner, &count), "query Components");
		return count;
	}

	internal static NativeHandle GetComponentAt(NativeHandle owner, uint index)
	{
		RequireInitialized();
		NativeHandle component;
		Check(api.getComponentAt(api.sceneContext, owner, index, &component), "query a Component");
		return component;
	}

	internal static string GetComponentType(NativeHandle component)
	{
		RequireInitialized();
		ulong required = 0;
		int status = api.getComponentType(api.sceneContext, component, null, 0, &required);
		if (status != 0 && status != 4)
		{
			Check(status, "read a Component type");
		}
		if (required > 4096)
		{
			throw new InvalidOperationException("Native Component type ID is too long.");
		}
		byte[] bytes = new byte[(int)required];
		fixed(byte* output = bytes)
		{
			Check(api.getComponentType(api.sceneContext, component, output, required, &required),
			      "read a Component type");
		}
		return Encoding.UTF8.GetString(bytes);
	}

	internal static NativeHandle GetComponentOwner(NativeHandle component)
	{
		RequireInitialized();
		NativeHandle owner;
		Check(api.getComponentOwner(api.sceneContext, component, &owner), "read a Component owner");
		return owner;
	}

	internal static bool GetComponentEnabled(NativeHandle component)
	{
		RequireInitialized();
		uint enabled = 0;
		Check(api.getComponentEnabled(api.sceneContext, component, &enabled), "read Component enabled state");
		return enabled != 0;
	}

	internal static void SetComponentEnabled(NativeHandle component, bool enabled)
	{
		RequireInitialized();
		Check(api.setComponentEnabled(api.sceneContext, component, enabled ? 1u : 0u), "set Component enabled state");
	}

	internal static bool IsComponentActive(NativeHandle component)
	{
		RequireInitialized();
		uint active = 0;
		Check(api.isComponentActive(api.sceneContext, component, &active), "read Component active state");
		return active != 0;
	}

	internal static void RemoveComponent(NativeHandle component)
	{
		RequireInitialized();
		Check(api.removeComponent(api.sceneContext, component), "remove a Component");
	}

	internal static NativeHandle LoadTexture(string guid, ulong localFileId)
	{
		return LoadAsset(guid, localFileId, true);
	}

	internal static NativeHandle LoadMaterial(string guid, ulong localFileId)
	{
		return LoadAsset(guid, localFileId, false);
	}

	internal static NativeHandle AddBehaviour(NativeHandle owner, string typeName, ulong token)
	{
		RequireInitialized();
		byte[] bytes = Encoding.UTF8.GetBytes(typeName);
		fixed(byte* data = bytes)
		{
			NativeHandle component;
			Check(api.addBehaviour(api.sceneContext, owner, data, (ulong)bytes.Length, token, &component),
			      "add a managed Behaviour");
			return component;
		}
	}

	private static string
	GetPersistentId(delegate* unmanaged[Cdecl]<nint, NativeHandle, byte*, ulong, ulong*, int> function,
	                NativeHandle handle, string operation)
	{
		RequireInitialized();
		if (function == null)
		{
			throw new InvalidOperationException("This host does not support persistent object references.");
		}
		ulong required = 0;
		int status = function(api.sceneContext, handle, null, 0, &required);
		if (status != 0 && status != 4)
		{
			Check(status, operation);
		}
		if (required != 36)
		{
			throw new InvalidOperationException($"Native persistent ID has invalid length {required}.");
		}
		byte[] bytes = new byte[(int)required];
		fixed(byte* output = bytes)
		{
			Check(function(api.sceneContext, handle, output, required, &required), operation);
		}
		return Encoding.UTF8.GetString(bytes);
	}

	private static NativeHandle
	ResolvePersistentId(delegate* unmanaged[Cdecl]<nint, byte*, ulong, NativeHandle*, int> function, string id,
	                    string operation)
	{
		RequireInitialized();
		if (function == null)
		{
			throw new InvalidOperationException("This host does not support persistent object references.");
		}
		byte[] bytes = Encoding.UTF8.GetBytes(id);
		if (bytes.Length != 36)
		{
			throw new ArgumentException("Persistent ID must be a 36-character GUID.", nameof(id));
		}
		fixed(byte* input = bytes)
		{
			NativeHandle handle;
			Check(function(api.sceneContext, input, (ulong)bytes.Length, &handle), operation);
			return handle;
		}
	}

	private static NativeHandle LoadAsset(string guid, ulong localFileId, bool texture)
	{
		RequireInitialized();
		if (!Guid.TryParse(guid, out _))
		{
			throw new ArgumentException("Asset GUID is invalid.", nameof(guid));
		}
		byte[] bytes = Encoding.UTF8.GetBytes(guid);
		fixed(byte* data = bytes)
		{
			NativeHandle asset;
			var callback = texture ? api.loadTexture : api.loadMaterial;
			if (callback == null || api.sceneContext == 0)
				throw new InvalidOperationException("Asset loading requires an active Scene/resource session.");
			Check(callback(api.sceneContext, data, (ulong)bytes.Length, localFileId, &asset),
			      texture ? "load a Texture asset" : "load a Material asset");
			return asset;
		}
	}

	private static void RequireInitialized()
	{
		if (!initialized)
		{
			throw new InvalidOperationException("The Ursa managed API has not been initialized.");
		}
	}

	private static void Check(int status, string operation)
	{
		if (status != 0)
		{
			throw new InvalidOperationException($"Native API failed to {operation} (status {status}).");
		}
	}
}
