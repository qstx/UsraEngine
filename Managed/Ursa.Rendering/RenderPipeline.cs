using System;
using System.Collections.Generic;
using Ursa;

namespace Ursa.Rendering;

public abstract class RenderPipelineAsset : ScriptableObject
{
	protected abstract RenderPipeline CreatePipeline();

	internal RenderPipeline CreatePipelineInstance()
	{
		RequireValid();
		return CreatePipeline() ?? throw new InvalidOperationException(
			$"Render pipeline asset '{GetType().FullName}' returned no pipeline instance.");
	}
}

public abstract class RenderPipeline : IDisposable
{
	private bool disposed;

	public bool IsDisposed => disposed;

	public abstract void Render(ScriptableRenderContext context, List<CameraComponent> cameras);

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		Dispose(true);
		GC.SuppressFinalize(this);
	}

	protected virtual void Dispose(bool disposing) { }

	internal void RenderInternal(ScriptableRenderContext context, List<CameraComponent> cameras)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ArgumentNullException.ThrowIfNull(context);
		ArgumentNullException.ThrowIfNull(cameras);
		Render(context, cameras);
	}
}

public static class RenderPipelineManager
{
	private static RenderPipelineAsset? activeAsset;
	private static RenderPipeline? activePipeline;

	public static event Action<ScriptableRenderContext>? BeginContextRendering;
	public static event Action<ScriptableRenderContext>? EndContextRendering;
	public static event Action<ScriptableRenderContext, CameraComponent>? BeginCameraRendering;
	public static event Action<ScriptableRenderContext, CameraComponent>? EndCameraRendering;

	public static RenderPipeline? ActivePipeline => activePipeline;
	public static RenderPipelineAsset? ActiveAsset => activeAsset;

	public static void SetPipelineAsset(RenderPipelineAsset? candidate)
	{
		if (ReferenceEquals(activeAsset, candidate)) return;
		RenderPipeline? next = candidate?.CreatePipelineInstance();
		if (candidate != null && next == null)
			throw new InvalidOperationException("Pipeline asset did not create a pipeline.");
		RenderPipeline? previous = activePipeline;
		activeAsset = candidate;
		activePipeline = next;
		previous?.Dispose();
	}

	public static void Render(List<CameraComponent> cameras, float aspect)
	{
		RenderPipeline pipeline = activePipeline ?? throw new InvalidOperationException("No active render pipeline is set.");
		using var context = new ScriptableRenderContext(aspect);
		BeginContextRendering?.Invoke(context);
		try
		{
			pipeline.Render(context, cameras);
		}
		finally
		{
			try { EndContextRendering?.Invoke(context); }
			finally { context.Dispose(); }
		}
	}

	public static void BeginCamera(ScriptableRenderContext context, CameraComponent camera) =>
		BeginCameraRendering?.Invoke(context, camera);

	public static void EndCamera(ScriptableRenderContext context, CameraComponent camera) =>
		EndCameraRendering?.Invoke(context, camera);

	public static void Reset()
	{
		RenderPipeline? previous = activePipeline;
		activePipeline = null;
		activeAsset = null;
		previous?.Dispose();
		BeginContextRendering = null;
		EndContextRendering = null;
		BeginCameraRendering = null;
		EndCameraRendering = null;
	}
}
