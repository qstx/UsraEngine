using System;
using System.Collections.Generic;
using System.Threading;
using Ursa;

namespace Ursa.Rendering;

[Ursa.Bindings.WireValue]
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
	public Vector3? CameraPosition { get; set; }
	public string ActivePipeline { get; set; } = "UrsaForward";
	public int OverrideMaterialPass { get; set; } = -1;
	public Material? OverrideMaterial { get; set; }
}

[Ursa.Bindings.WireValue]
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
		context = NativeRenderingBindings.Begin(frameId, aspect);
	}

	public CullingResults Cull(Camera camera)
	{
		ArgumentNullException.ThrowIfNull(camera);
		RequireValid();
		var payload = NativeRenderingBindings.Cull(context, camera.PersistentId);
		return new CullingResults(context, payload.Index, payload.Generation);
	}

	public void SetupCameraProperties(Camera camera)
	{
		ArgumentNullException.ThrowIfNull(camera);
		RequireValid();
		var frame = NativeRenderingBindings.SetupCamera(context, camera.PersistentId);
		camera.Update(frame);
	}

	public RendererList CreateRendererList(CullingResults cullingResults, DrawingSettings drawing,
		FilteringSettings filtering)
	{
		ArgumentNullException.ThrowIfNull(drawing);
		ArgumentNullException.ThrowIfNull(filtering);
		RequireValid();
		if (!SameContext(cullingResults.Context, context))
			throw new InvalidOperationException("Culling results belong to another render context.");
		var payload = NativeRenderingBindings.CreateRendererList(context, new RenderResultValue
		{
			Context = cullingResults.Context, Index = cullingResults.Index, Generation = cullingResults.Generation
		}, new DrawingSettingsValue
		{
			ShaderTags = drawing.ShaderTags, Sorting = drawing.Sorting, CameraPosition = drawing.CameraPosition,
			ActivePipeline = drawing.ActivePipeline, OverrideMaterialPass = drawing.OverrideMaterialPass,
			OverrideMaterial = drawing.OverrideMaterial?.Handle
		}, filtering);
		return new RendererList(context, payload.Index, payload.Generation);
	}

	public void Dispose()
	{
		if (disposed)
			return;
		try { NativeRenderingBindings.End(context); }
		finally { retainedResources.Clear(); disposed = true; }
	}

	public void Submit()
	{
		RequireValid();
		NativeRenderingBindings.Submit(context);
	}

	public void ExecuteCommandBuffer(CommandBuffer commands)
	{
		ArgumentNullException.ThrowIfNull(commands);
		RequireValid();
		commands.Execute(this);
	}

	internal void ReadCameras(Dictionary<string, Camera> wrappers, List<Camera> cameras)
	{
		RequireValid();
		var descriptions = NativeRenderingBindings.Cameras(context);
		cameras.Clear();
		foreach (var description in descriptions)
		{
			if (!wrappers.TryGetValue(description.Identity, out Camera? camera))
			{
				camera = new Camera(description.Identity);
				wrappers.Add(description.Identity, camera);
			}
			camera.Update(description);
			cameras.Add(camera);
		}
		if (wrappers.Count > cameras.Count + 32)
		{
			var alive = new HashSet<Camera>(cameras);
			foreach (var entry in new List<KeyValuePair<string, Camera>>(wrappers))
				if (!alive.Contains(entry.Value)) wrappers.Remove(entry.Key);
		}
	}

	public void Abort()
	{
		if (disposed) return;
		try { NativeRenderingBindings.Abort(context); }
		finally { retainedResources.Clear(); disposed = true; }
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

	private static bool SameContext(RenderContextToken left, RenderContextToken right) =>
		left.sessionId == right.sessionId && left.frameId == right.frameId && left.index == right.index &&
		left.generation == right.generation;

}
