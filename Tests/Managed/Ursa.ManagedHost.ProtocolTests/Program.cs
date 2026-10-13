using System;
using System.Buffers.Binary;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Ursa.ManagedHost;
using Ursa;

internal static unsafe class Program
{
	private sealed class FakeObject
	{
		internal string name = string.Empty;
		internal string persistentId = string.Empty;
		internal bool active = true;
		internal Bootstrap.NativeHandle parent;
		internal float[] transform = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };
		internal readonly System.Collections.Generic.List<uint> components = new();
	}

	private sealed class FakeComponent
	{
		internal uint owner;
		internal string typeId = string.Empty;
		internal string persistentId = string.Empty;
		internal bool enabled = true;
		internal float[] values = Array.Empty<float>();
	}

	private sealed class PipelineProbeAsset : Ursa.Rendering.RenderPipelineAsset
	{
		internal int renders;
		internal int disposals;
		internal int cameraCount;
		internal System.Collections.Generic.List<CameraComponent>? receivedCameras;
		internal bool failFactory;

		protected override Ursa.Rendering.RenderPipeline CreatePipeline()
		{
			if (failFactory) throw new InvalidOperationException("pipeline factory probe");
			return new PipelineProbe(this);
		}
	}

	private sealed class PipelineProbe : Ursa.Rendering.RenderPipeline
	{
		private readonly PipelineProbeAsset owner;
		internal PipelineProbe(PipelineProbeAsset selected) => owner = selected;
		public override void Render(Ursa.Rendering.ScriptableRenderContext context,
			System.Collections.Generic.List<CameraComponent> cameras)
		{
			++owner.renders;
			owner.receivedCameras = cameras;
			foreach (CameraComponent camera in cameras)
			{
				++owner.cameraCount;
				Ursa.Rendering.RenderPipelineManager.BeginCamera(context, camera);
				try { context.SetupCameraProperties(camera); }
				finally { Ursa.Rendering.RenderPipelineManager.EndCamera(context, camera); }
			}
		}
		protected override void Dispose(bool disposing) => ++owner.disposals;
	}

	private static readonly System.Collections.Generic.Dictionary<uint, FakeObject> fakeObjects = new();
	private static readonly System.Collections.Generic.Dictionary<uint, FakeComponent> fakeComponents = new();
	private static uint nextFakeObject = 1;
	private static uint nextFakeComponent = 1;
	private static uint nextFakeScriptComponent = 1000;
	private static uint fakeInputDown;
	private static uint fakeInputPressed;
	private static uint fakeInputReleased;
	private static readonly System.Collections.Generic.Dictionary<string, byte[]> fakeFiles = new();
	private static string fakeLog = string.Empty;
	private static uint fakeLogLevel;
	private static string fakeWindowTitle = string.Empty;
	private static int renderBatchSubmissions;
	private static readonly System.Collections.Generic.List<byte[]> submittedRenderBatches = new();
	private static ulong renderSessionIdentity = 90;
	private const string fakeTextureGuid = "11111111-1111-4111-8111-111111111111";
	private const string fakeMaterialGuid = "22222222-2222-4222-8222-222222222222";

	private static void Expect(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException(message);
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static void Log(nint context, uint level, byte* message, ulong length)
	{
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int SubmitRenderBatch(nint context, byte* token, ulong tokenSize, byte* batch, ulong batchSize)
	{
		if (token == null || tokenSize != 24 || batch == null || batchSize < 16 || batchSize > 64 * 1024 * 1024)
			return 1;
		var bytes = new ReadOnlySpan<byte>(batch, checked((int)batchSize));
		if (BinaryPrimitives.ReadUInt32LittleEndian(bytes) != 1 ||
		    BinaryPrimitives.ReadUInt32LittleEndian(bytes[4..]) != 16 ||
		    BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]) != batchSize)
			return 1;
		submittedRenderBatches.Add(bytes.ToArray());
		++renderBatchSubmissions;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int CreateGameObject(nint context, byte* name, ulong length, Bootstrap.NativeHandle* output)
	{
		uint index = nextFakeObject++;
		fakeObjects[index] = new FakeObject { name = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(name, (int)length)),
			                                  persistentId = $"00000000-0000-4000-8000-{index:D12}" };
		*output = new Bootstrap.NativeHandle { sessionId = 1, index = index, generation = 1, kind = 1 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetName(nint context, Bootstrap.NativeHandle handle, byte* output, ulong capacity,
	                           ulong* required)
	{
		byte[] name = Encoding.UTF8.GetBytes(fakeObjects[handle.index].name);
		*required = (ulong)name.Length;
		if (capacity < (ulong)name.Length)
		{
			return 4;
		}
		name.AsSpan().CopyTo(new Span<byte>(output, name.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetName(nint context, Bootstrap.NativeHandle handle, byte* value, ulong length)
	{
		fakeObjects[handle.index].name = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(value, (int)length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetActive(nint context, Bootstrap.NativeHandle handle, uint* active)
	{
		*active = fakeObjects[handle.index].active ? 1u : 0u;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetActive(nint context, Bootstrap.NativeHandle handle, uint active)
	{
		fakeObjects[handle.index].active = active != 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetParent(nint context, Bootstrap.NativeHandle handle, Bootstrap.NativeHandle* parent)
	{
		*parent = fakeObjects[handle.index].parent;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetParent(nint context, Bootstrap.NativeHandle handle, Bootstrap.NativeHandle parent)
	{
		fakeObjects[handle.index].parent = parent;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetTransform(nint context, Bootstrap.NativeHandle handle, float* values, ulong count)
	{
		fakeObjects[handle.index].transform.AsSpan().CopyTo(new Span<float>(values, (int)count));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetTransform(nint context, Bootstrap.NativeHandle handle, float* values, ulong count)
	{
		new ReadOnlySpan<float>(values, (int)count).CopyTo(fakeObjects[handle.index].transform);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetCameraProjection(nint context, Bootstrap.NativeHandle handle, float* values, ulong count)
	{
		if (context != 123 || count != 3 || !fakeComponents.TryGetValue(handle.index, out FakeComponent? component) ||
			component.typeId != "ursa.Camera")
			return 1;
		component.values.AsSpan().CopyTo(new Span<float>(values, 3));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetCameraProjection(nint context, Bootstrap.NativeHandle handle, float* values, ulong count)
	{
		if (context != 123 || count != 3 || !fakeComponents.TryGetValue(handle.index, out FakeComponent? component) ||
			component.typeId != "ursa.Camera" || !float.IsFinite(values[0]) || values[0] <= 0 || values[0] >= MathF.PI ||
			!float.IsFinite(values[1]) || !float.IsFinite(values[2]) || values[1] <= 0 || values[2] <= values[1])
			return 1;
		new ReadOnlySpan<float>(values, 3).CopyTo(component.values);
		return 0;
	}









	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetWindowTitle(nint context, byte* title, ulong length)
	{
		if (context != 123 || length > 1024 || (title == null && length != 0))
			return 1;
		string value = length == 0 ? string.Empty : Encoding.UTF8.GetString(new ReadOnlySpan<byte>(title, (int)length));
		if (value.Contains('\0'))
			return 1;
		fakeWindowTitle = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetInputState(nint context, uint key, uint phase, uint* state)
	{
		*state = phase switch {
			0 => fakeInputDown,
			1 => fakeInputPressed,
			2 => fakeInputReleased,
			_ => 0
		};
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetTimeState(nint context, float* delta, float* fixedDelta, double* time, double* fixedTime,
	                                ulong* frame)
	{
		*delta = 0.016f;
		*fixedDelta = 0.02f;
		*time = 10.5;
		*fixedTime = 10.4;
		*frame = 99;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int WriteLog(nint context, uint level, byte* message, ulong length)
	{
		fakeLogLevel = level;
		fakeLog = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(message, (int)length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int ReadFile(nint context, byte* path, ulong pathLength, byte* output, ulong capacity,
	                            ulong* required)
	{
		string key = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(path, (int)pathLength));
		byte[] bytes = fakeFiles[key];
		*required = (ulong)bytes.Length;
		if (capacity < (ulong)bytes.Length)
		{
			return 4;
		}
		bytes.AsSpan().CopyTo(new Span<byte>(output, bytes.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int WriteFile(nint context, byte* path, ulong pathLength, byte* data, ulong length)
	{
		string key = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(path, (int)pathLength));
		fakeFiles[key] = new ReadOnlySpan<byte>(data, (int)length).ToArray();
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int AddComponent(nint context, Bootstrap.NativeHandle owner, byte* type, ulong length,
	                                Bootstrap.NativeHandle* output)
	{
		if (!fakeObjects.TryGetValue(owner.index, out FakeObject? gameObject))
		{
			return 1;
		}
		uint index = nextFakeComponent++;
		fakeComponents[index] = new FakeComponent {
			owner = owner.index, typeId = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(type, (int)length)),
			persistentId = $"00000000-0000-4000-8001-{index:D12}",
			values = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(type, (int)length)) == "ursa.Camera"
			             ? new[] { 0.78539816f, 0.1f, 100.0f }
			             : new[] { 1.0f, 1.0f, 1.0f, 1.0f }
		};
		gameObject.components.Add(index);
		*output = new Bootstrap.NativeHandle { sessionId = 1, index = index, generation = 1, kind = 2 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetComponentCount(nint context, Bootstrap.NativeHandle owner, uint* count)
	{
		if (!fakeObjects.TryGetValue(owner.index, out FakeObject? gameObject))
		{
			return 1;
		}
		*count = (uint)gameObject.components.Count;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetComponentAt(nint context, Bootstrap.NativeHandle owner, uint index,
	                                  Bootstrap.NativeHandle* output)
	{
		if (!fakeObjects.TryGetValue(owner.index, out FakeObject? gameObject) || index >= gameObject.components.Count)
		{
			return 1;
		}
		uint componentIndex = gameObject.components[(int)index];
		*output = new Bootstrap.NativeHandle { sessionId = 1, index = componentIndex, generation = 1, kind = 2 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetComponentType(nint context, Bootstrap.NativeHandle handle, byte* output, ulong capacity,
	                                    ulong* required)
	{
		if (!fakeComponents.TryGetValue(handle.index, out FakeComponent? component))
		{
			return 1;
		}
		byte[] type = Encoding.UTF8.GetBytes(component.typeId);
		*required = (ulong)type.Length;
		if (capacity < (ulong)type.Length)
		{
			return 4;
		}
		type.AsSpan().CopyTo(new Span<byte>(output, type.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetComponentOwner(nint context, Bootstrap.NativeHandle handle, Bootstrap.NativeHandle* output)
	{
		if (!fakeComponents.TryGetValue(handle.index, out FakeComponent? component))
		{
			return 1;
		}
		*output = new Bootstrap.NativeHandle { sessionId = 1, index = component.owner, generation = 1, kind = 1 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetComponentEnabled(nint context, Bootstrap.NativeHandle handle, uint* enabled)
	{
		if (!fakeComponents.TryGetValue(handle.index, out FakeComponent? component))
		{
			return 1;
		}
		*enabled = component.enabled ? 1u : 0u;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int SetComponentEnabled(nint context, Bootstrap.NativeHandle handle, uint enabled)
	{
		if (enabled > 1 || !fakeComponents.TryGetValue(handle.index, out FakeComponent? component))
		{
			return 1;
		}
		component.enabled = enabled != 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int IsComponentActive(nint context, Bootstrap.NativeHandle handle, uint* active)
	{
		if (!fakeComponents.TryGetValue(handle.index, out FakeComponent? component) ||
			!fakeObjects.TryGetValue(component.owner, out FakeObject? owner))
		{
			return 1;
		}
		*active = component.enabled && owner.active ? 1u : 0u;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int RemoveComponent(nint context, Bootstrap.NativeHandle handle)
	{
		if (!fakeComponents.Remove(handle.index, out FakeComponent? component) ||
			!fakeObjects.TryGetValue(component.owner, out FakeObject? owner))
		{
			return 1;
		}
		owner.components.Remove(handle.index);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int LoadTexture(nint context, byte* guid, ulong length, ulong localFileId,
	                               Bootstrap.NativeHandle* output)
	{
		string value = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(guid, (int)length));
		if (context != 123 || value != fakeTextureGuid || localFileId != 0)
		{
			return 1;
		}
		*output = new Bootstrap.NativeHandle { sessionId = 1, index = 201, generation = 1, kind = 3 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int LoadMaterial(nint context, byte* guid, ulong length, ulong localFileId,
	                                Bootstrap.NativeHandle* output)
	{
		string value = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(guid, (int)length));
		if (context != 123 || value != fakeMaterialGuid || localFileId != 0)
		{
			return 1;
		}
		*output = new Bootstrap.NativeHandle { sessionId = 1, index = 202, generation = 1, kind = 3 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int AddBehaviour(nint context, Bootstrap.NativeHandle owner, byte* typeName, ulong typeNameLength,
	                                ulong instanceToken, Bootstrap.NativeHandle* output)
	{
		if (!fakeObjects.TryGetValue(owner.index, out FakeObject? gameObject) || instanceToken == 0 || typeNameLength == 0)
		{
			return 1;
		}
		uint index = nextFakeScriptComponent++;
		fakeComponents[index] =
		    new FakeComponent { owner = owner.index,
			                    typeId = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(typeName, (int)typeNameLength)),
			                    persistentId = $"00000000-0000-4000-8001-{index:D12}" };
		gameObject.components.Add(index);
		*output = new Bootstrap.NativeHandle { sessionId = owner.sessionId, index = index, generation = 1, kind = 2 };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetGameObjectPersistentId(nint context, Bootstrap.NativeHandle handle, byte* output,
	                                             ulong capacity, ulong* required)
	{
		return CopyPersistentId(fakeObjects[handle.index].persistentId, output, capacity, required);
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int ResolveGameObjectPersistentId(nint context, byte* id, ulong length,
	                                                 Bootstrap.NativeHandle* output)
	{
		string value = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(id, (int)length));
		foreach (var entry in fakeObjects)
		{
			if (entry.Value.persistentId == value)
			{
				*output = new Bootstrap.NativeHandle { sessionId = 1, index = entry.Key, generation = 1, kind = 1 };
				return 0;
			}
		}
		return 3;
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int GetComponentPersistentId(nint context, Bootstrap.NativeHandle handle, byte* output,
	                                            ulong capacity, ulong* required)
	{
		return CopyPersistentId(fakeComponents[handle.index].persistentId, output, capacity, required);
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

	private static int ResolveComponentPersistentId(nint context, byte* id, ulong length,
	                                                Bootstrap.NativeHandle* output)
	{
		string value = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(id, (int)length));
		foreach (var entry in fakeComponents)
		{
			if (entry.Value.persistentId == value)
			{
				*output = new Bootstrap.NativeHandle { sessionId = 1, index = entry.Key, generation = 1, kind = 2 };
				return 0;
			}
		}
		return 3;
	}

	private static int CopyPersistentId(string id, byte* output, ulong capacity, ulong* required)
	{
		byte[] bytes = Encoding.UTF8.GetBytes(id);
		*required = (ulong)bytes.Length;
		if (capacity < (ulong)bytes.Length)
		{
			return 4;
		}
		bytes.AsSpan().CopyTo(new Span<byte>(output, bytes.Length));
		return 0;
	}

	private static string resourceResponse = string.Empty;

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int ResourceRequest(nint context, byte* input, ulong length, byte* output, ulong capacity, ulong* required)
	{
		if (context != 123 || required == null) return 1;
		if (input != null)
		{
			using var document = System.Text.Json.JsonDocument.Parse(Encoding.UTF8.GetString(new ReadOnlySpan<byte>(input, checked((int)length))));
			var request = document.RootElement;
			object response = request.GetProperty("operation").GetString() switch
			{
				"render.context.begin" => new { value = (object)new { context = new { sessionId = ++renderSessionIdentity, frameId = request.GetProperty("frameId").GetUInt64(), index = 1U, generation = 1U } } },
				"render.camera.setup" => new { value = (object?)null },
				"render.cull" => new { value = (object)new { context = request.GetProperty("context"), index = 0U, generation = 1U } },
				"render.rendererList.create" => new { value = (object)new { context = request.GetProperty("context"), index = 0U, generation = 1U } },
				"render.context.end" => new { value = (object?)null },
				"material.load" when request.GetProperty("guid").GetString() == fakeMaterialGuid => new { value = (object)new { sessionId = 1UL, index = 202U, generation = 1U, kind = 5U, reserved = 0U } },
				"resource.valid" => new { value = (object)(request.GetProperty("handle").GetProperty("index").GetUInt32() is 201U or 202U) },
				_ => (object)new { error = "Unsupported protocol fixture resource request" }
			};
			resourceResponse = System.Text.Json.JsonSerializer.Serialize(response);
		}
		var bytes = Encoding.UTF8.GetBytes(resourceResponse);
		*required = (ulong)bytes.Length;
		if (capacity < *required) return 4;
		bytes.CopyTo(new Span<byte>(output, bytes.Length));
		resourceResponse = string.Empty;
		return 0;
	}

	private static int Validate(uint version, uint size, bool hasCallback, int argumentSize)
	{
		Bootstrap.NativeApiV6 api = new() { version = version, size = size, context = 0, sceneContext = 123,
			                                logUtf8 = hasCallback ? &Log : null,
			                                submitRenderBatch = hasCallback ? &SubmitRenderBatch : null };
		return Bootstrap.Validate((nint)(&api), argumentSize);
	}

	private static void ManagedRenderContextApiTests()
	{
		GameObject owner = GameObject.Create("Render Camera");
		CameraComponent camera = owner.AddComponent<CameraComponent>();
		var drawing = new Ursa.Rendering.DrawingSettings { ShaderTags = new[] { "SRPDefaultUnlit" } };
		var filtering = new Ursa.Rendering.FilteringSettings();
		using var context = new Ursa.Rendering.ScriptableRenderContext(1.0f);
		context.SetupCameraProperties(camera);
		var culling = context.Cull(camera);
		var list = context.CreateRendererList(culling, drawing, filtering);
		Expect(list.Generation != 0, "Managed ScriptableRenderContext did not create a RendererList");
		using var commands = new Ursa.Rendering.CommandBuffer("two-camera-lists");
		commands.DrawRendererList(list, context);
		int previousBatchCount = submittedRenderBatches.Count;
		commands.Execute(context);
		commands.Clear();
		GameObject secondOwner = GameObject.Create("Second Render Camera");
		CameraComponent secondCamera = secondOwner.AddComponent<CameraComponent>();
		context.SetupCameraProperties(secondCamera);
		var secondList = context.CreateRendererList(context.Cull(secondCamera), drawing, filtering);
		commands.DrawRendererList(secondList, context);
		commands.Execute(context);
		Expect(submittedRenderBatches.Count == previousBatchCount + 2 &&
		       BinaryPrimitives.ReadUInt32LittleEndian(submittedRenderBatches[^2].AsSpan(8)) == 1 &&
		       BinaryPrimitives.ReadUInt32LittleEndian(submittedRenderBatches[^1].AsSpan(8)) == 1,
		       "Managed context did not submit RendererLists for two cameras in order");
		using var otherContext = new Ursa.Rendering.ScriptableRenderContext(1.0f);
		bool staleRejected = false;
		try
		{
			otherContext.CreateRendererList(culling, drawing, filtering);
		}
		catch (InvalidOperationException)
		{
			staleRejected = true;
		}
		Expect(staleRejected, "Managed ScriptableRenderContext accepted CullingResults from another frame/session");
	}

	private static void ManagedCommandBufferApiTests()
	{
		using var context = new Ursa.Rendering.ScriptableRenderContext(1.0f);
		using var commands = new Ursa.Rendering.CommandBuffer("command-buffer-test");
		var mesh = Ursa.Mesh.FromHandle(new Ursa.NativeHandle { sessionId = 1, index = 201, generation = 1, kind = 4 });
		var meshReference = new WeakReference(mesh!);
		var material = Ursa.Material.Load(new AssetReference(fakeMaterialGuid, 0));
		commands.SetRenderTarget(12);
		commands.ClearTarget(true, true, false, new Ursa.Vector4(0.1f, 0.2f, 0.3f, 1));
		commands.SetViewport(0, 0, 640, 480);
		commands.SetScissor(0, 0, 640, 480);
		commands.DrawMesh(mesh!, material!, new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
		commands.Execute(context);
		Expect(submittedRenderBatches.Count > 0, "CommandBuffer did not submit a batch");
		byte[] executedBatch = submittedRenderBatches[^1];
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(8)) == 5,
		       "CommandBuffer did not preserve command count");
		uint[] expectedOpcodes = { 1, 2, 3, 12, 5 };
		int offset = 16;
		foreach (uint opcode in expectedOpcodes)
		{
			Expect(BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(offset)) == opcode,
			       "CommandBuffer changed recorded command order");
			offset += checked((int)BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(offset + 4)));
		}
		mesh = null;
		material = null;
		commands.Clear();
		GC.Collect();
		GC.WaitForPendingFinalizers();
		GC.Collect();
		Expect(meshReference.IsAlive, "Clearing an executed CommandBuffer released a resource retained by its context");
		commands.SetRenderTarget(13);
		commands.Execute(context);
		byte[] nextBatch = submittedRenderBatches[^1];
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(nextBatch.AsSpan(8)) == 1 &&
		       BinaryPrimitives.ReadUInt32LittleEndian(nextBatch.AsSpan(16)) == 1 &&
		       BinaryPrimitives.ReadUInt32LittleEndian(nextBatch.AsSpan(24)) == 13,
		       "Cleared CommandBuffer did not execute only its newly recorded commands");
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(8)) == 5,
		       "Clearing a CommandBuffer mutated a previously submitted batch");
		commands.Clear();
		var sourceTexture = Ursa.Rendering.RenderTexture.FromHandle(
			new Ursa.NativeHandle { sessionId = 1, index = 301, generation = 1, kind = 6 })!;
		var destinationTexture = Ursa.Rendering.RenderTexture.FromHandle(
			new Ursa.NativeHandle { sessionId = 1, index = 302, generation = 1, kind = 6 })!;
		var sourceBuffer = Ursa.Rendering.GraphicsBuffer.FromHandle(
			new Ursa.NativeHandle { sessionId = 1, index = 303, generation = 1, kind = 7 })!;
		var destinationBuffer = Ursa.Rendering.GraphicsBuffer.FromHandle(
			new Ursa.NativeHandle { sessionId = 1, index = 304, generation = 1, kind = 7 })!;
		var blitMaterial = Ursa.Material.Load(new AssetReference(fakeMaterialGuid, 0));
		commands.SetGlobalFloat("_Intensity", 0.75f);
		commands.SetGlobalVector("_Tint", new Ursa.Vector4(1, 0.5f, 0.25f, 1));
		commands.SetGlobalTexture("_Source", sourceTexture);
		var descriptor = new Ursa.Rendering.RenderTextureDescriptor(64, 64);
		commands.GetTemporaryTarget(77, descriptor);
		bool duplicateTemporaryRejected = false;
		try { commands.GetTemporaryTarget(77, descriptor); }
		catch (ArgumentException) { duplicateTemporaryRejected = true; }
		Expect(duplicateTemporaryRejected, "CommandBuffer accepted a duplicate temporary target ID");
		commands.SetRenderTarget(77);
		commands.ReleaseTemporaryTarget(77);
		commands.SetRenderTarget(destinationTexture);
		commands.CopyTexture(sourceTexture, destinationTexture);
		bool feedbackRejected = false;
		try { commands.Blit(sourceTexture, sourceTexture, blitMaterial!); }
		catch (InvalidOperationException) { feedbackRejected = true; }
		Expect(feedbackRejected, "CommandBuffer accepted a same-texture Blit feedback loop");
		commands.Blit(sourceTexture, destinationTexture, blitMaterial!);
		commands.CopyBuffer(sourceBuffer, destinationBuffer, 128);
		commands.Execute(context);
		byte[] resourceBatch = submittedRenderBatches[^1];
		uint[] resourceOpcodes = { 9, 9, 9, 10, 1, 11, 1, 13, 6, 7 };
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(resourceBatch.AsSpan(8)) == resourceOpcodes.Length,
		       "Resource and compositing commands were not recorded");
		offset = 16;
		foreach (uint opcode in resourceOpcodes)
		{
			Expect(BinaryPrimitives.ReadUInt32LittleEndian(resourceBatch.AsSpan(offset)) == opcode,
			       "Resource and compositing command order changed");
			offset += checked((int)BinaryPrimitives.ReadUInt32LittleEndian(resourceBatch.AsSpan(offset + 4)));
		}
		commands.Clear();
		commands.GetTemporaryTarget(88, descriptor);
		bool unreleasedTemporaryRejected = false;
		try { commands.Execute(context); }
		catch (InvalidOperationException) { unreleasedTemporaryRejected = true; }
		Expect(unreleasedTemporaryRejected, "CommandBuffer executed with an unreleased temporary target");
		commands.Clear();
		commands.Dispose();
		bool disposedRejected = false;
		try { commands.SetRenderTarget(1); }
		catch (ObjectDisposedException) { disposedRejected = true; }
		Expect(disposedRejected, "Disposed CommandBuffer accepted new commands");
		disposedRejected = false;
		try { commands.Name = "disposed"; }
		catch (ObjectDisposedException) { disposedRejected = true; }
		Expect(disposedRejected, "Disposed CommandBuffer accepted a name change");
	}

	private static void ManagedRenderPipelineLifecycleTests()
	{
		Ursa.Rendering.RenderPipelineManager.Reset();
		var defaultTypeIdentity = (UrsaScriptTypeAttribute?)Attribute.GetCustomAttribute(
			typeof(Ursa.Rendering.ForwardRenderPipelineAsset), typeof(UrsaScriptTypeAttribute));
		Expect(defaultTypeIdentity?.Id == "ursa.rendering.forward" && defaultTypeIdentity.Version == 1,
		       "Framework Forward pipeline type did not expose its stable identity");
		var first = Ursa.ScriptableObject.CreateInstance<PipelineProbeAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(first);
		var second = Ursa.ScriptableObject.CreateInstance<PipelineProbeAsset>();
		second.failFactory = true;
		bool factoryRejected = false;
		try { Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(second); }
		catch (InvalidOperationException) { factoryRejected = true; }
		Expect(factoryRejected && ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActiveAsset, first) && first.disposals == 0,
		       "Failed pipeline candidate replaced or disposed the active pipeline");
		int began = 0, ended = 0, camerasBegan = 0, camerasEnded = 0;
		Ursa.Rendering.RenderPipelineManager.BeginContextRendering += _ => ++began;
		Ursa.Rendering.RenderPipelineManager.EndContextRendering += _ => ++ended;
		Ursa.Rendering.RenderPipelineManager.BeginCameraRendering += (_, _) => ++camerasBegan;
		Ursa.Rendering.RenderPipelineManager.EndCameraRendering += (_, _) => ++camerasEnded;
		var cameraList = new System.Collections.Generic.List<CameraComponent>
		{
			GameObject.Create("Pipeline Camera").AddComponent<CameraComponent>()
		};
		Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1);
		Expect(first.renders == 1 && began == 1 && ended == 1 && camerasBegan == 1 && camerasEnded == 1 &&
		       first.cameraCount == 1 && ReferenceEquals(first.receivedCameras, cameraList),
		       "Pipeline manager did not preserve cameras or pair context/camera events around Render");
		Ursa.Rendering.RenderPipeline previousPipeline = Ursa.Rendering.RenderPipelineManager.ActivePipeline!;
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(null);
		Expect(first.disposals == 1 && previousPipeline.IsDisposed &&
		       Ursa.Rendering.RenderPipelineManager.ActivePipeline == null,
		       "Replacing the active pipeline did not dispose its previous instance");
		Ursa.Rendering.RenderPipelineManager.Reset();
		first.Dispose();
		second.Dispose();
		var forward = Ursa.ScriptableObject.CreateInstance<Ursa.Rendering.ForwardRenderPipelineAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(forward);
		Expect(Ursa.Rendering.RenderPipelineManager.ActivePipeline is Ursa.Rendering.ForwardRenderPipeline,
		       "Framework Forward pipeline asset factory returned the wrong pipeline");
		Ursa.Rendering.RenderPipelineManager.Reset();
		forward.Dispose();
	}

	private static int Main()
	{
		ScriptableObjectTests.Run();
		RenderCommandProtocolTests.Run();
		Bootstrap.NativeApiV6 fieldApi = new() { version = 6, size = (uint)sizeof(Bootstrap.NativeApiV6),
		                                         logUtf8 = &Log, sceneContext = 123,
		                                         submitRenderBatch = &SubmitRenderBatch,
		                                         loadTexture = &LoadTexture, loadMaterial = &LoadMaterial,
		                                         resourceRequest = &ResourceRequest };
		EngineApi.Initialize((nint)(&fieldApi));
		ScriptableAssetFieldTests.Run();
		ShaderFieldTests.Run();
		ScriptableAssetGraphTests.Run();
		ScriptableAssetRecoveryTests.Run();
		int tableSize = sizeof(Bootstrap.NativeApiV6);
		Expect(tableSize == 336, "Native API struct size differs from the x64 C ABI contract");
		Expect(Marshal.OffsetOf<Bootstrap.NativeApiV6>(nameof(Bootstrap.NativeApiV6.submitRenderBatch)).ToInt32() == 328,
		       "Native API render-batch callback offset differs from the x64 C ABI contract");
		Expect(Bootstrap.Validate(0, tableSize) == 1, "Null native API pointer was accepted");
		Expect(Validate(1, (uint)tableSize, true, tableSize - 1) == 1, "Truncated native API arguments were accepted");
		Expect(Validate(5, (uint)tableSize, true, tableSize) == 2, "Legacy native API version was accepted");
		Expect(Validate(6, (uint)(tableSize - 1), true, tableSize) == 2, "Truncated native API table was accepted");
		Expect(Validate(6, (uint)tableSize, false, tableSize) == 3, "Missing required callback was accepted");
		Expect(Validate(6, (uint)tableSize, true, tableSize) == 0, "Valid native API table was rejected");
		var serializedFields = ScriptFieldSchema.Describe(typeof(SchemaProbeBehaviour));
		Expect(
		    serializedFields.Count == 3 &&
		        serializedFields.Any(field => field.Name == "publicValue" && field.IsSupported) &&
		        serializedFields.Any(field => field.Name == "hiddenValue" && field.IsSupported) &&
		        serializedFields.Any(field => field.Name == "unsupportedValue" && !field.IsSupported &&
		                                      field.Diagnostic.Contains("unsupportedValue", StringComparison.Ordinal)),
		    "Managed script field schema did not include public/marked fields or diagnose unsupported types");
		Expect(
		    serializedFields.All(field => field.Name != "staticValue" && field.Name != "readOnlyValue" &&
		                                  field.Name != "transientValue" && field.Name != "propertyValue" &&
		                                  field.Name != "unmarkedPrivateValue"),
		    "Managed script field schema included a transient, static, read-only, property, or unmarked private field");
		var sourcePayload =
		    new PayloadProbeBehaviour { publicValue = 42,
			                            values = new[] { 2, 5, 8 },
			                            nullValues = null,
			                            mode = PayloadMode.Second,
			                            enabled = true,
			                            signedByte = -7,
			                            unsignedByte = 250,
			                            signedShort = -30000,
			                            unsignedShort = 60000,
			                            unsignedValue = 4000000000,
			                            unsignedLong = 18000000000,
			                            count = 9000000000,
			                            ratio = 0.125,
			                            nullableText = null,
			                            position = new Vector3(1.0f, 2.0f, 3.0f),
			                            rotation = new Quaternion(0.1f, 0.2f, 0.3f, 0.4f),
			                            color = new Color(0.4f, 0.5f, 0.6f, 0.7f),
			                            asset = new AssetReference(fakeTextureGuid),
			                            labels = new System.Collections.Generic.List<string> { "one", "二", "three" } };
		sourcePayload.SetHiddenValue("持久字段");
		string serializedPayload = ScriptFieldSchema.Serialize(sourcePayload);
		using (var enumPayload = System.Text.Json.JsonDocument.Parse(serializedPayload))
		{
			Expect(enumPayload.RootElement.GetProperty("Fields").GetProperty("mode").GetByte() == 1 &&
			           enumPayload.RootElement.GetProperty("FieldTypes")
			                   .GetProperty("mode")
			                   .GetProperty("EnumUnderlyingType")
			                   .GetString() == "System.Byte",
			       "Enum payload did not preserve numeric value and underlying type.");
		}
		bool enumOverflowRejected = false;
		try
		{
			ScriptFieldSchema.Deserialize(
			    new PayloadProbeBehaviour(),
			    "{\"FormatVersion\":1,\"ScriptType\":\"test.payload\",\"Version\":2,\"Fields\":{\"mode\":256}}");
		}
		catch (System.Text.Json.JsonException)
		{
			enumOverflowRejected = true;
		}
		Expect(enumOverflowRejected, "Enum deserialization accepted an overflowing underlying value.");
		var restoredPayload = new PayloadProbeBehaviour();
		ScriptFieldSchema.Deserialize(restoredPayload, serializedPayload);
		Expect(restoredPayload.publicValue == 42 && restoredPayload.GetHiddenValue() == "持久字段" &&
		           restoredPayload.values.AsSpan().SequenceEqual(new[] { 2, 5, 8 }) &&
		           restoredPayload.nullValues == null && restoredPayload.mode == PayloadMode.Second &&
		           restoredPayload.enabled && restoredPayload.signedByte == -7 && restoredPayload.unsignedByte == 250 &&
		           restoredPayload.signedShort == -30000 && restoredPayload.unsignedShort == 60000 &&
		           restoredPayload.unsignedValue == 4000000000 && restoredPayload.unsignedLong == 18000000000 &&
		           restoredPayload.count == 9000000000 && restoredPayload.ratio == 0.125 &&
		           restoredPayload.nullableText == null && restoredPayload.position.X == 1.0f &&
		           restoredPayload.rotation.W == 0.4f && restoredPayload.color.A == 0.7f &&
		           restoredPayload.asset.Guid == fakeTextureGuid && restoredPayload.labels.Count == 3 &&
		           restoredPayload.labels[1] == "二" &&
		           serializedPayload.Contains("test.payload", StringComparison.Ordinal),
		       $"Managed script payload round-trip failed: int={restoredPayload.publicValue}, " +
		           $"hidden={restoredPayload.GetHiddenValue()}, values={string.Join(',', restoredPayload.values)}, " +
		           $"mode={restoredPayload.mode}, enabled={restoredPayload.enabled}, count={restoredPayload.count}, " +
		           $"ratio={restoredPayload.ratio}, nullable={restoredPayload.nullableText ?? "<null>"}, " +
		           $"position={restoredPayload.position.X}, rotation={restoredPayload.rotation.W}, " +
		           $"color={restoredPayload.color.A}, asset={restoredPayload.asset.Guid}, " +
		           $"labels={string.Join(',', restoredPayload.labels)}");
		var migrationPayload = new MigrationProbeBehaviour();
		ScriptFieldSchema.Deserialize(migrationPayload,
		                              "{\"FormatVersion\":1,\"ScriptType\":\"test.migration\",\"Version\":2," +
		                                  "\"Fields\":{\"oldName\":31,\"removedField\":\"keep me\"}}");
		string migratedPayload = ScriptFieldSchema.Serialize(migrationPayload);
		using (JsonDocument migrationDocument = JsonDocument.Parse(migratedPayload))
		{
			Expect(migrationPayload.newName == 31 &&
			           migrationDocument.RootElement.GetProperty("FormerFieldNames")
			                   .GetProperty("newName") [0]
			                   .GetString() == "oldName" &&
			           !migrationDocument.RootElement.GetProperty("Fields").TryGetProperty("removedField", out _),
			       "Managed FormerlySerializedAs did not migrate an old field name into the current descriptor");
		}
		bool migrationVersionRejected = false;
		try
		{
			ScriptFieldSchema.Deserialize(
			    new MigrationProbeBehaviour(),
			    "{\"FormatVersion\":1,\"ScriptType\":\"test.migration\",\"Version\":1,\"Fields\":{}}");
		}
		catch (InvalidOperationException)
		{
			migrationVersionRejected = true;
		}
		Expect(migrationVersionRejected, "Managed script fields accepted an incompatible type version");

		Bootstrap.NativeApiV6 api = new() { version = 6,
			                                size = (uint)sizeof(Bootstrap.NativeApiV6),
			                                sceneContext = 123,
			                                logUtf8 = &Log,
			                                submitRenderBatch = &SubmitRenderBatch,
			                                createGameObject = &CreateGameObject,
			                                getGameObjectName = &GetName,
			                                setGameObjectName = &SetName,
			                                getGameObjectActive = &GetActive,
			                                setGameObjectActive = &SetActive,
			                                getGameObjectParent = &GetParent,
			                                setGameObjectParent = &SetParent,
			                                getLocalTransform = &GetTransform,
			                                setLocalTransform = &SetTransform,
			                                getInputState = &GetInputState,
			                                getTimeState = &GetTimeState,
			                                writeLog = &WriteLog,
			                                readFile = &ReadFile,
			                                writeFile = &WriteFile,
			                                addComponent = &AddComponent,
			                                getComponentCount = &GetComponentCount,
			                                getComponentAt = &GetComponentAt,
			                                getComponentType = &GetComponentType,
			                                getComponentOwner = &GetComponentOwner,
			                                getComponentEnabled = &GetComponentEnabled,
			                                setComponentEnabled = &SetComponentEnabled,
			                                isComponentActive = &IsComponentActive,
			                                removeComponent = &RemoveComponent,
			                                loadTexture = &LoadTexture,
			                                loadMaterial = &LoadMaterial,
			                                addBehaviour = &AddBehaviour,
			                                getGameObjectPersistentId = &GetGameObjectPersistentId,
			                                resolveGameObjectPersistentId = &ResolveGameObjectPersistentId,
			                                getComponentPersistentId = &GetComponentPersistentId,
			                                resolveComponentPersistentId = &ResolveComponentPersistentId,
			                                getCameraProjection = &GetCameraProjection,
			                                setCameraProjection = &SetCameraProjection,
			                                setWindowTitle = &SetWindowTitle,
			                                resourceRequest = &ResourceRequest };
		EngineApi.Initialize((nint)(&api));
		ManagedRenderContextApiTests();
		ManagedCommandBufferApiTests();
		ManagedRenderPipelineLifecycleTests();
		byte[] renderBatch = new byte[16];
		System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(renderBatch, 1);
		System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(renderBatch.AsSpan(4), 16);
		System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(renderBatch.AsSpan(8), 0);
		System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(renderBatch.AsSpan(12), 16);
		Ursa.Rendering.RenderContextToken renderToken = new() { sessionId = 7, frameId = 9, index = 2, generation = 3 };
		int submissionsBeforeProtocol = renderBatchSubmissions;
		Ursa.Rendering.RenderCommandProtocol.Submit(renderToken, renderBatch);
		Expect(renderBatchSubmissions == submissionsBeforeProtocol + 1,
		       "Managed RenderCommandProtocol did not submit its fixed-layout batch");
		GameObject parent = GameObject.Create("父对象");
		GameObject child = GameObject.Create("子对象");
		child.Parent = parent;
		Expect(child.Parent?.Name == "父对象", "Managed GameObject parent/name wrapper failed");
		child.Name = "改名对象";
		Expect(child.Name == "改名对象", "Managed GameObject UTF-8 name round-trip failed");
		child.ActiveSelf = false;
		Expect(!child.ActiveInHierarchy, "Managed active hierarchy wrapper ignored inactive ancestors");
		var identityProbe = child.AddComponent<ReferenceProbeBehaviour>();
		Expect(ReferenceEquals(child.GetComponent<ReferenceProbeBehaviour>(), identityProbe) &&
		           child.GetComponents<Behaviour>().Any(component => ReferenceEquals(component, identityProbe)),
		       "GetComponent created a second Behaviour instance or failed the base-type query");
		child.Transform.LocalPosition = new Vector3(1.0f, 2.0f, 3.0f);
		Expect(child.Transform.LocalPosition.X == 1.0f && child.Transform.LocalPosition.Y == 2.0f &&
		           child.Transform.LocalPosition.Z == 3.0f,
		       "Managed Transform wrapper failed to round-trip local position");
		CameraComponent camera = parent.AddComponent<CameraComponent>();
		Expect(ReferenceEquals(camera.GameObject, parent) &&
		           parent.GetComponent<CameraComponent>()?.TypeId == "ursa.Camera" &&
		           parent.GetComponents<CameraComponent>().Length == 1,
		       "Managed Component creation/query/owner wrappers failed");
		camera.VerticalFovRadians = 1.1f;
		camera.NearPlane = 0.2f;
		camera.FarPlane = 80.0f;
		Expect(Math.Abs(camera.VerticalFovRadians - 1.1f) < 0.0001f && camera.NearPlane == 0.2f &&
		           camera.FarPlane == 80.0f,
		       "Managed Camera properties did not round-trip through the native API");
		bool invalidProjectionRejected = false;
		try
		{
			camera.FarPlane = camera.NearPlane;
		}
		catch (InvalidOperationException)
		{
			invalidProjectionRejected = true;
		}
		Expect(invalidProjectionRejected && camera.FarPlane == 80.0f,
		       "Managed Camera accepted an invalid clipping range or changed the old value");
		camera.Enabled = false;
		Expect(!camera.IsActiveAndEnabled, "Managed Component enabled wrapper failed");
		Application.SetWindowTitle("Ursa Snake | Score: 二");
		Expect(fakeWindowTitle == "Ursa Snake | Score: 二", "Managed Application title did not preserve UTF-8");
		sourcePayload.targetObject = child;
		sourcePayload.targetCamera = camera;
		sourcePayload.objectTargets = new GameObject?[] { parent, null, child };
		sourcePayload.nullObjectTargets = null;
		sourcePayload.objectList = new System.Collections.Generic.List < GameObject ? > { child, parent };
		sourcePayload.cameraTargets = new CameraComponent?[] { camera };
		sourcePayload.cameraList = new System.Collections.Generic.List < CameraComponent ? > { camera, null };
		string referencePayload = ScriptFieldSchema.Serialize(sourcePayload);
		var restoredReferences = new PayloadProbeBehaviour();
		ScriptFieldSchema.Deserialize(restoredReferences, referencePayload);
		ScriptFieldSchema.ResolvePendingReferences(restoredReferences);
		Expect(
		    restoredReferences.targetObject?.PersistentId == child.PersistentId &&
		        restoredReferences.targetCamera?.PersistentId == camera.PersistentId &&
		        restoredReferences.objectTargets[1] == null && restoredReferences.nullObjectTargets == null &&
		        restoredReferences.objectList.Count == 2 &&
		        restoredReferences.objectList[0]?.PersistentId == child.PersistentId &&
		        restoredReferences.cameraTargets[0]?.PersistentId == camera.PersistentId &&
		        restoredReferences.cameraList.Count == 2 && restoredReferences.cameraList[1] == null,
		    $"Managed references failed: target={restoredReferences.targetObject?.PersistentId ?? "<null>"}/" +
		        $"{child.PersistentId}, camera={restoredReferences.targetCamera?.PersistentId ?? "<null>"}/" +
		        $"{camera.PersistentId}, objectAt1={restoredReferences.objectTargets[1]?.Name ?? "<null>"}, " +
		        $"objectList0={restoredReferences.objectList[0]?.PersistentId ?? "<null>"}/" +
		        $"{child.PersistentId}, cameraArray0={restoredReferences.cameraTargets[0]?.PersistentId ?? "<null>"}, " +
		        $"cameraList1={restoredReferences.cameraList[1]?.PersistentId ?? "<null>"}, " +
		        $"checks={restoredReferences.targetObject?.PersistentId == child.PersistentId}/" +
		        $"{restoredReferences.targetCamera?.PersistentId == camera.PersistentId}/" +
		        $"{restoredReferences.objectTargets[1] == null}/{restoredReferences.nullObjectTargets == null}/" +
		        $"{restoredReferences.objectList.Count == 2}/" +
		        $"{restoredReferences.objectList[0]?.PersistentId == child.PersistentId}/" +
		        $"{restoredReferences.cameraTargets[0]?.PersistentId == camera.PersistentId}/" +
		        $"{restoredReferences.cameraList.Count == 2}/{restoredReferences.cameraList[1] == null}");
		GameObject referenceOwner = GameObject.Create("ReferenceOwner");
		GameObject referencePeerOwner = GameObject.Create("ReferencePeer");
		GameObject referenceTarget = GameObject.Create("ReferenceTarget");
		ReferenceProbeBehaviour referenceBehaviour = referenceOwner.AddComponent<ReferenceProbeBehaviour>();
		ReferenceProbeBehaviour referencePeer = referencePeerOwner.AddComponent<ReferenceProbeBehaviour>();
		referenceBehaviour.targetObject = referenceTarget;
		referenceBehaviour.targetCamera = camera;
		referenceBehaviour.targetBehaviour = referencePeer;
		referencePeer.targetBehaviour = referenceBehaviour;
		string lifecyclePayload = ScriptFieldSchema.Serialize(referenceBehaviour);
		string peerLifecyclePayload = ScriptFieldSchema.Serialize(referencePeer);
		referenceBehaviour.targetObject = null;
		referenceBehaviour.targetCamera = null;
		referenceBehaviour.targetBehaviour = null;
		referencePeer.targetBehaviour = null;
		byte[] lifecyclePayloadBytes = Encoding.UTF8.GetBytes(lifecyclePayload);
		byte[] peerLifecyclePayloadBytes = Encoding.UTF8.GetBytes(peerLifecyclePayload);
		fixed(byte* payload = lifecyclePayloadBytes) fixed(byte* peerPayload = peerLifecyclePayloadBytes)
		{
			ulong token = BehaviourRegistry.TokenFor(referenceBehaviour);
			ulong peerToken = BehaviourRegistry.TokenFor(referencePeer);
			delegate* unmanaged[Stdcall]<ulong, byte*, ulong, int> deserialize = &Bootstrap.DeserializeBehaviour;
			delegate* unmanaged[Stdcall]<ulong, uint, int> invokeReference = &Bootstrap.InvokeBehaviour;
			Expect(deserialize(token, payload, (ulong)lifecyclePayloadBytes.Length) == 0 &&
			           deserialize(peerToken, peerPayload, (ulong)peerLifecyclePayloadBytes.Length) == 0 &&
			           invokeReference(token, 0) == 0 && invokeReference(peerToken, 0) == 0,
			       "Managed script references were not restored before Awake");
		}
		Expect(referenceOwner.Name == "ReferenceTarget:父对象:ReferencePeer" &&
		           referencePeerOwner.Name == "::ReferenceTarget:父对象:ReferencePeer",
		       "Managed Awake did not observe bidirectional script, GameObject, and native Component references");
		GameObject missingReferenceOwner = GameObject.Create("MissingReferenceOwner");
		ReferenceProbeBehaviour missingReferenceBehaviour =
		    missingReferenceOwner.AddComponent<ReferenceProbeBehaviour>();
		byte[] missingReferencePayload = Encoding.UTF8.GetBytes(
		    "{\"FormatVersion\":1,\"ScriptType\":\"test.reference\",\"Version\":1,\"Fields\":{\"targetObject\":{\"$" +
		    "ursaReference\":{\"Kind\":\"GameObject\",\"Id\":\"99999999-9999-4999-8999-999999999999\"}}}}");
		fixed(byte* payload = missingReferencePayload)
		{
			ulong token = BehaviourRegistry.TokenFor(missingReferenceBehaviour);
			delegate* unmanaged[Stdcall]<ulong, byte*, ulong, int> deserialize = &Bootstrap.DeserializeBehaviour;
			delegate* unmanaged[Stdcall]<ulong, uint, int> invokeReference = &Bootstrap.InvokeBehaviour;
			Expect(deserialize(token, payload, (ulong)missingReferencePayload.Length) == 0 &&
			           invokeReference(token, 0) != 0,
			       "Managed Awake accepted a non-null missing GameObject reference");
		}
		Expect(missingReferenceOwner.Name == "MissingReferenceOwner",
		       "Missing managed reference allowed Awake to run with an incomplete object graph");
		camera.Remove();
		Expect(parent.GetComponent<CameraComponent>() == null, "Managed Component removal was not visible to queries");
		bool removedHandleRejected = false;
		try
		{
			_ = camera.Enabled;
		}
		catch (InvalidOperationException)
		{
			removedHandleRejected = true;
		}
		Expect(removedHandleRejected, "Managed Component wrapper accepted a removed native handle");
		ProtocolBehaviour addedBehaviour = parent.AddComponent<ProtocolBehaviour>();
		Expect(ReferenceEquals(addedBehaviour.GameObject, parent) && addedBehaviour.Enabled &&
		           parent.GetComponent<ProtocolBehaviour>() != null,
		       "GameObject.AddComponent did not attach a managed Behaviour instance");
		addedBehaviour.Remove();
		TextureAsset textureAsset = TextureAsset.Load(new AssetReference(fakeTextureGuid));
		TextureFieldTests.Run(textureAsset);
		MaterialAsset materialAsset = MaterialAsset.Load(new AssetReference(fakeMaterialGuid));
		Expect(textureAsset.IsValid && materialAsset.IsValid && textureAsset.Reference.Guid == fakeTextureGuid,
		       "Managed Texture/Material asset wrappers failed to preserve stable references");
		bool wrongAssetTypeRejected = false;
		try
		{
			_ = TextureAsset.Load(new AssetReference(fakeMaterialGuid));
		}
		catch (InvalidOperationException)
		{
			wrongAssetTypeRejected = true;
		}
		Expect(wrongAssetTypeRejected, "Managed Texture wrapper accepted a Material reference");
		Bootstrap.NativeHandle behaviorOwnerA = ToBootstrapHandle(parent.Handle);
		Bootstrap.NativeHandle behaviorOwnerB = ToBootstrapHandle(child.Handle);
		byte[] behaviorName = Encoding.UTF8.GetBytes(typeof(ProtocolBehaviour).AssemblyQualifiedName!);
		ulong firstToken = 0;
		ulong secondToken = 0;
		fixed(byte* typeName = behaviorName)
		{
			delegate* unmanaged[Stdcall]<byte*, ulong, Bootstrap.NativeHandle*, ulong*, int> create =
			    &Bootstrap.CreateBehaviour;
			Expect(create(typeName, (ulong)behaviorName.Length, &behaviorOwnerA, &firstToken) == 0 && firstToken != 0 &&
			           create(typeName, (ulong)behaviorName.Length, &behaviorOwnerB, &secondToken) == 0 &&
			           secondToken != 0 && firstToken != secondToken,
			       "Managed Behaviour factory did not create independent instances");
		}
		Bootstrap.NativeHandle nativeScriptHandleA =
		    new() { sessionId = parent.Handle.sessionId, index = 900, generation = 1, kind = 2 };
		Bootstrap.NativeHandle nativeScriptHandleB =
		    new() { sessionId = child.Handle.sessionId, index = 901, generation = 1, kind = 2 };
		delegate* unmanaged[Stdcall]<ulong, Bootstrap.NativeHandle*, int> bind = &Bootstrap.BindBehaviour;
		delegate* unmanaged[Stdcall]<ulong, uint, int> invoke = &Bootstrap.InvokeBehaviour;
		delegate* unmanaged[Stdcall]<ulong, int> destroy = &Bootstrap.DestroyBehaviour;
		Expect(bind(firstToken, &nativeScriptHandleA) == 0 && bind(secondToken, &nativeScriptHandleB) == 0,
		       "Managed Behaviour did not bind its native Component handle");
		for (uint eventId = 0; eventId < 8; ++eventId)
		{
			Expect(invoke(firstToken, eventId) == 0 && invoke(secondToken, eventId) == 0,
			       "Managed Behaviour lifecycle dispatch failed");
		}
		Expect(parent.Name.EndsWith(":OnDestroy", StringComparison.Ordinal) &&
		           child.Name.EndsWith(":OnDestroy", StringComparison.Ordinal) && destroy(firstToken) == 0 &&
		           destroy(secondToken) == 0 && invoke(firstToken, 4) != 0,
		       "Managed Behaviour lifecycle state was shared or retained after removal");
		Expect(Encoding.UTF8.GetString(AssetFileSystem.ReadAllBytes("user:ManagedBehaviourOnDestroy.txt")) ==
		           child.Name,
		       "Managed OnDestroy did not retain access to its owning GameObject");
		fakeInputDown = 1;
		fakeInputPressed = 1;
		fakeInputReleased = 0;
		Expect(Input.GetKey(KeyCode.W) && Input.GetKeyDown(KeyCode.W) && !Input.GetKeyUp(KeyCode.W),
		       "Managed Input wrapper did not read the frame snapshot");
		Expect(Time.DeltaTime == 0.016f && Time.FixedDeltaTime == 0.02f && Time.ElapsedTime == 10.5 &&
		           Time.FixedElapsedTime == 10.4 && Time.FrameCount == 99,
		       "Managed Time wrapper did not expose frame timing");
		Debug.LogWarning("托管日志");
		Expect(fakeLogLevel == 3 && fakeLog == "托管日志", "Managed Debug wrapper did not preserve UTF-8 logs");
		AssetFileSystem.WriteAllBytes("user:test.bin", new byte[] { 1, 3, 5, 7 });
		Expect(AssetFileSystem.ReadAllBytes("user:test.bin").AsSpan().SequenceEqual(new byte[] { 1, 3, 5, 7 }),
		       "Managed VFS wrapper did not round-trip file bytes");
		return 0;
	}

	private static Bootstrap.NativeHandle ToBootstrapHandle(NativeHandle handle)
	{
		return new Bootstrap.NativeHandle { sessionId = handle.sessionId, index = handle.index,
			                                generation = handle.generation, kind = handle.kind,
			                                reserved = handle.reserved };
	}
}

public sealed class ProtocolBehaviour : Behaviour
{
	public override void Awake() => Append("Awake");

	public override void OnEnable() => Append("OnEnable");

	public override void Start() => Append("Start");

	public override void FixedUpdate() => Append("FixedUpdate");

	public override void Update() => Append("Update");

	public override void LateUpdate() => Append("LateUpdate");

	public override void OnDisable() => Append("OnDisable");

	public override void OnDestroy()
	{
		Append("OnDestroy");
		AssetFileSystem.WriteAllBytes("user:ManagedBehaviourOnDestroy.txt", Encoding.UTF8.GetBytes(GameObject.Name));
	}

	private void Append(string eventName)
	{
		GameObject.Name += $":{eventName}";
	}
}

[UrsaScriptType("test.serialized-probe", Version = 1)]
public sealed class SerializedProbeBehaviour : Behaviour
{
	public int savedValue;

	public override void Awake()
	{
		GameObject.Name += $":Awake:{savedValue}";
	}
}

public sealed class ThrowOnAwakeBehaviour : Behaviour
{
	public override void Awake() => throw new InvalidOperationException("Synthetic Awake failure");
}

public sealed class ThrowOnFixedUpdateBehaviour : Behaviour
{
	public override void FixedUpdate() => throw new InvalidOperationException("Synthetic FixedUpdate failure");
}

public sealed class ThrowOnUpdateBehaviour : Behaviour
{
	public override void Update() => throw new InvalidOperationException("Synthetic Update failure");
}

public sealed class ThrowOnDisableBehaviour : Behaviour
{
	public override void OnDisable() => throw new InvalidOperationException("Synthetic OnDisable failure");
}

public sealed class ThrowOnDestroyBehaviour : Behaviour
{
	public override void OnDestroy() => throw new InvalidOperationException("Synthetic OnDestroy failure");
}

public sealed class SchemaProbeBehaviour : Behaviour
{
	public int publicValue;
	[SerializeField]
	private string hiddenValue = string.Empty;
	[NonSerialized]
	public int transientValue;
	public static int staticValue;
	public readonly int readOnlyValue;
	public int propertyValue { get; set; }
	private int unmarkedPrivateValue = 0;
	public object unsupportedValue = new();

	public int ReadUnmarkedPrivateValue() => unmarkedPrivateValue;
}

public enum PayloadMode : byte
{
	First,
	Second
}

[UrsaScriptType("test.payload", Version = 2)]
public sealed class PayloadProbeBehaviour : Behaviour
{
	public int publicValue;
	[SerializeField]
	private string hiddenValue = string.Empty;
	public int[] values = Array.Empty<int>();
	public int[]? nullValues;
	public PayloadMode mode;
	public bool enabled;
	public sbyte signedByte;
	public byte unsignedByte;
	public short signedShort;
	public ushort unsignedShort;
	public uint unsignedValue;
	public ulong unsignedLong;
	public long count;
	public double ratio;
	public string? nullableText;
	public Vector3 position;
	public Quaternion rotation;
	public Color color;
	public AssetReference asset;
	public System.Collections.Generic.List<string> labels = new();
	public GameObject? targetObject;
	public CameraComponent? targetCamera;
	public GameObject?[] objectTargets = Array.Empty<GameObject?>();
	public GameObject?[]? nullObjectTargets;
	public System.Collections.Generic.List<GameObject?> objectList = new();
	public CameraComponent?[] cameraTargets = Array.Empty<CameraComponent?>();
	public System.Collections.Generic.List<CameraComponent?> cameraList = new();

	public void SetHiddenValue(string value) => hiddenValue = value;

	public string GetHiddenValue() => hiddenValue;
}

[UrsaScriptType("test.reference", Version = 1)]
public sealed class ReferenceProbeBehaviour : Behaviour
{
	public GameObject? targetObject;
	public CameraComponent? targetCamera;
	public ReferenceProbeBehaviour? targetBehaviour;

	public override void Awake()
	{
		GameObject.Name = $"{targetObject?.Name}:{targetCamera?.GameObject.Name}:{targetBehaviour?.GameObject.Name}";
	}
}

[UrsaScriptType("test.migration", Version = 2)]
public sealed class MigrationProbeBehaviour : Behaviour
{
	[FormerlySerializedAs("oldName")]
	public int newName;
}
