using System;
using System.Collections.Generic;

namespace Ursa.Rendering;

internal sealed class RenderOutputBackingCache<T> : IDisposable where T : class
{
	internal sealed class Entry
	{
		internal readonly T Resource;
		internal readonly uint Width;
		internal readonly uint Height;
		internal readonly ulong Generation;
		internal readonly ulong AllocatedBytes;
		internal ulong LastUsedTick;
		internal bool Initialized;

		internal Entry(T resource, uint width, uint height, ulong generation, ulong allocatedBytes, ulong lastUsedTick)
		{
			Resource = resource;
			Width = width;
			Height = height;
			Generation = generation;
			AllocatedBytes = allocatedBytes;
			LastUsedTick = lastUsedTick;
		}
	}

	private readonly Dictionary<(ulong Session, ulong Identity), Entry> entries = new();
	private readonly Func<uint, uint, T> create;
	private readonly Action<T> release;
	private readonly int maximumEntries;
	private readonly ulong maximumBytes;
	private ulong allocatedBytes;
	private ulong clock;
	private bool disposed;

	internal RenderOutputBackingCache(int maximumEntries, ulong maximumBytes, Func<uint, uint, T> create,
	                                  Action<T> release)
	{
		if (maximumEntries <= 0) throw new ArgumentOutOfRangeException(nameof(maximumEntries));
		if (maximumBytes == 0) throw new ArgumentOutOfRangeException(nameof(maximumBytes));
		this.maximumEntries = maximumEntries;
		this.maximumBytes = maximumBytes;
		this.create = create ?? throw new ArgumentNullException(nameof(create));
		this.release = release ?? throw new ArgumentNullException(nameof(release));
	}

	internal int Count => entries.Count;
	internal ulong AllocatedBytes => allocatedBytes;

	internal Entry Get(ulong session, ulong identity, ulong generation, uint width, uint height)
	{
		ObjectDisposedException.ThrowIf(disposed, this);
		if (session == 0 || identity == 0 || generation == 0 || width == 0 || height == 0)
			throw new ArgumentOutOfRangeException(nameof(identity), "Render output identity, generation, and dimensions must be nonzero.");
		var key = (session, identity);
		if (entries.TryGetValue(key, out Entry? existing) &&
		    (existing.Generation != generation || existing.Width != width || existing.Height != height))
		{
			release(existing.Resource);
			entries.Remove(key);
			allocatedBytes -= existing.AllocatedBytes;
			existing = null;
		}
		if (existing != null)
		{
			existing.LastUsedTick = checked(++clock);
			return existing;
		}

		ulong requestedBytes = checked((ulong)width * height * 12ul);
		if (requestedBytes > maximumBytes)
			throw new InvalidOperationException($"A renderer output backing exceeds the {maximumBytes} byte target budget.");
		while (entries.Count >= maximumEntries || requestedBytes > maximumBytes - allocatedBytes)
			EvictLeastRecentlyUsed();
		T resource = create(width, height) ?? throw new InvalidOperationException("Output backing factory returned null.");
		var entry = new Entry(resource, width, height, generation, requestedBytes, checked(++clock));
		entries.Add(key, entry);
		allocatedBytes += requestedBytes;
		return entry;
	}

	private void EvictLeastRecentlyUsed()
	{
		if (entries.Count == 0)
			throw new InvalidOperationException("Renderer output backing budget cannot satisfy this target allocation.");
		KeyValuePair<(ulong Session, ulong Identity), Entry> leastRecentlyUsed = default;
		bool found = false;
		foreach (var candidate in entries)
		{
			if (!found || candidate.Value.LastUsedTick < leastRecentlyUsed.Value.LastUsedTick)
			{
				leastRecentlyUsed = candidate;
				found = true;
			}
		}
		release(leastRecentlyUsed.Value.Resource);
		entries.Remove(leastRecentlyUsed.Key);
		allocatedBytes -= leastRecentlyUsed.Value.AllocatedBytes;
	}

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		List<Exception>? errors = null;
		foreach (Entry entry in entries.Values)
		{
			try { release(entry.Resource); }
			catch (Exception error) { (errors ??= new()).Add(error); }
		}
		entries.Clear();
		allocatedBytes = 0;
		if (errors != null) throw new AggregateException("Output backing cache cleanup failed.", errors);
	}
}
