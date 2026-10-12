using System.Text.Json;

namespace Ursa;

// Compatibility loading adapter; editing and binding use the common Material API.
public sealed class MaterialAsset : Material
{
	private MaterialAsset(AssetReference reference, NativeHandle handle) : base(handle)
	{
		Reference = reference;
	}

	internal static MaterialAsset FromBinding(JsonElement binding, NativeHandle handle)
	{
		var reference = binding.GetProperty("kind").GetString() == "asset" ?
			binding.GetProperty("asset").Deserialize<AssetReference>(EngineApi.ResourceJson) : default;
		return new MaterialAsset(reference, handle);
	}

	public AssetReference Reference { get; }

	public new static MaterialAsset Load(AssetReference reference)
	{
		return new MaterialAsset(reference, EngineApi.ResourceRequest<NativeHandle>(new
		{
			operation = "material.load", guid = reference.Guid, localFileId = reference.LocalFileId
		}));
	}
}
