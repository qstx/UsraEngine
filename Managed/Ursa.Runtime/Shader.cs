using System;

namespace Ursa;

// Persistent configuration reference. Imported program resolution belongs to the rendering context.
public sealed class Shader
{
	private Shader(AssetReference reference) { Reference = reference; }

	public AssetReference Reference { get; }

	public static Shader FromReference(AssetReference reference)
	{
		if (string.IsNullOrEmpty(reference.Guid) || reference.Guid == Guid.Empty.ToString("D"))
			throw new ArgumentException("Shader reference requires a nonempty asset GUID.", nameof(reference));
		return new Shader(reference);
	}
}
