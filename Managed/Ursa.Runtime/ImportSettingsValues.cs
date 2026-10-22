using Ursa.Bindings;

namespace Ursa;

[WireValue]
internal struct TextureImporterSettingsValue
{
	public uint Type;
	public bool SRgb;
	public bool Alpha;
	public bool GenerateMipmaps;
	public uint Filter;
	public uint Wrap;
	public uint MaxDimension;
}

[WireValue]
internal struct ModelImporterSettingsValue
{
	public float ScaleFactor;
	public bool GenerateNormals;
	public bool GenerateTangents;
	public bool FlipUvs;
	public bool ImportMaterials;
	public bool ImportTextures;
	public uint MaxBoneInfluences;
}
