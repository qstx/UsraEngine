using System;
using System.Collections.Generic;
using System.Threading;

namespace Ursa.Rendering;

/// <summary>Owns temporary targets recorded into one CommandBuffer recording generation.</summary>
public sealed class TemporaryRenderTargetScope : IDisposable
{
	private static long nextId;
	private readonly CommandBuffer commands;
	private readonly uint recordingGeneration;
	private readonly List<uint> targets = new();
	private bool disposed;

	internal TemporaryRenderTargetScope(CommandBuffer commands, uint recordingGeneration)
	{
		this.commands = commands;
		this.recordingGeneration = recordingGeneration;
	}

	public RenderTargetIdentifier Allocate(RenderTextureDescriptor descriptor)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		ArgumentNullException.ThrowIfNull(descriptor);
		EnsureRecording();
		uint id;
		do { id = unchecked((uint)Interlocked.Increment(ref nextId)); } while (id == 0);
		commands.GetTemporaryTarget(id, descriptor);
		targets.Add(id);
		return new RenderTargetIdentifier(id);
	}

	public void Release(RenderTargetIdentifier target)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		EnsureRecording();
		if (target.Id == 0 || target.Texture != null || !targets.Contains(target.Id))
			throw new ArgumentException("Temporary target is not owned by this scope.", nameof(target));
		commands.ReleaseTemporaryTarget(target.Id);
		targets.Remove(target.Id);
	}

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		if (!commands.IsAlive || commands.RecordingGeneration != recordingGeneration)
		{
			targets.Clear();
			commands.ReleaseTemporaryScope();
			return;
		}
		List<Exception>? errors = null;
		for (int index = targets.Count - 1; index >= 0; --index)
		{
			try { commands.ReleaseTemporaryTarget(targets[index]); }
			catch (Exception error) { (errors ??= new()).Add(error); }
		}
		targets.Clear();
		commands.ReleaseTemporaryScope();
		if (errors != null) throw new AggregateException("Temporary render target cleanup failed.", errors);
	}

	private void EnsureRecording()
	{
		if (!commands.IsAlive || commands.RecordingGeneration != recordingGeneration)
			throw new InvalidOperationException("The CommandBuffer recording owning this temporary target scope was cleared or disposed.");
	}
}
