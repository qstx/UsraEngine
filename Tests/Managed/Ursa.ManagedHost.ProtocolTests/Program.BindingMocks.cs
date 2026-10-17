using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Ursa.Bindings;
using Ursa.Bindings.Generated;
using WireHandle = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_NativeHandle;
using WireTransform = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_LocalTransformValue;
using WireVector = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_Vector3;
using WireProjection = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_CameraProjection;
using WireCamera = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_CameraRenderingSettings;
using WireTime = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_TimeState;
using WireAsset = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_NativeAssetReference;
using WireGroup = Ursa.Bindings.Generated.RuntimeModule.Value_Ursa_ComputeThreadGroupSize;

internal static unsafe partial class Program
{
	private static BindingCallbackRegistration InstallRuntimeBindings()
	{
		var registration = new BindingCallbackRegistration(RuntimeModule.Hash, (uint)(sizeof(RuntimeModule.Table) / sizeof(nint)), sizeof(RuntimeModule.Table));
		var table = (RuntimeModule.Table*)registration.Functions;
		table->NativeRuntimeBindings_CreateGameObject = &BoundCreateGameObject;
		table->NativeRuntimeBindings_GetName = &BoundGetName;
		table->NativeRuntimeBindings_SetName = &BoundSetName;
		table->NativeRuntimeBindings_GetActive = &BoundGetActive;
		table->NativeRuntimeBindings_SetActive = &BoundSetActive;
		table->NativeRuntimeBindings_GetParent = &BoundGetParent;
		table->NativeRuntimeBindings_SetParent = &BoundSetParent;
		table->NativeRuntimeBindings_DestroyGameObject = &BoundDestroy;
		table->NativeRuntimeBindings_GetLayer = &BoundGetLayer;
		table->NativeRuntimeBindings_SetLayer = &BoundSetLayer;
		table->NativeRuntimeBindings_CreatePrimitive = &BoundPrimitive;
		table->NativeRuntimeBindings_GetGameObjectPersistentId = &BoundGetId;
		table->NativeRuntimeBindings_ResolveGameObjectPersistentId = &BoundResolveId;
		table->NativeRuntimeBindings_AddComponent = &BoundAddComponent;
		table->NativeRuntimeBindings_GetComponents = &BoundGetComponents;
		table->NativeRuntimeBindings_GetComponentType = &BoundGetComponentType;
		table->NativeRuntimeBindings_GetComponentOwner = &BoundGetComponentOwner;
		table->NativeRuntimeBindings_GetComponentEnabled = &BoundGetComponentEnabled;
		table->NativeRuntimeBindings_SetComponentEnabled = &BoundSetComponentEnabled;
		table->NativeRuntimeBindings_IsComponentActive = &BoundIsComponentActive;
		table->NativeRuntimeBindings_RemoveComponent = &BoundRemoveComponent;
		table->NativeRuntimeBindings_AddBehaviour = &BoundAddBehaviour;
		table->NativeRuntimeBindings_GetComponentPersistentId = &BoundGetComponentId;
		table->NativeRuntimeBindings_ResolveComponentPersistentId = &BoundResolveComponentId;
		table->NativeRuntimeBindings_GetLocalTransform = &BoundGetLocalTransform;
		table->NativeRuntimeBindings_SetLocalTransform = &BoundSetLocalTransform;
		table->NativeRuntimeBindings_GetWorldPosition = &BoundGetWorldPosition;
		table->NativeRuntimeBindings_SetWorldPosition = &BoundSetWorldPosition;
		table->NativeRuntimeBindings_GetCameraProjection = &BoundGetProjection;
		table->NativeRuntimeBindings_SetCameraProjection = &BoundSetProjection;
		table->NativeRuntimeBindings_GetCameraRenderingSettings = &BoundGetCameraSettings;
		table->NativeRuntimeBindings_SetCameraRenderingSettings = &BoundSetCameraSettings;
		table->NativeRuntimeBindings_GetKey = &BoundGetKey;
		table->NativeRuntimeBindings_GetTime = &BoundGetTime;
		table->NativeRuntimeBindings_WriteLog = &BoundWriteLog;
		table->NativeRuntimeBindings_ReportHostMessage = &BoundReportHostMessage;
		table->NativeRuntimeBindings_ReadFile = &BoundReadFile;
		table->NativeRuntimeBindings_WriteFile = &BoundWriteFile;
		table->NativeRuntimeBindings_SetWindowTitle = &BoundSetWindowTitle;
		table->NativeMaterialBindings_Load = &BoundLoadMaterial;
		table->NativeResourceBindings_LoadTexture = &BoundLoadTexture;
		table->NativeResourceBindings_IsValid = &BoundResourceValid;
		table->NativeResourceBindings_FindComputeKernel = &BoundFindComputeKernel;
		table->NativeResourceBindings_GetComputeThreadGroupSize = &BoundGetComputeThreadGroupSize;
		RuntimeModule.Bind(registration.Descriptor, registration.Context);
		return registration;
	}

	private static FakeObject BoundObject(WireHandle handle)
	{
		if (handle.F_sessionId != 1 || handle.F_kind != 1 || handle.F_generation != 1 || handle.F_reserved != 0 ||
			!fakeObjects.TryGetValue(handle.F_index, out FakeObject? value))
			throw new InvalidOperationException("Stale or invalid protocol fixture GameObject handle.");
		return value;
	}

	private static WireHandle BoundHandle(uint index) => new() { F_sessionId = 1, F_index = index, F_generation = 1, F_kind = 1 };
	private static WireHandle CreateBoundObject(string name)
	{
		uint index = nextFakeObject++;
		fakeObjects.Add(index, new FakeObject { name = name, persistentId = $"00000000-0000-4000-8000-{index:D12}" });
		return BoundHandle(index);
	}

	private static int BoundCall(nint context, BindingOutcome* outcome, Action<BindingArena> body)
	{
		*outcome = default;
		BindingArena? arena = null;
		try
		{
			BindingCallbackRegistration.Require(context, 0);
			arena = new BindingArena();
			body(arena);
			arena.Transfer(ref *outcome);
			arena = null;
			return 0;
		}
		catch (Exception error) { return BindingMemory.Fail(error, outcome, "Protocol.GameObject", context); }
		finally { arena?.Dispose(); }
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundCreateGameObject(nint context, Utf8View* name, WireHandle* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = CreateBoundObject(BindingMemory.String(*name, false)!));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetName(nint context, WireHandle* handle, Utf8View* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = arena.String(BoundObject(*handle).name, false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetName(nint context, WireHandle* handle, Utf8View* value, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => BoundObject(*handle).name = BindingMemory.String(*value, false)!);

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetActive(nint context, WireHandle* handle, uint* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = BoundObject(*handle).active ? 1u : 0u);

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetActive(nint context, WireHandle* handle, uint active, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => BoundObject(*handle).active = BindingMemory.Bool(active));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetParent(nint context, WireHandle* handle, WireHandle* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = BoundObject(*handle).parent.sessionId == 0 ? default : BoundHandle(BoundObject(*handle).parent.index));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetParent(nint context, WireHandle* handle, WireHandle* parent, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena =>
		{
			FakeObject value = BoundObject(*handle);
			if (parent->F_sessionId != 0) _ = BoundObject(*parent);
			value.parent = new() { sessionId = parent->F_sessionId, index = parent->F_index, generation = parent->F_generation, kind = parent->F_kind };
		});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundDestroy(nint context, WireHandle* handle, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		_ = BoundObject(*handle);
		fakeObjects.Remove(handle->F_index);
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetLayer(nint context, WireHandle* handle, int* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = BoundObject(*handle).layer);

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetLayer(nint context, WireHandle* handle, int layer, BindingOutcome* outcome) => BoundCall(context, outcome, _ =>
	{
		if (layer < 0 || layer > 31) throw new ArgumentOutOfRangeException(nameof(layer));
		BoundObject(*handle).layer = layer;
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundPrimitive(nint context, int type, WireHandle* result, BindingOutcome* outcome) => BoundCall(context, outcome, _ =>
	{
		if (type != 0) throw new ArgumentOutOfRangeException(nameof(type));
		*result = CreateBoundObject("Cube");
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetId(nint context, WireHandle* handle, Utf8View* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = arena.String(BoundObject(*handle).persistentId, false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundResolveId(nint context, Utf8View* id, WireHandle* result, BindingOutcome* outcome) => BoundCall(context, outcome, _ =>
	{
		string value = BindingMemory.String(*id, false)!;
		var found = fakeObjects.Single(pair => pair.Value.persistentId == value);
		*result = BoundHandle(found.Key);
	});

	private static WireHandle BoundComponentHandle(uint index) => new() { F_sessionId = 1, F_index = index, F_generation = 1, F_kind = 2 };
	private static FakeComponent BoundComponent(WireHandle handle)
	{
		if (handle.F_sessionId != 1 || handle.F_kind != 2 || handle.F_generation != 1 || handle.F_reserved != 0 ||
			!fakeComponents.TryGetValue(handle.F_index, out FakeComponent? value))
			throw new InvalidOperationException("Stale or invalid protocol fixture Component handle.");
		return value;
	}

	private static WireHandle CreateBoundComponent(WireHandle owner, string type, bool behaviour)
	{
		FakeObject value = BoundObject(owner);
		uint index = behaviour ? nextFakeScriptComponent++ : nextFakeComponent++;
		fakeComponents.Add(index, new FakeComponent
		{
			owner = owner.F_index, typeId = type, persistentId = $"00000000-0000-4000-8001-{index:D12}",
			values = type == "ursa.Camera" ? new[] { 0.78539816f, 0.1f, 100.0f } : new[] { 1.0f, 1.0f, 1.0f, 1.0f }
		});
		value.components.Add(index);
		return BoundComponentHandle(index);
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundAddComponent(nint context, WireHandle* owner, Utf8View* type, WireHandle* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = CreateBoundComponent(*owner, BindingMemory.String(*type, false)!, false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetComponents(nint context, WireHandle* owner, DataView* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = arena.Array<uint, WireHandle>(BoundObject(*owner).components.ToArray(),
			static (index, _) => BoundComponentHandle(index), false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetComponentType(nint context, WireHandle* component, Utf8View* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = arena.String(BoundComponent(*component).typeId, false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetComponentOwner(nint context, WireHandle* component, WireHandle* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = BoundHandle(BoundComponent(*component).owner));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetComponentEnabled(nint context, WireHandle* component, uint* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = BoundComponent(*component).enabled ? 1u : 0u);

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetComponentEnabled(nint context, WireHandle* component, uint enabled, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => BoundComponent(*component).enabled = BindingMemory.Bool(enabled));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundIsComponentActive(nint context, WireHandle* component, uint* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		FakeComponent value = BoundComponent(*component);
		*result = value.enabled && fakeObjects[value.owner].active ? 1u : 0u;
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundRemoveComponent(nint context, WireHandle* component, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		FakeComponent value = BoundComponent(*component);
		fakeObjects[value.owner].components.Remove(component->F_index);
		fakeComponents.Remove(component->F_index);
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundAddBehaviour(nint context, WireHandle* owner, Utf8View* type, ulong token, WireHandle* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena =>
		{
			if (token == 0) throw new InvalidOperationException("The fixture requires an existing Behaviour token.");
			*result = CreateBoundComponent(*owner, BindingMemory.String(*type, false)!, true);
		});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetComponentId(nint context, WireHandle* component, Utf8View* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = arena.String(BoundComponent(*component).persistentId, false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundResolveComponentId(nint context, Utf8View* id, WireHandle* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		string value = BindingMemory.String(*id, false)!;
		*result = BoundComponentHandle(fakeComponents.Single(pair => pair.Value.persistentId == value).Key);
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetLocalTransform(nint context, WireHandle* handle, WireTransform* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		float[] v = BoundObject(*handle).transform;
		*result = new() { F_Position = new() { F_X = v[0], F_Y = v[1], F_Z = v[2] },
			F_Rotation = new() { F_X = v[3], F_Y = v[4], F_Z = v[5], F_W = v[6] },
			F_Scale = new() { F_X = v[7], F_Y = v[8], F_Z = v[9] } };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetLocalTransform(nint context, WireHandle* handle, WireTransform* value, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		BoundObject(*handle).transform = new[] { value->F_Position.F_X, value->F_Position.F_Y, value->F_Position.F_Z,
			value->F_Rotation.F_X, value->F_Rotation.F_Y, value->F_Rotation.F_Z, value->F_Rotation.F_W,
			value->F_Scale.F_X, value->F_Scale.F_Y, value->F_Scale.F_Z };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetWorldPosition(nint context, WireHandle* handle, WireVector* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		float[] v = BoundObject(*handle).transform;
		*result = new() { F_X = v[0], F_Y = v[1], F_Z = v[2] };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetWorldPosition(nint context, WireHandle* handle, WireVector* value, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		float[] v = BoundObject(*handle).transform;
		v[0] = value->F_X; v[1] = value->F_Y; v[2] = value->F_Z;
	});

	private static FakeComponent BoundCamera(WireHandle handle)
	{
		FakeComponent camera = BoundComponent(handle);
		if (camera.typeId != "ursa.Camera") throw new InvalidOperationException("Camera binding requires a CameraComponent.");
		return camera;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetProjection(nint context, WireHandle* handle, WireProjection* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		float[] v = BoundCamera(*handle).values;
		*result = new() { F_VerticalFovRadians = v[0], F_NearPlane = v[1], F_FarPlane = v[2] };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetProjection(nint context, WireHandle* handle, WireProjection* value, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		if (!float.IsFinite(value->F_VerticalFovRadians) || value->F_VerticalFovRadians <= 0 || value->F_VerticalFovRadians >= MathF.PI ||
			!float.IsFinite(value->F_NearPlane) || value->F_NearPlane <= 0 || !float.IsFinite(value->F_FarPlane) || value->F_FarPlane <= value->F_NearPlane)
			throw new ArgumentException("Invalid Camera projection.");
		BoundCamera(*handle).values = new[] { value->F_VerticalFovRadians, value->F_NearPlane, value->F_FarPlane };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetCameraSettings(nint context, WireHandle* handle, WireCamera* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		var s = BoundCamera(*handle).rendering;
		*result = new() { Present = 1, F_Type = (uint)s.Type, F_Depth = s.Depth, F_ClearFlags = (uint)s.ClearFlags, F_CullingMask = s.CullingMask,
			F_Background = new() { F_R = s.Background.R, F_G = s.Background.G, F_B = s.Background.B, F_A = s.Background.A },
			F_Viewport = new() { F_X = s.Viewport.X, F_Y = s.Viewport.Y, F_Width = s.Viewport.Width, F_Height = s.Viewport.Height },
			F_TargetWidth = s.TargetWidth, F_TargetHeight = s.TargetHeight };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetCameraSettings(nint context, WireHandle* handle, WireCamera* value, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		BoundCamera(*handle).rendering = new() { Type = (Ursa.CameraType)value->F_Type, Depth = value->F_Depth,
			ClearFlags = (Ursa.CameraClearFlags)value->F_ClearFlags, CullingMask = value->F_CullingMask,
			Background = new(value->F_Background.F_R, value->F_Background.F_G, value->F_Background.F_B, value->F_Background.F_A),
			Viewport = new(value->F_Viewport.F_X, value->F_Viewport.F_Y, value->F_Viewport.F_Width, value->F_Viewport.F_Height),
			TargetWidth = value->F_TargetWidth, TargetHeight = value->F_TargetHeight };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetKey(nint context, uint key, uint phase, uint* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, _ => *result = phase switch { 0 => fakeInputDown, 1 => fakeInputPressed, 2 => fakeInputReleased, _ => 0 });

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetTime(nint context, WireTime* result, BindingOutcome* outcome) => BoundCall(context, outcome, _ =>
		*result = new() { F_DeltaTime = 0.016f, F_FixedDeltaTime = 0.02f, F_ElapsedTime = 10.5, F_FixedElapsedTime = 10.4, F_FrameCount = 99 });

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundWriteLog(nint context, uint level, Utf8View* message, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		if (level > 5) throw new ArgumentOutOfRangeException(nameof(level));
		fakeLogLevel = level;
		fakeLog = BindingMemory.String(*message, false)!;
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundReportHostMessage(nint context, uint level, Utf8View* message, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => { });

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundReadFile(nint context, Utf8View* path, DataView* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = arena.Array<byte, byte>(fakeFiles[BindingMemory.String(*path, false)!], static (value, _) => value, false));

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundWriteFile(nint context, Utf8View* path, DataView* bytes, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => fakeFiles[BindingMemory.String(*path, false)!] =
			BindingMemory.Array<byte, byte>(*bytes, static value => value, false)!);

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundSetWindowTitle(nint context, Utf8View* title, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		string text = BindingMemory.String(*title, false)!;
		if (title->Length > 1024 || text.Contains('\0')) throw new ArgumentException("Invalid window title.");
		fakeWindowTitle = text;
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundLoadMaterial(nint context, WireAsset* asset, WireHandle* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		if (BindingMemory.String(asset->F_Guid, false) != fakeMaterialGuid) throw new InvalidOperationException("Material asset kind mismatch.");
		*result = new() { F_sessionId = 1, F_index = 202, F_generation = 1, F_kind = 5 };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundLoadTexture(nint context, WireAsset* asset, WireHandle* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		if (BindingMemory.String(asset->F_Guid, false) != fakeTextureGuid) throw new InvalidOperationException("Texture asset kind mismatch.");
		*result = new() { F_sessionId = 1, F_index = 201, F_generation = 1, F_kind = 3 };
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundResourceValid(nint context, WireHandle* resource, uint* result, BindingOutcome* outcome) =>
		BoundCall(context, outcome, arena => *result = resource->F_sessionId == 1 && resource->F_generation == 1 &&
			(resource->F_index == 201 && resource->F_kind == 3 || resource->F_index == 202 && resource->F_kind == 5) ? 1u : 0u);

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundFindComputeKernel(nint context, WireAsset* asset, Utf8View* name, int* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		if (BindingMemory.String(*name, false) != "Fill") throw new InvalidOperationException("Compute kernel is missing.");
		*result = 0;
	});

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int BoundGetComputeThreadGroupSize(nint context, WireAsset* asset, int kernel, WireGroup* result, BindingOutcome* outcome) => BoundCall(context, outcome, arena =>
	{
		if (kernel != 0) throw new ArgumentOutOfRangeException(nameof(kernel));
		*result = new() { F_X = 8, F_Y = 8, F_Z = 1 };
	});
}
