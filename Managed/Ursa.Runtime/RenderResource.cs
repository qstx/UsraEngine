using System;
using System.Collections.Generic;
using System.Linq;
namespace Ursa;

public abstract class RenderResource : IEquatable<RenderResource>
{
	private static readonly Dictionary<NativeHandle, WeakReference<RenderResource>> wrappers = new();
	internal NativeHandle Handle { get; }

	internal RenderResource(NativeHandle handle)
	{
		Handle = handle;
	}

	internal static T? Wrap<T>(NativeHandle handle, Func<NativeHandle, T> factory) where T : RenderResource
	{
		if (handle.sessionId == 0) return null;
		if (wrappers.TryGetValue(handle, out var cached) && cached.TryGetTarget(out var value)) return (T)value;
		if (wrappers.Count > 1024)
		{
			foreach (var entry in wrappers.Where(entry => !entry.Value.TryGetTarget(out _)).ToArray())
				wrappers.Remove(entry.Key);
		}
		T result = factory(handle);
		wrappers[handle] = new WeakReference<RenderResource>(result);
		return result;
	}

	public bool IsValid => EngineApi.ResourceValid(Handle);

	public bool Equals(RenderResource? other) => other is not null && Handle.Equals(other.Handle);

	public override bool Equals(object? obj) => obj is RenderResource other && Equals(other);

	public override int GetHashCode() => Handle.GetHashCode();

	public static bool operator ==(RenderResource? a, RenderResource? b) => ReferenceEquals(a, b) || a is not null && a.Equals(b);

	public static bool operator !=(RenderResource? a, RenderResource? b) => !(a == b);
}
