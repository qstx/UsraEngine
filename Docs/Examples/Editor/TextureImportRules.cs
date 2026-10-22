using System;
using Ursa;
using Ursa.Editor;

namespace Ursa.Examples;

public sealed class TextureImportRules : AssetPostprocessor
{
	public override uint GetVersion() => 1;
	public override int GetPostprocessOrder() => 100;
	public override void OnPreprocessTexture()
	{
		if (!AssetPath.StartsWith("prj:GeneratedTextures/", StringComparison.Ordinal)) return;
		var importer = (TextureImporter)AssetImporter;
		importer.MaxDimension = 1024;
		importer.GenerateMipmaps = true;
		importer.WrapMode = TextureWrapMode.Clamp;
		if (AssetPath.EndsWith(".mask.png", StringComparison.Ordinal)) importer.TextureType = TextureImportType.LinearData;
	}
	public override void OnPostprocessTexture(Texture2D texture)
	{
		if (!AssetPath.StartsWith("prj:GeneratedTextures/", StringComparison.Ordinal)) return;
		Color[] pixels = texture.GetPixels();
		for (int index = 0; index < pixels.Length; ++index) pixels[index].A = Math.Clamp(pixels[index].A, 0, 1);
		texture.SetPixels(pixels);
	}
}
