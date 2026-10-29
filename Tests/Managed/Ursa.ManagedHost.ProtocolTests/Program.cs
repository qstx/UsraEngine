using System;
using System.Buffers.Binary;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Ursa.ManagedHost;
using Ursa;
using Ursa.Bindings;
using CommandCodec = Ursa.Bindings.Generated.RenderingCommands;

internal static unsafe partial class Program
{
	private sealed class FakeObject
	{
		internal string name = string.Empty;
		internal string persistentId = string.Empty;
		internal bool active = true;
		internal int layer;
		internal NativeHandle parent;
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
		internal CameraRenderingSettings rendering = new();
	}

	private sealed class PipelineProbeAsset : Ursa.Rendering.RenderPipelineAsset
	{
		internal int renders;
		internal int disposals;
		internal int cameraCount;
		internal System.Collections.Generic.List<Ursa.Rendering.Camera>? receivedCameras;
		internal bool failFactory;
		internal bool failDispose;
		internal bool failRender;
		internal Ursa.Rendering.RenderPipelineAsset? switchDuringRender;

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
			System.Collections.Generic.List<Ursa.Rendering.Camera> cameras)
		{
			++owner.renders;
			if (owner.switchDuringRender != null)
			{
				Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(owner.switchDuringRender);
				Expect(ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActiveAsset, owner) && owner.disposals == 0,
				       "A pipeline switch disposed the active instance inside its Render callback");
			}
			if (owner.failRender) throw new InvalidOperationException("pipeline render probe");
			owner.receivedCameras = cameras;
			foreach (Ursa.Rendering.Camera camera in cameras)
			{
				++owner.cameraCount;
				Ursa.Rendering.RenderPipelineManager.BeginCamera(context, camera);
				try { context.SetupCameraProperties(camera); }
				finally { Ursa.Rendering.RenderPipelineManager.EndCamera(context, camera); }
			}
		}
		protected override void Dispose(bool disposing)
		{
			++owner.disposals;
			if (owner.failDispose) throw new InvalidOperationException("pipeline dispose probe");
		}
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

	private static void ManagedRenderContextApiTests()
	{
		GameObject owner = GameObject.Create("Render Camera");
		CameraComponent camera = owner.AddComponent<CameraComponent>();
		var drawing = new Ursa.Rendering.DrawingSettings { ShaderTags = new[] { "SRPDefaultUnlit" } };
		var filtering = new Ursa.Rendering.FilteringSettings();
		using var context = new Ursa.Rendering.ScriptableRenderContext(1.0f);
		context.SetupCameraProperties(camera);
		var cameraView = Ursa.Rendering.Camera.FromComponent(camera);
		Expect(ReferenceEquals(cameraView, Ursa.Rendering.Camera.FromComponent(camera)) && cameraView.PixelWidth == 64,
		       "Camera wrappers were not reused or did not receive captured target dimensions");
		ulong previousCameraSession = BehaviourRegistry.currentSession;
		BehaviourRegistry.currentSession = 99999;
		bool cameraSessionRejected = false;
		try { context.SetupCameraProperties(cameraView); }
		catch (ObjectDisposedException) { cameraSessionRejected = true; }
		finally { BehaviourRegistry.currentSession = previousCameraSession; }
		Expect(cameraSessionRejected, "A camera wrapper was accepted in another script session");
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
		commands.DrawMesh(mesh!, material!, Matrix4x4.Identity);
		commands.SetViewProjectionMatrix(Matrix4x4.Identity);
		var originalVectors = new[] { new Vector4(6.25f, -9.5f, 13.75f, 0.125f), new Vector4(-3.5f, 2.25f, 17.5f, -0.75f) };
		var vectors = (Vector4[])originalVectors.Clone();
		byte[] originalVectorBytes = MemoryMarshal.AsBytes(originalVectors.AsSpan()).ToArray();
		var originalMatrices = new[] { new Matrix4x4(new float[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }) };
		var matrices = (Matrix4x4[])originalMatrices.Clone();
		byte[] originalMatrixBytes = MemoryMarshal.AsBytes(originalMatrices.AsSpan()).ToArray();
		commands.SetGlobalVectorArray("_ProbeVectors", vectors);
		commands.SetGlobalMatrixArray("_ProbeMatrices", matrices);
		vectors[0] = new Vector4(91.125f, -71.25f, 53.5f, 19.75f);
		matrices[0] = new Matrix4x4(new float[] { 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166 });
		bool oversizedVectorArrayRejected = false;
		try { commands.SetGlobalVectorArray("_TooManyVectors", new Vector4[Ursa.Rendering.CommandBuffer.MaximumGlobalParameterArrayElements + 1]); }
		catch (ArgumentOutOfRangeException) { oversizedVectorArrayRejected = true; }
		bool oversizedMatrixArrayRejected = false;
		try { commands.SetGlobalMatrixArray("_TooManyMatrices", new Matrix4x4[Ursa.Rendering.CommandBuffer.MaximumGlobalParameterArrayElements + 1]); }
		catch (ArgumentOutOfRangeException) { oversizedMatrixArrayRejected = true; }
		Expect(Ursa.Rendering.CommandBuffer.MaximumGlobalParameterArrayElements == 16 && oversizedVectorArrayRejected &&
		       oversizedMatrixArrayRejected, "CommandBuffer accepted an array above its public fixed capacity");
		commands.SetDepthBias(128, 0.25f);
		commands.RestoreDepthBias();
		commands.Execute(context);
		Expect(submittedRenderBatches.Count > 0, "CommandBuffer did not submit a batch");
		byte[] executedBatch = submittedRenderBatches[^1];
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(8)) == 12,
		       "CommandBuffer did not preserve command count");
		uint[] expectedOpcodes = { CommandCodec.Opcode_RenderCommands_SetRenderTarget, CommandCodec.Opcode_RenderCommands_ClearTarget,
			CommandCodec.Opcode_RenderCommands_SetViewport, CommandCodec.Opcode_RenderCommands_SetScissor,
			CommandCodec.Opcode_RenderCommands_DrawMesh, CommandCodec.Opcode_RenderCommands_SetGlobalMatrix,
			CommandCodec.Opcode_RenderCommands_SetGlobalVectorArray, CommandCodec.Opcode_RenderCommands_SetGlobalMatrixArray,
			CommandCodec.Opcode_RenderCommands_SetGlobalInt, CommandCodec.Opcode_RenderCommands_SetGlobalInt,
			CommandCodec.Opcode_RenderCommands_SetGlobalFloat, CommandCodec.Opcode_RenderCommands_SetGlobalInt };
		int offset = BindingCommandWriter.HeaderSize;
		foreach (uint opcode in expectedOpcodes)
		{
			Expect(BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(offset)) == opcode,
			       "CommandBuffer changed recorded command order");
			offset += checked((int)BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(offset + 4)));
		}
		Expect(executedBatch.AsSpan().IndexOf(originalVectorBytes) >= 0 &&
		       executedBatch.AsSpan().IndexOf(originalMatrixBytes) >= 0 &&
		       executedBatch.AsSpan().IndexOf(MemoryMarshal.AsBytes(vectors.AsSpan()).ToArray()) < 0 &&
		       executedBatch.AsSpan().IndexOf(MemoryMarshal.AsBytes(matrices.AsSpan()).ToArray()) < 0,
		       "Global uniform arrays were not copied when the CommandBuffer recorded them");
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
		       BinaryPrimitives.ReadUInt32LittleEndian(nextBatch.AsSpan(BindingCommandWriter.HeaderSize)) == CommandCodec.Opcode_RenderCommands_SetRenderTarget &&
		       BinaryPrimitives.ReadUInt32LittleEndian(nextBatch.AsSpan(BindingCommandWriter.HeaderSize + 12)) == 13,
		       "Cleared CommandBuffer did not execute only its newly recorded commands");
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(executedBatch.AsSpan(8)) == 12,
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
		uint[] resourceOpcodes = { CommandCodec.Opcode_RenderCommands_SetGlobalFloat, CommandCodec.Opcode_RenderCommands_SetGlobalVector,
			CommandCodec.Opcode_RenderCommands_SetGlobalTexture, CommandCodec.Opcode_RenderCommands_GetTemporaryTarget,
			CommandCodec.Opcode_RenderCommands_SetRenderTarget, CommandCodec.Opcode_RenderCommands_ReleaseTemporaryTarget,
			CommandCodec.Opcode_RenderCommands_SetRenderTarget, CommandCodec.Opcode_RenderCommands_CopyTexture,
			CommandCodec.Opcode_RenderCommands_Blit, CommandCodec.Opcode_RenderCommands_CopyBuffer };
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(resourceBatch.AsSpan(8)) == resourceOpcodes.Length,
		       "Resource and compositing commands were not recorded");
		offset = BindingCommandWriter.HeaderSize;
		foreach (uint opcode in resourceOpcodes)
		{
			Expect(BinaryPrimitives.ReadUInt32LittleEndian(resourceBatch.AsSpan(offset)) == opcode,
			       "Resource and compositing command order changed");
			offset += checked((int)BinaryPrimitives.ReadUInt32LittleEndian(resourceBatch.AsSpan(offset + 4)));
		}
		commands.Clear();
		commands.GetTemporaryTarget(89, descriptor);
		commands.SetGlobalTexture("_Source", (Ursa.Rendering.RenderTargetIdentifier)89u);
		commands.SetGlobalInt("_Mode", 3);
		commands.SetGlobalMatrix("_Transform", Matrix4x4.Identity);
		commands.SetGlobalBuffer("_Data", sourceBuffer);
		commands.Blit(89u, Ursa.Rendering.RenderTargetIdentifier.CameraTarget, blitMaterial!);
		commands.SetRenderTarget(Ursa.Rendering.RenderTargetIdentifier.CameraTarget);
		commands.SetRenderTargets(new Ursa.Rendering.RenderTargetIdentifier[] {sourceTexture, destinationTexture});
		commands.DrawFullscreen(blitMaterial!);
		commands.CopyTexture((Ursa.Rendering.RenderTargetIdentifier)sourceTexture,
		                     (Ursa.Rendering.RenderTargetIdentifier)destinationTexture);
		commands.ReleaseTemporaryTarget(89);
		commands.Execute(context);
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(submittedRenderBatches[^1].AsSpan(8)) == 11,
		       "Extended target/global/fullscreen commands were not delivered");
		commands.Clear();
		var compute = ComputeShader.FromReference(new AssetReference("12345678-1234-5678-9012-345678901234"));
		int kernel = compute.FindKernel("Fill");
		Expect(kernel == 0 && compute.GetKernelThreadGroupSizes(kernel) == (8u, 8u, 1u),
		       "ComputeShader lookup did not preserve the imported kernel metadata");
		commands.SetComputeFloat(compute, kernel, "_Intensity", 0.75f);
		commands.SetComputeInt(compute, kernel, "_Bias", 7);
		commands.SetComputeVector(compute, kernel, "_Color", new Vector4(0.5f, 0.25f, 0.75f, 1));
		commands.SetComputeMatrix(compute, kernel, "_Matrix", Matrix4x4.Identity);
		commands.SetComputeTexture(compute, kernel, "_Output", sourceTexture);
		commands.SetComputeBuffer(compute, kernel, "_Data", sourceBuffer);
		commands.DispatchCompute(compute, kernel, 1);
		commands.Execute(context);
		byte[] computeBatch = submittedRenderBatches[^1];
		Expect(BinaryPrimitives.ReadUInt32LittleEndian(computeBatch.AsSpan(8)) == 7 &&
		           BinaryPrimitives.ReadUInt32LittleEndian(computeBatch.AsSpan(BindingCommandWriter.HeaderSize)) == CommandCodec.Opcode_RenderCommands_SetComputeFloat &&
		           Encoding.UTF8.GetString(computeBatch.AsSpan(BindingCommandWriter.HeaderSize + 20, 36)) == compute.Reference.Guid,
		       "Compute commands lost their typed persistent identity or parameter framing");
		bool invalidComputeRejected = false;
		try { commands.DispatchCompute(compute, -1, 1); }
		catch (ArgumentOutOfRangeException) { invalidComputeRejected = true; }
		Expect(invalidComputeRejected, "Compute dispatch accepted a negative kernel index");
		invalidComputeRejected = false;
		try { commands.DispatchCompute(compute, kernel, 0); }
		catch (ArgumentOutOfRangeException) { invalidComputeRejected = true; }
		Expect(invalidComputeRejected, "Compute dispatch accepted an empty group range");
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
		var first = Ursa.ScriptableObject.CreateInstance<PipelineProbeAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(first);
		Expect(Ursa.Rendering.RenderPipelineManager.ActivePipeline == null,
		       "A pipeline request created an instance before a render boundary");
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
		var second = Ursa.ScriptableObject.CreateInstance<PipelineProbeAsset>();
		second.failFactory = true;
		bool factoryRejected = false;
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(second);
		try { Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges(); }
		catch (InvalidOperationException error)
		{
			factoryRejected = error.Message.Contains(nameof(PipelineProbeAsset)) &&
			                  error.Message.Contains("CreatePipeline") &&
			                  error.InnerException?.Message == "pipeline factory probe";
		}
		Expect(factoryRejected && ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActiveAsset, first) && first.disposals == 0,
		       "Failed pipeline candidate replaced or disposed the active pipeline");
		int began = 0, ended = 0, camerasBegan = 0, camerasEnded = 0;
		Ursa.Rendering.RenderPipelineManager.BeginContextRendering += _ => ++began;
		Ursa.Rendering.RenderPipelineManager.EndContextRendering += _ => ++ended;
		Ursa.Rendering.RenderPipelineManager.BeginCameraRendering += (_, _) => ++camerasBegan;
		Ursa.Rendering.RenderPipelineManager.EndCameraRendering += (_, _) => ++camerasEnded;
		var cameraList = new System.Collections.Generic.List<Ursa.Rendering.Camera>
		{
			GameObject.Create("Pipeline Camera").AddComponent<CameraComponent>()
		};
		Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1);
		Expect(first.renders == 1 && began == 1 && ended == 1 && camerasBegan == 1 && camerasEnded == 1 &&
		       first.cameraCount == 1 && ReferenceEquals(first.receivedCameras, cameraList),
		       "Pipeline manager did not preserve cameras or pair context/camera events around Render");
		Ursa.Rendering.RenderPipeline previousPipeline = Ursa.Rendering.RenderPipelineManager.ActivePipeline!;
		second.ValidateFields();
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
		Expect(ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActivePipeline, previousPipeline),
		       "Validating a non-active asset rebuilt the active pipeline");
		first.ValidateFields();
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
		Expect(previousPipeline.IsDisposed && first.disposals == 1 &&
		       !ReferenceEquals(previousPipeline, Ursa.Rendering.RenderPipelineManager.ActivePipeline),
		       "Validating the active asset did not rebuild it at the next boundary");
		previousPipeline = Ursa.Rendering.RenderPipelineManager.ActivePipeline!;
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(null);
		Expect(!previousPipeline.IsDisposed, "Requesting the default pipeline disposed the current instance immediately");
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
		Expect(first.disposals == 2 && previousPipeline.IsDisposed &&
		       Ursa.Rendering.RenderPipelineManager.ActivePipeline is Ursa.Rendering.ForwardRenderPipeline &&
		       Ursa.Rendering.RenderPipelineManager.ActiveAsset == null,
		       "Replacing the active pipeline did not dispose its previous instance");
		Ursa.Rendering.RenderPipelineManager.Reset();
		first.Dispose();
		second.Dispose();
		var forward = Ursa.ScriptableObject.CreateInstance<Ursa.Rendering.ForwardRenderPipelineAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(forward);
		Ursa.Rendering.RenderPipelineManager.ApplyPendingChanges();
		Expect(Ursa.Rendering.RenderPipelineManager.ActivePipeline is Ursa.Rendering.ForwardRenderPipeline,
		       "Framework Forward pipeline asset factory returned the wrong pipeline");
		Ursa.Rendering.RenderPipelineManager.Reset();
		forward.Dispose();
		using var switching = ScriptableObject.CreateInstance<PipelineProbeAsset>();
		using var replacement = ScriptableObject.CreateInstance<PipelineProbeAsset>();
		switching.switchDuringRender = replacement;
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(switching);
		Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1);
		Expect(ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActiveAsset, switching),
		       "A render callback's pending switch was applied before the next boundary");
		Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1);
		Expect(ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActiveAsset, replacement) && switching.disposals == 1,
		       "The pending switch did not apply at the following render boundary");
		replacement.failDispose = true;
		int oldEvents = 0;
		Ursa.Rendering.RenderPipelineManager.EndContextRendering += _ => ++oldEvents;
		bool disposeRejected = false;
		try { Ursa.Rendering.RenderPipelineManager.Reset(); }
		catch (InvalidOperationException) { disposeRejected = true; }
		Expect(disposeRejected && Ursa.Rendering.RenderPipelineManager.ActivePipeline == null &&
		       Ursa.Rendering.RenderPipelineManager.ActiveAsset == null,
		       "A Dispose exception prevented manager state cleanup");
		int cleanupEvents = 0;
		using var failing = ScriptableObject.CreateInstance<PipelineProbeAsset>();
		failing.failRender = true;
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(failing);
		Ursa.Rendering.RenderPipelineManager.EndContextRendering += _ => ++cleanupEvents;
		bool renderRejected = false;
		try { Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1); }
		catch (InvalidOperationException error) { renderRejected = error.Message == "pipeline render probe"; }
		Expect(renderRejected && cleanupEvents == 1 && oldEvents == 0,
		       "The render failure lost its diagnostic/event or a failing Dispose retained an old event");
		int previousRenders = failing.renders;
		Ursa.Rendering.RenderPipelineManager.BeginContextRendering += _ => throw new InvalidOperationException("begin failure probe");
		bool beginRejected = false;
		try { Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1); }
		catch (InvalidOperationException error) { beginRejected = error.Message == "begin failure probe"; }
		Expect(beginRejected && cleanupEvents == 2 && failing.renders == previousRenders,
		       "A begin-context exception rendered a partial callback or lost its paired end event");
		Ursa.Rendering.RenderPipelineManager.Reset();
		int cameraEndEvents = 0;
		Ursa.Rendering.RenderPipelineManager.BeginCameraRendering += (_, _) => throw new InvalidOperationException("camera begin failure probe");
		Ursa.Rendering.RenderPipelineManager.EndCameraRendering += (_, _) => ++cameraEndEvents;
		bool cameraBeginRejected = false;
		try { Ursa.Rendering.RenderPipelineManager.Render(cameraList, 1); }
		catch (InvalidOperationException error) { cameraBeginRejected = error.Message == "camera begin failure probe"; }
		Expect(cameraBeginRejected && cameraEndEvents == 1,
		       "Default Forward did not pair its camera end event after a begin-event failure");
		Ursa.Rendering.RenderPipelineManager.Reset();
	}

	private static void ManagedRenderPipelineSessionTests()
	{
		const ulong editSession = 70001, runSession = 70002;
		void Select(ulong session)
		{
			ScriptableObjectRegistry.SelectSession(session);
			Ursa.Rendering.RenderPipelineManager.SelectSession(session);
		}
		var cameras = new System.Collections.Generic.List<Ursa.Rendering.Camera>
		{
			GameObject.Create("Session Pipeline Camera").AddComponent<CameraComponent>()
		};
		Select(editSession);
		var edit = ScriptableObject.CreateInstance<PipelineProbeAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(edit);
		int editEvents = 0, runEvents = 0;
		Ursa.Rendering.RenderPipelineManager.BeginContextRendering += _ => ++editEvents;
		Ursa.Rendering.RenderPipelineManager.Render(cameras, 1);
		var editPipeline = Ursa.Rendering.RenderPipelineManager.ActivePipeline!;
		Select(runSession);
		Expect(Ursa.Rendering.RenderPipelineManager.ActiveAsset == null,
		       "A Run session inherited the editing pipeline instance");
		var run = ScriptableObject.CreateInstance<PipelineProbeAsset>();
		Ursa.Rendering.RenderPipelineManager.SetPipelineAsset(run);
		Ursa.Rendering.RenderPipelineManager.BeginContextRendering += _ => ++runEvents;
		Ursa.Rendering.RenderPipelineManager.Render(cameras, 1);
		Expect(editEvents == 1 && runEvents == 1 && edit.disposals == 0,
		       "Run shared editing callbacks or disposed the editing pipeline");
		var runPipeline = Ursa.Rendering.RenderPipelineManager.ActivePipeline!;
		Ursa.Rendering.RenderPipelineManager.ReleaseSession(runSession);
		Expect(ScriptableObjectRegistry.ClearSession(runSession).Count == 0 && runPipeline.IsDisposed && !run.IsValid,
		       "Session close retained its pipeline or asset instance");
		bool oldAssetRejected = false;
		try { _ = run.InstanceId; }
		catch (ObjectDisposedException) { oldAssetRejected = true; }
		Expect(oldAssetRejected, "A closed Run session retained a usable pipeline asset handle");
		Select(editSession);
		Expect(ReferenceEquals(Ursa.Rendering.RenderPipelineManager.ActivePipeline, editPipeline) && edit.disposals == 0,
		       "Stop did not restore the editing pipeline instance");
		Ursa.Rendering.RenderPipelineManager.Render(cameras, 1);
		Expect(editEvents == 2 && runEvents == 1, "Stop retained a Run event or lost the editing callback");
		Ursa.Rendering.RenderPipelineManager.ReleaseSession(editSession);
		Expect(ScriptableObjectRegistry.ClearSession(editSession).Count == 0 && editPipeline.IsDisposed,
		       "Editing session close did not release its pipeline and asset");
		Select(0);
	}

	private static int Main(string[] args)
	{
		if (args.Length == 1 && args[0] == "--debug-symbols-only")
		{
			ScriptDebugSymbolsTests.Run();
			Console.WriteLine("Original framework/test source paths, checksums and DLL/PDB identities verified.");
			return 0;
		}
		InstallProtocolScriptIdentities();
		using var runtimeBindings = InstallRuntimeBindings();
		using var renderingBindings = InstallRenderingBindings();
		ScriptableObjectTests.Run();
		TestOwnerThreadIsolation();
		TestEditorObjectSnapshots();
		TestImporterRegistration();
		TestPostprocessorRegistration();
		RenderCommandProtocolTests.Run();
		RendererInputContractTests.Run();
		RenderOutputBackingCacheTests.Run();
		ScriptableAssetFieldTests.Run();
		UniversalPipelineAssetSettingsTests.Run();
		UniversalPipelineFeatureTests.Run();
		TemporaryRenderTargetScopeTests.Run();
		ShaderFieldTests.Run();
		ScriptableAssetGraphTests.Run();
		ScriptableAssetRecoveryTests.Run();
		ScriptableAssetSnapshotTests.Run();
		ScriptDebugSymbolsTests.Run();
		CustomEditorRegistrationTests.Run();
		delegate* unmanaged[Stdcall]<nint, int, int> bootstrap = &Bootstrap.InitializeBindings;
		Expect(bootstrap(0, sizeof(BindingBootstrap)) != 0, "Null binding bootstrap pointer was accepted");
		BindingBootstrap invalidBootstrap = default;
		Expect(bootstrap((nint)(&invalidBootstrap), sizeof(BindingBootstrap) - 1) != 0, "Truncated binding bootstrap was accepted");
		Expect(bootstrap((nint)(&invalidBootstrap), sizeof(BindingBootstrap)) != 0, "Unsupported binding bootstrap version was accepted");
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

		ManagedRenderContextApiTests();
		ManagedCommandBufferApiTests();
		ManagedRenderPipelineLifecycleTests();
		ManagedRenderPipelineSessionTests();
		using var emptyCommandWriter = new BindingCommandWriter(CommandCodec.Hash);
		byte[] renderBatch = emptyCommandWriter.ToArray();
		Ursa.Rendering.RenderContextToken renderToken = Ursa.Rendering.NativeRenderingBindings.Begin(9, 1);
		int submissionsBeforeProtocol = renderBatchSubmissions;
		Ursa.Rendering.RenderCommandProtocol.Submit(renderToken, renderBatch, "Protocol test");
		Expect(renderBatchSubmissions == submissionsBeforeProtocol + 1,
		       "Managed RenderCommandProtocol did not submit its fixed-layout batch");
		Ursa.Rendering.NativeRenderingBindings.End(renderToken);
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
		Expect(parent.GetChildren().Length == 1 && parent.GetChildren()[0].Name == child.Name &&
		           parent.GetComponentsInChildren<ReferenceProbeBehaviour>().Length == 0 &&
		           ReferenceEquals(parent.GetComponentsInChildren<ReferenceProbeBehaviour>(true).Single(), identityProbe),
		       "Child traversal lost component identity or ignored the inactive filter");
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
		ulong referenceToken = BehaviourRegistry.TokenFor(referenceBehaviour);
		ulong peerReferenceToken = BehaviourRegistry.TokenFor(referencePeer);
		ManagedHostBindings.DeserializeBehaviour(referenceToken, new() { Version = 1, Json = lifecyclePayload });
		ManagedHostBindings.DeserializeBehaviour(peerReferenceToken, new() { Version = 1, Json = peerLifecyclePayload });
		ManagedHostBindings.InvokeBehaviour(referenceToken, 0);
		ManagedHostBindings.InvokeBehaviour(peerReferenceToken, 0);
		Expect(referenceOwner.Name == "ReferenceTarget:父对象:ReferencePeer" &&
		           referencePeerOwner.Name == "::ReferenceTarget:父对象:ReferencePeer",
		       "Managed Awake did not observe bidirectional script, GameObject, and native Component references");
		GameObject missingReferenceOwner = GameObject.Create("MissingReferenceOwner");
		ReferenceProbeBehaviour missingReferenceBehaviour =
		    missingReferenceOwner.AddComponent<ReferenceProbeBehaviour>();
		byte[] missingReferencePayload = Encoding.UTF8.GetBytes(
		    "{\"FormatVersion\":1,\"ScriptType\":\"test.reference\",\"Version\":1,\"Fields\":{\"targetObject\":{\"$" +
		    "ursaReference\":{\"Kind\":\"GameObject\",\"Id\":\"99999999-9999-4999-8999-999999999999\"}}}}");
		ulong missingToken = BehaviourRegistry.TokenFor(missingReferenceBehaviour);
		ManagedHostBindings.DeserializeBehaviour(missingToken, new() { Version = 1, Json = Encoding.UTF8.GetString(missingReferencePayload) });
		bool missingGraphRejected = false;
		try { ManagedHostBindings.InvokeBehaviour(missingToken, 0); }
		catch (InvalidOperationException) { missingGraphRejected = true; }
		Expect(missingGraphRejected, "Managed Awake accepted a non-null missing GameObject reference");
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
		ulong firstToken = ManagedHostBindings.CreateBehaviour(typeof(ProtocolBehaviour).AssemblyQualifiedName!, parent.Handle);
		ulong secondToken = ManagedHostBindings.CreateBehaviour(typeof(ProtocolBehaviour).AssemblyQualifiedName!, child.Handle);
		Expect(firstToken != 0 && secondToken != 0 && firstToken != secondToken, "Managed Behaviour factory did not create independent instances");
		ManagedHostBindings.BindBehaviour(firstToken, new NativeHandle { sessionId = parent.Handle.sessionId, index = 900, generation = 1, kind = 2 });
		ManagedHostBindings.BindBehaviour(secondToken, new NativeHandle { sessionId = child.Handle.sessionId, index = 901, generation = 1, kind = 2 });
		for (uint eventId = 0; eventId < 8; ++eventId)
		{
			ManagedHostBindings.InvokeBehaviour(firstToken, eventId);
			ManagedHostBindings.InvokeBehaviour(secondToken, eventId);
		}
		Expect(parent.Name.EndsWith(":OnDestroy", StringComparison.Ordinal) && child.Name.EndsWith(":OnDestroy", StringComparison.Ordinal), "Managed Behaviour lifecycle state was shared");
		ManagedHostBindings.DestroyBehaviour(firstToken);
		ManagedHostBindings.DestroyBehaviour(secondToken);
		bool removedBehaviourRejected = false;
		try { ManagedHostBindings.InvokeBehaviour(firstToken, 4); }
		catch (InvalidOperationException) { removedBehaviourRejected = true; }
		Expect(removedBehaviourRejected, "Managed Behaviour token remained callable after removal");
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

	private static void InstallProtocolScriptIdentities()
	{
		const string sourceGuid = "11111111-1111-4111-8111-111111111111";
		var declared = new System.Collections.Generic.Dictionary<string, (string Id, uint Version)>(StringComparer.Ordinal)
		{
			[typeof(PayloadProbeBehaviour).FullName!] = ("test.payload", 2),
			[typeof(MigrationProbeBehaviour).FullName!] = ("test.migration", 2),
			[typeof(ReferenceProbeBehaviour).FullName!] = ("test.reference", 1),
			[typeof(ScriptableAssetFieldTests).FullName! + "+Settings"] = ("test.texture-settings", 1),
			[typeof(ShaderFieldTests).FullName! + "+Settings"] = ("demo.render.settings", 1),
			[typeof(ShaderFieldTests).FullName! + "+Script"] = ("test.shader-behaviour", 1),
			[typeof(TextureFieldTests).FullName! + "+Settings"] = ("test.texture-settings", 1),
			[typeof(ScriptableAssetSnapshotTests).FullName! + "+Settings"] = ("test.snapshot-fields", 1),
			[typeof(ScriptableAssetGraphTests).FullName! + "+Settings"] = ("test.graph-settings", 1),
			[typeof(ScriptableAssetRecoveryTests).FullName! + "+Before"] = ("test.recoverable-settings", 1),
			[typeof(ScriptableAssetRecoveryTests).FullName! + "+Renamed"] = ("test.recoverable-settings", 1),
			[typeof(ScriptableAssetRecoveryTests).FullName! + "+Incompatible"] = ("test.recoverable-settings", 1),
			[typeof(ScriptableAssetRecoveryTests).FullName! + "+Throwing"] = ("test.recoverable-settings", 1)
		};
		var resolved = new System.Collections.Generic.Dictionary<Type, ScriptIdentity>();
		BehaviourRegistry.identityResolver = type =>
		{
			if (!resolved.TryGetValue(type, out ScriptIdentity identity))
			{
				(string id, uint version) = declared.TryGetValue(type.FullName ?? type.Name, out var known)
					? known : ($"auto:{sourceGuid}:{type.FullName ?? type.Name}", 1u);
				identity = new ScriptIdentity(id, version, sourceGuid, 1000ul + (ulong)resolved.Count);
				resolved.Add(type, identity);
			}
			return identity;
		};
	}

	private static NativeHandle ToMockHandle(NativeHandle handle)
	{
		return new NativeHandle { sessionId = handle.sessionId, index = handle.index,
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

public sealed class MigrationProbeBehaviour : Behaviour
{
	[FormerlySerializedAs("oldName")]
	public int newName;
}
