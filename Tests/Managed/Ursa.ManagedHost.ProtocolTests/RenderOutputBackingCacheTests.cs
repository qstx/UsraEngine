using System;
using System.Collections.Generic;
using Ursa.Rendering;

internal static class RenderOutputBackingCacheTests
{
	private sealed class Resource { }

	internal static void Run()
	{
		var released = new List<Resource>();
		int created = 0;
		using (var cache = new RenderOutputBackingCache<Resource>(2, 300_000,
			       (_, _) => { ++created; return new Resource(); }, released.Add))
		{
			var first = cache.Get(1, 11, 1, 100, 100);
			first.Initialized = true;
			var second = cache.Get(1, 22, 1, 100, 100);
			Expect(ReferenceEquals(cache.Get(1, 11, 1, 100, 100), first) && first.Initialized,
			       "Matching output identity, size and generation did not reuse its backing.");

			var third = cache.Get(1, 33, 1, 100, 100);
			Expect(cache.Count == 2 && released.Count == 1 && ReferenceEquals(released[0], second.Resource),
			       "Output backing capacity did not evict the least recently used target.");
			var replacement = cache.Get(1, 11, 2, 100, 100);
			Expect(!ReferenceEquals(replacement, first) && !replacement.Initialized &&
			       released.Contains(first.Resource),
			       "Changing an output generation reused stale backing contents.");
			var resized = cache.Get(1, 11, 3, 120, 100);
			Expect(resized.Width == 120 && resized.Height == 100 && !resized.Initialized &&
			       released.Contains(replacement.Resource),
			       "Changing output dimensions did not replace its backing.");
			Expect(cache.AllocatedBytes == 120_000 + 144_000 && created == 5,
			       "Output backing cache byte accounting changed after replacements and eviction.");

			int beforeOversized = created;
			bool oversizedRejected = false;
			try { cache.Get(1, 44, 1, 500, 500); }
			catch (InvalidOperationException error) { oversizedRejected = error.Message.Contains("budget"); }
			Expect(oversizedRejected && created == beforeOversized && cache.Count == 2,
			       "Output backing over-budget requests were not rejected before resource allocation.");

			var otherSession = cache.Get(2, 11, 1, 1, 1);
			Expect(!ReferenceEquals(otherSession, resized) && cache.Count == 2,
			       "Output identities from separate script sessions shared the same backing.");
		}
		Expect(released.Count == created,
		       "Disposing the output backing cache did not release every allocated resource exactly once.");

		using var invalidCache = new RenderOutputBackingCache<Resource>(1, 1024, (_, _) => new Resource(), _ => { });
		Reject(() => invalidCache.Get(0, 1, 1, 1, 1), "Output cache accepted an invalid session identity.");
		Reject(() => invalidCache.Get(1, 0, 1, 1, 1), "Output cache accepted an invalid output identity.");
		Reject(() => invalidCache.Get(1, 1, 0, 1, 1), "Output cache accepted an invalid output generation.");
	}

	private static void Reject(Action action, string message)
	{
		try { action(); }
		catch (ArgumentOutOfRangeException) { return; }
		throw new Exception(message);
	}

	private static void Expect(bool value, string message)
	{
		if (!value) throw new Exception(message);
	}
}
