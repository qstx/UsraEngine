using System;

namespace Ursa;

public sealed class TextureAsset : Texture
{
	private TextureAsset(AssetReference reference, NativeHandle handle) : base(reference, handle)
	{
	}

	public new static TextureAsset Load(AssetReference reference)
	{
		return new TextureAsset(reference, EngineApi.LoadTexture(reference.Guid, reference.LocalFileId));
	}

}
