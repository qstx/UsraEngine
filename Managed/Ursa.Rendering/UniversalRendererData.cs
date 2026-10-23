using System;
using Ursa;

namespace Ursa.Rendering;

[CreateAssetMenu(MenuName = "Rendering/Universal Renderer Data", FileName = "UniversalRendererData")]
public sealed class UniversalRendererData : ScriptableObject
{
	public ScriptableRendererFeature[] Features = Array.Empty<ScriptableRendererFeature>();

	internal ulong GetConfigurationRevision()
	{
		unchecked
		{
			ulong hash = 14695981039346656037UL;
			Mix(ref hash, ValidationRevision);
			ScriptableRendererFeature[] features = Features ?? Array.Empty<ScriptableRendererFeature>();
			Mix(ref hash, (ulong)features.Length);
			foreach (ScriptableRendererFeature? feature in features)
			{
				if (feature == null) { Mix(ref hash, ulong.MaxValue); continue; }
				Mix(ref hash, feature.InstanceId);
				Mix(ref hash, feature.ValidationRevision);
				Mix(ref hash, feature.Enabled ? 1UL : 0UL);
			}
			return hash;
		}
	}

	private static void Mix(ref ulong hash, ulong value)
	{
		unchecked
		{
			for (int index = 0; index < sizeof(ulong); ++index)
			{
				hash ^= (byte)value;
				hash *= 1099511628211UL;
				value >>= 8;
			}
		}
	}
}

public abstract class ScriptableRendererFeature : ScriptableObject
{
	public bool Enabled = true;

	protected abstract RendererFeature CreateFeature();

	internal RendererFeature CreateFeatureInstance()
	{
		RequireValid();
		if (!Enabled) throw new InvalidOperationException($"Renderer feature '{GetType().FullName}' is disabled.");
		RendererFeature instance = CreateFeature() ?? throw new InvalidOperationException(
			$"Renderer feature '{GetType().FullName}' returned no runtime instance.");
		if (instance.IsDisposed) throw new InvalidOperationException("A renderer feature factory returned a disposed instance.");
		return instance;
	}
}

public abstract class RendererFeature : IDisposable
{
	private bool disposed;
	public bool IsDisposed => disposed;

	public virtual void AddRenderPasses(ScriptableRenderer renderer, Camera camera) { }

	public void Dispose()
	{
		if (disposed) return;
		disposed = true;
		try { Dispose(true); }
		finally { GC.SuppressFinalize(this); }
	}

	protected virtual void Dispose(bool disposing) { }
}
