using System;

namespace Ursa;

public class Texture : RenderResource
{
	internal Texture(AssetReference reference, NativeHandle handle) : base(handle)
	{
		Reference = reference;
	}

	public AssetReference Reference { get; private set; }
	internal void BindImportReference(AssetReference reference)
	{
		ImportExecution.RequireActive();
		if (this is not Texture2D || !IsValid || reference.LocalFileId == 0 ||
			(!string.IsNullOrEmpty(Reference.Guid) && !Reference.Equals(reference)))
			throw new InvalidOperationException("Import texture cannot change its output identity.");
		Reference = reference;
	}

	public static Texture Load(AssetReference reference) => new(reference,
		EngineApi.LoadTexture(reference.Guid, reference.LocalFileId));
}
