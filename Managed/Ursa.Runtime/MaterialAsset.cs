namespace Ursa;

public sealed class MaterialAsset
{
	private readonly NativeHandle handle;

	private MaterialAsset(AssetReference reference, NativeHandle handle)
	{
		Reference = reference;
		this.handle = handle;
	}

	public AssetReference Reference { get; }

	public static MaterialAsset Load(AssetReference reference)
	{
		return new MaterialAsset(reference, EngineApi.LoadMaterial(reference.Guid, reference.LocalFileId));
	}

	public bool IsValid => handle.sessionId != 0 && handle.kind == 3;
}
