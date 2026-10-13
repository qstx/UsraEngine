using System;
using System.Collections.Generic;
using System.Threading;
using Ursa;

namespace Ursa.Rendering;

public enum SortingCriteria : uint
{
	None,
	CommonOpaque,
	CommonTransparent
}

public sealed class DrawingSettings
{
	public string[] ShaderTags { get; set; } = Array.Empty<string>();
	public SortingCriteria Sorting { get; set; } = SortingCriteria.CommonOpaque;
	public float[] CameraPosition { get; set; } = new float[3];
	public string ActivePipeline { get; set; } = "UrsaForward";
	public int OverrideMaterialPass { get; set; } = -1;
}

public sealed class FilteringSettings
{
	public int MinimumQueue { get; set; }
	public int MaximumQueue { get; set; } = 5000;
	public uint LayerMask { get; set; } = uint.MaxValue;
}

public readonly struct CullingResults
{
	internal RenderContextToken Context { get; }
	internal uint Index { get; }
	internal uint Generation { get; }

	internal CullingResults(RenderContextToken context, uint index, uint generation)
	{
		Context = context;
		Index = index;
		Generation = generation;
	}
}

public readonly struct RendererList
{
	internal RenderContextToken Context { get; }
	internal uint Index { get; }
	internal uint Generation { get; }

	internal RendererList(RenderContextToken context, uint index, uint generation)
	{
		Context = context;
		Index = index;
		Generation = generation;
	}
}

public sealed class ScriptableRenderContext : IDisposable
{
	private static long nextFrameId;
	private readonly RenderContextToken context;
	private readonly List<RenderResource[]> retainedResources = new();
	private bool disposed;

	public ScriptableRenderContext(float aspect)
	{
		if (!float.IsFinite(aspect) || aspect <= 0)
			throw new ArgumentOutOfRangeException(nameof(aspect));
		ulong frameId = checked((ulong)Interlocked.Increment(ref nextFrameId));
		var payload = EngineApi.ResourceRequest<RenderContextPayload>(new
		{
			operation = "render.context.begin",
			context = TokenData(),
			frameId,
			aspect
		});
		context = payload.Context;
	}

	public CullingResults Cull(CameraComponent camera)
	{
		ArgumentNullException.ThrowIfNull(camera);
		RequireValid();
		var payload = EngineApi.ResourceRequest<RenderResultPayload>(new
		{
			operation = "render.cull",
			context = TokenData(),
			cameraPersistentId = camera.PersistentId
		});
		return new CullingResults(context, payload.Index, payload.Generation);
	}

	public void SetupCameraProperties(CameraComponent camera)
	{
		ArgumentNullException.ThrowIfNull(camera);
		RequireValid();
		EngineApi.ResourceRequest(new
		{
			operation = "render.camera.setup",
			context = TokenData(),
			cameraPersistentId = camera.PersistentId
		});
	}

	public RendererList CreateRendererList(CullingResults cullingResults, DrawingSettings drawing,
		FilteringSettings filtering)
	{
		ArgumentNullException.ThrowIfNull(drawing);
		ArgumentNullException.ThrowIfNull(filtering);
		RequireValid();
		if (!SameContext(cullingResults.Context, context))
			throw new InvalidOperationException("Culling results belong to another render context.");
		if (drawing.CameraPosition == null || drawing.CameraPosition.Length != 3)
			throw new ArgumentException("CameraPosition must contain three values.", nameof(drawing));
		var payload = EngineApi.ResourceRequest<RenderResultPayload>(new
		{
			operation = "render.rendererList.create",
			context = TokenData(),
			cullingContext = TokenData(cullingResults.Context),
			cullingIndex = cullingResults.Index,
			cullingGeneration = cullingResults.Generation,
			shaderTags = drawing.ShaderTags,
			sortingCriteria = (uint)drawing.Sorting,
			cameraPosition = drawing.CameraPosition,
			activePipeline = drawing.ActivePipeline,
			overrideMaterialPass = drawing.OverrideMaterialPass,
			minimumQueue = filtering.MinimumQueue,
			maximumQueue = filtering.MaximumQueue,
			layerMask = filtering.LayerMask
		});
		return new RendererList(context, payload.Index, payload.Generation);
	}

	public void Dispose()
	{
		if (disposed)
			return;
		EngineApi.ResourceRequest(new { operation = "render.context.end", context = TokenData() });
		retainedResources.Clear();
		disposed = true;
	}

	internal void Submit(ReadOnlySpan<byte> batch, IReadOnlyCollection<RenderResource> resources)
	{
		RequireValid();
		RenderCommandProtocol.Submit(context, batch);
		retainedResources.Add(new List<RenderResource>(resources).ToArray());
	}

	internal void ValidateRendererList(RendererList list)
	{
		RequireValid();
		if (!SameContext(list.Context, context) || list.Generation == 0)
			throw new InvalidOperationException("RendererList belongs to another or expired render context.");
	}

	private void RequireValid()
	{
		if (disposed)
			throw new ObjectDisposedException(nameof(ScriptableRenderContext));
	}

	private object TokenData() => new
	{
		sessionId = context.sessionId,
		frameId = context.frameId,
		index = context.index,
		generation = context.generation
	};

	private static object TokenData(RenderContextToken token) => new
	{
		sessionId = token.sessionId,
		frameId = token.frameId,
		index = token.index,
		generation = token.generation
	};

	private static bool SameContext(RenderContextToken left, RenderContextToken right) =>
		left.sessionId == right.sessionId && left.frameId == right.frameId && left.index == right.index &&
		left.generation == right.generation;

	private struct RenderContextPayload
	{
		public RenderContextToken Context { get; set; }
	}

	private struct RenderResultPayload
	{
		public RenderContextToken Context { get; set; }
		public uint Index { get; set; }
		public uint Generation { get; set; }
	}
}
