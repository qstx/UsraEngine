using System;

namespace Ursa.Rendering;

/// <summary>A reusable name for a GPU debug region recorded in a CommandBuffer.</summary>
public sealed class ProfilingSampler
{
	public ProfilingSampler(string name)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		if (name.Contains('\0') || System.Text.Encoding.UTF8.GetByteCount(name) > 1024)
			throw new ArgumentOutOfRangeException(nameof(name), "Sample names must fit within 1024 UTF-8 bytes and contain no null character.");
		Name = name;
	}

	public string Name { get; }
}

/// <summary>Records a matching sample pair when disposed.</summary>
public struct ProfilingScope : IDisposable
{
	private CommandBuffer? commands;
	private readonly string? name;
	private readonly uint generation;

	public ProfilingScope(CommandBuffer commands, ProfilingSampler sampler)
	{
		ArgumentNullException.ThrowIfNull(commands);
		ArgumentNullException.ThrowIfNull(sampler);
		this.commands = commands;
		name = sampler.Name;
		generation = commands.RecordingGeneration;
		commands.BeginSample(name);
	}

	public void Dispose()
	{
		CommandBuffer? target = commands;
		commands = null;
		if (target != null && name != null)
			target.EndSampleIfCurrent(name, generation);
	}
}
