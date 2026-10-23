using System;
using System.Collections.Generic;
using System.Runtime.ExceptionServices;
using Ursa;

namespace Ursa.Rendering;

public abstract class RenderPipelineAsset : ScriptableObject
{
	protected abstract RenderPipeline CreatePipeline();

	internal virtual ulong GetConfigurationRevision() => ValidationRevision;

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

	public abstract void Render(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras);

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		try { Dispose(true); }
		finally { GC.SuppressFinalize(this); }
	}

	protected virtual void Dispose(bool disposing) { }

	internal void RenderInternal(ScriptableRenderContext context, List<Ursa.Rendering.Camera> cameras)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ArgumentNullException.ThrowIfNull(context);
		ArgumentNullException.ThrowIfNull(cameras);
		Render(context, cameras);
	}
}

public static class RenderPipelineManager
{
	private sealed class PipelineState
	{
		internal readonly ulong session;
		internal RenderPipelineAsset? activeAsset;
		internal RenderPipeline? activePipeline;
		internal RenderPipelineAsset? pendingAsset;
		internal bool hasPendingSelection;
		internal ulong activeRevision;
		internal bool rendering;
		internal readonly List<Camera> cameras = new();
		internal readonly Dictionary<string, Camera> cameraWrappers = new(StringComparer.Ordinal);
		internal Action<ScriptableRenderContext>? beginContext;
		internal Action<ScriptableRenderContext>? endContext;
		internal Action<ScriptableRenderContext, Camera>? beginCamera;
		internal Action<ScriptableRenderContext, Camera>? endCamera;
		internal PipelineState(ulong session) => this.session = session;
	}

	[ThreadStatic] private static Dictionary<ulong, PipelineState>? ownerStates;
	[ThreadStatic] private static PipelineState? ownerCurrentState;
	private static Dictionary<ulong, PipelineState> states => ownerStates ??= new() { [0] = new PipelineState(0) };
	private static PipelineState currentState { get => ownerCurrentState ??= states[0]; set => ownerCurrentState = value; }
	private static RenderPipelineAsset? activeAsset { get => currentState.activeAsset; set => currentState.activeAsset = value; }
	private static RenderPipeline? activePipeline { get => currentState.activePipeline; set => currentState.activePipeline = value; }
	private static RenderPipelineAsset? pendingAsset { get => currentState.pendingAsset; set => currentState.pendingAsset = value; }
	private static bool hasPendingSelection { get => currentState.hasPendingSelection; set => currentState.hasPendingSelection = value; }
	private static ulong activeRevision { get => currentState.activeRevision; set => currentState.activeRevision = value; }
	private static bool rendering { get => currentState.rendering; set => currentState.rendering = value; }

	public static event Action<ScriptableRenderContext>? BeginContextRendering
	{
		add { ScriptableObjectRegistry.RequireThread(); currentState.beginContext += value; }
		remove { ScriptableObjectRegistry.RequireThread(); currentState.beginContext -= value; }
	}
	public static event Action<ScriptableRenderContext>? EndContextRendering
	{
		add { ScriptableObjectRegistry.RequireThread(); currentState.endContext += value; }
		remove { ScriptableObjectRegistry.RequireThread(); currentState.endContext -= value; }
	}
	public static event Action<ScriptableRenderContext, Camera>? BeginCameraRendering
	{
		add { ScriptableObjectRegistry.RequireThread(); currentState.beginCamera += value; }
		remove { ScriptableObjectRegistry.RequireThread(); currentState.beginCamera -= value; }
	}
	public static event Action<ScriptableRenderContext, Camera>? EndCameraRendering
	{
		add { ScriptableObjectRegistry.RequireThread(); currentState.endCamera += value; }
		remove { ScriptableObjectRegistry.RequireThread(); currentState.endCamera -= value; }
	}

	internal static void SelectSession(ulong session)
	{
		ScriptableObjectRegistry.RequireThread();
		if (currentState.session == session) return;
		if (rendering) throw new InvalidOperationException("Cannot change a script session inside a pipeline callback.");
		if (!states.TryGetValue(session, out PipelineState? state))
		{
			state = new PipelineState(session);
			states.Add(session, state);
		}
		currentState = state;
	}

	internal static void RequireSessionBoundary()
	{
		ScriptableObjectRegistry.RequireThread();
		if (rendering) throw new InvalidOperationException("Script session changes require a render boundary.");
	}

	internal static (bool HasSelection, RenderPipelineAsset? Asset) ReloadSelection()
	{
		RequireSessionBoundary();
		return (hasPendingSelection || activePipeline != null, hasPendingSelection ? pendingAsset : activeAsset);
	}

	internal static void ReleaseSession(ulong session)
	{
		ScriptableObjectRegistry.RequireThread();
		if (!states.TryGetValue(session, out PipelineState? state)) return;
		if (state.rendering || rendering) throw new InvalidOperationException("Cannot close a session inside a pipeline callback.");
		PipelineState previousState = currentState;
		currentState = state;
		if (session != 0) states.Remove(session);
		try { Reset(); }
		finally { currentState = ReferenceEquals(previousState, state) ? states[0] : previousState; }
	}

	public static RenderPipeline? ActivePipeline => activePipeline;
	public static RenderPipelineAsset? ActiveAsset => activeAsset;

	public static void SetPipelineAsset(RenderPipelineAsset? candidate)
	{
		if (ImportExecution.Active) throw new InvalidOperationException("Import callbacks cannot change the render pipeline.");
		ScriptableObjectRegistry.RequireThread();
		candidate?.RequireValid();
		pendingAsset = candidate;
		hasPendingSelection = true;
	}

	internal static void ApplyPendingChanges()
	{
		if (ImportExecution.Active) throw new InvalidOperationException("Import callbacks cannot create render pipelines.");
		ScriptableObjectRegistry.RequireThread();
		if (rendering) throw new InvalidOperationException("Pipeline changes require a render boundary.");
		RenderPipelineAsset? candidate = hasPendingSelection ? pendingAsset : activeAsset;
		candidate?.RequireValid();
		if (activePipeline != null && !activePipeline.IsDisposed && ReferenceEquals(activeAsset, candidate) &&
			(candidate?.GetConfigurationRevision() ?? 0) == activeRevision)
		{
			pendingAsset = null;
			hasPendingSelection = false;
			return;
		}
		pendingAsset = null;
		hasPendingSelection = false;
		string candidateLabel = candidate?.GetType().FullName ?? "Default Forward";
		if (candidate?.AssetReference is AssetReference identity)
			candidateLabel += $" ({identity.Guid}:{identity.LocalFileId})";
		RenderPipeline next;
		try
		{
			next = candidate?.CreatePipelineInstance() ?? new ForwardRenderPipeline();
			if (next.IsDisposed || ReferenceEquals(next, activePipeline))
				throw new InvalidOperationException("A pipeline factory must return a new live instance.");
		}
		catch (Exception error)
		{
			throw new InvalidOperationException($"RenderPipelineAsset '{candidateLabel}' CreatePipeline failed.", error);
		}
		RenderPipeline? previous = activePipeline;
		activeAsset = candidate;
		activePipeline = next;
		activeRevision = candidate?.GetConfigurationRevision() ?? 0;
		previous?.Dispose();
	}

	public static void Render(List<Ursa.Rendering.Camera> cameras, float aspect)
	{
		if (ImportExecution.Active) throw new InvalidOperationException("Import callbacks cannot render.");
		ArgumentNullException.ThrowIfNull(cameras);
		ApplyPendingChanges();
		var context = new ScriptableRenderContext(aspect);
		RenderContext(context, cameras);
	}

	internal static void RenderScene(float aspect)
	{
		ApplyPendingChanges();
		using var context = new ScriptableRenderContext(aspect);
		try
		{
			context.ReadCameras(currentState.cameraWrappers, currentState.cameras);
			if (currentState.cameras.Count == 0) throw new InvalidOperationException("Scene has no active rendering camera.");
		}
		catch { context.Abort(); throw; }
		RenderContext(context, currentState.cameras);
	}

	private static void RenderContext(ScriptableRenderContext context, List<Camera> cameras)
	{
		RenderPipeline pipeline = activePipeline!;
		List<Exception>? errors = null;
		rendering = true;
		try
		{
			currentState.beginContext?.Invoke(context);
			pipeline.RenderInternal(context, cameras);
		}
		catch (Exception error) { AddError(ref errors, error); }
		finally
		{
			try { currentState.endContext?.Invoke(context); }
			catch (Exception error) { AddError(ref errors, error); }
			try
			{
				if (errors == null) context.Dispose();
				else context.Abort();
			}
			catch (Exception error) { AddError(ref errors, error); }
			finally { rendering = false; }
		}
		if (errors?.Count == 1) ExceptionDispatchInfo.Capture(errors[0]).Throw();
		if (errors?.Count > 1) throw new AggregateException($"Render pipeline '{pipeline.GetType().FullName}' failed during rendering and cleanup.", errors);
	}

	private static void AddError(ref List<Exception>? errors, Exception error)
	{
		errors ??= new List<Exception>();
		errors.Add(error);
	}

	public static void BeginCamera(ScriptableRenderContext context, Camera camera)
	{
		ScriptableObjectRegistry.RequireThread();
		currentState.beginCamera?.Invoke(context, camera);
	}

	public static void EndCamera(ScriptableRenderContext context, Camera camera)
	{
		ScriptableObjectRegistry.RequireThread();
		currentState.endCamera?.Invoke(context, camera);
	}

	public static void Reset()
	{
		if (ImportExecution.Active) throw new InvalidOperationException("Import callbacks cannot reset render pipelines.");
		ScriptableObjectRegistry.RequireThread();
		if (rendering) throw new InvalidOperationException("Cannot reset a pipeline inside its render callback.");
		RenderPipeline? previous = activePipeline;
		activePipeline = null;
		activeAsset = null;
		pendingAsset = null;
		hasPendingSelection = false;
		activeRevision = 0;
		currentState.beginContext = null;
		currentState.endContext = null;
		currentState.beginCamera = null;
		currentState.endCamera = null;
		currentState.cameras.Clear();
		currentState.cameraWrappers.Clear();
		previous?.Dispose();
	}
}
