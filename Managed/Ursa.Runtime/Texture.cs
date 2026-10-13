using System;

namespace Ursa;

public class Texture : RenderResource
{
	internal Texture(AssetReference reference, NativeHandle handle) : base(handle)
	{
		Reference = reference;
	}

	public AssetReference Reference { get; }

	public static Texture Load(AssetReference reference) => new(reference,
		EngineApi.LoadTexture(reference.Guid, reference.LocalFileId));
}
