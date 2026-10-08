using System;

namespace Ursa;

public sealed class TextureAsset
{
	private readonly NativeHandle handle;

	private TextureAsset(AssetReference reference, NativeHandle handle)
	{
		Reference = reference;
		this.handle = handle;
	}

	public AssetReference Reference { get; }

	public static TextureAsset Load(AssetReference reference)
	{
		return new TextureAsset(reference, EngineApi.LoadTexture(reference.Guid, reference.LocalFileId));
	}

	public bool IsValid => handle.sessionId != 0 && handle.kind == 3;
}
