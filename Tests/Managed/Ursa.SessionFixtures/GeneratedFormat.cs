using System;
using Ursa;
using Ursa.Examples;

namespace Ursa.SessionFixtures;

public sealed class GeneratedFormatReader : Behaviour
{
	public string assetGuid = string.Empty;
	public ulong assetLocalId;
	public int validated;
	public override void Awake()
	{
		var reference = new AssetReference(assetGuid, assetLocalId);
		var asset = ScriptableObject.Load<GeneratedFormatAsset>(reference);
		if (asset.label != "Generated dataset" || asset.size != 2 || asset.mesh.GetData().Positions[1].X != 2 ||
			asset.material.Color.G != 1 || asset.texture.Width != 2 || asset.texture.Height != 1 || asset.texture.IsLinear ||
			asset.texture.GetPixels()[0].R != 1 || !ReferenceEquals(asset, ScriptableObject.Load<GeneratedFormatAsset>(reference)))
			throw new InvalidOperationException("Published scripted asset data, types or references differ after runtime loading.");
		++validated;
	}
}
