using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using Ursa;

namespace Ursa.ManagedHost;

public static unsafe partial class Bootstrap
{
	[StructLayout(LayoutKind.Sequential)]
	public struct NativeHandle
	{
		public ulong sessionId;
		public uint index;
		public uint generation;
		public uint kind;
		public uint reserved;
	}

	[StructLayout(LayoutKind.Sequential)]
	internal struct NativeApiV6
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

	private static readonly byte[] initializedMessage = Encoding.UTF8.GetBytes("Ursa 托管运行时已初始化");

	private static void LogFailure(string message)
	{
		try
		{
			Debug.LogError(message);
		}
		catch
		{
			Console.Error.WriteLine(message);
		}
	}

	internal static int Validate(nint arguments, int argumentSize)
	{
		if (arguments == 0 || argumentSize < sizeof(NativeApiV6))
		{
			return 1;
		}
		NativeApiV6* api = (NativeApiV6*)arguments;
		if (api->version != 6 || api->size < (uint)sizeof(NativeApiV6))
		{
			return 2;
		}
		return api->logUtf8 == null || api->submitRenderBatch == null ? 3 : 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int Initialize(nint arguments, int argumentSize)
	{
		int validation = Validate(arguments, argumentSize);
		if (validation != 0)
		{
			return validation;
		}

		NativeApiV6* api = (NativeApiV6*)arguments;
		Ursa.EngineApi.Initialize(arguments);
		fixed(byte* message = initializedMessage)
		{
			api->logUtf8(api->context, 1, message, (ulong)initializedMessage.Length);
		}
		api->logUtf8(api->context, 2, null, 0);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int CreateBehaviour(byte* typeNameBytes, ulong typeNameLength, NativeHandle* gameObjectHandle,
	                                  ulong* outputToken)
	{
		if (typeNameBytes == null || typeNameLength == 0 || typeNameLength > 4096 || gameObjectHandle == null ||
		    outputToken == null)
		{
			return 1;
		}
		try
		{
			string typeName = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(typeNameBytes, (int)typeNameLength));
			Ursa.NativeHandle runtimeHandle =
			    new() { sessionId = gameObjectHandle->sessionId, index = gameObjectHandle->index,
				        generation = gameObjectHandle->generation, kind = gameObjectHandle->kind,
				        reserved = gameObjectHandle->reserved };
			var owner = GameObject.FromHandle(runtimeHandle);
			*outputToken = BehaviourRegistry.Create(typeName, owner);
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管行为创建失败：{exception}");
			return 5;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int BindBehaviour(ulong token, NativeHandle* componentHandle)
	{
		if (componentHandle == null)
		{
			return 1;
		}
		try
		{
			Ursa.NativeHandle runtimeHandle =
			    new() { sessionId = componentHandle->sessionId, index = componentHandle->index,
				        generation = componentHandle->generation, kind = componentHandle->kind,
				        reserved = componentHandle->reserved };
			BehaviourRegistry.BindNative(token, runtimeHandle);
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管行为组件绑定失败：{exception}");
			return 2;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int InvokeBehaviour(ulong token, uint eventId)
	{
		if (eventId > 7)
		{
			return 1;
		}
		try
		{
			BehaviourRegistry.Invoke(token, eventId);
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管行为事件 {eventId} 抛出异常：{exception}");
			return 2;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int DestroyBehaviour(ulong token)
	{
		try
		{
			BehaviourRegistry.Destroy(token);
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管行为销毁失败：{exception}");
			return 1;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int SerializeBehaviour(ulong token, byte* output, ulong capacity, ulong* required)
	{
		if (required == null || capacity > 64ul * 1024ul * 1024ul || (output == null && capacity != 0))
		{
			return 1;
		}
		try
		{
			byte[] payload = Encoding.UTF8.GetBytes(BehaviourRegistry.Serialize(token));
			*required = (ulong)payload.Length;
			if (capacity < (ulong)payload.Length)
			{
				return 4;
			}
			payload.AsSpan().CopyTo(new Span<byte>(output, payload.Length));
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管行为序列化失败：{exception}");
			return 2;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int DeserializeBehaviour(ulong token, byte* payloadBytes, ulong payloadLength)
	{
		if (payloadBytes == null || payloadLength == 0 || payloadLength > 64ul * 1024ul * 1024ul)
		{
			return 1;
		}
		try
		{
			string payload = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(payloadBytes, (int)payloadLength));
			BehaviourRegistry.Deserialize(token, payload);
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管行为反序列化失败：{exception}");
			return 2;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]

	public static int LoadScriptAssembly(byte* assemblyBytes, ulong assemblyLength)
	{
		if (assemblyBytes == null || assemblyLength == 0 || assemblyLength > 256ul * 1024ul * 1024ul)
		{
			return 1;
		}
		try
		{
			byte[] bytes = new byte[(int)assemblyLength];
			new ReadOnlySpan<byte>(assemblyBytes, bytes.Length).CopyTo(bytes);
			using var stream = new MemoryStream(bytes, writable: false);
			ScriptSessions.LoadAssembly(bytes);
			return 0;
		}
		catch (Exception exception)
		{
			LogFailure($"托管脚本程序集加载失败：{exception}");
			return 2;
		}
	}
}
