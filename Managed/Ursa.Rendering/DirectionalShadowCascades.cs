using System;

namespace Ursa.Rendering;

public sealed class DirectionalShadowCascadeSettings
{
	public uint CascadeCount { get; set; } = 2;
	public float ShadowDistance { get; set; } = 50;
	public float[] SplitFractions { get; set; } = new[] { 0.25f };
	public uint AtlasWidth { get; set; } = 2048;
	public uint AtlasHeight { get; set; } = 1024;

	internal void Validate()
	{
		if (CascadeCount is not (1 or 2 or 4)) throw new ArgumentOutOfRangeException(nameof(CascadeCount));
		if (!float.IsFinite(ShadowDistance) || ShadowDistance <= 0) throw new ArgumentOutOfRangeException(nameof(ShadowDistance));
		if (SplitFractions == null || SplitFractions.Length != CascadeCount - 1)
			throw new ArgumentException("The split count must equal cascade count minus one.", nameof(SplitFractions));
		float previous = 0;
		foreach (float split in SplitFractions)
		{
			if (!float.IsFinite(split) || split <= previous || split >= 1)
				throw new ArgumentOutOfRangeException(nameof(SplitFractions), "Split fractions must strictly increase inside (0, 1).");
			previous = split;
		}
		if (AtlasWidth == 0 || AtlasHeight == 0) throw new ArgumentOutOfRangeException(nameof(AtlasWidth));
		uint columns = CascadeCount == 1 ? 1u : 2u;
		uint rows = CascadeCount == 4 ? 2u : 1u;
		if (AtlasWidth % columns != 0 || AtlasHeight % rows != 0 || AtlasWidth / columns != AtlasHeight / rows)
			throw new ArgumentException("The shadow atlas must divide into equal square cascade tiles.");
		if ((ulong)AtlasWidth * AtlasHeight > 64ul * 1024 * 1024)
			throw new ArgumentOutOfRangeException(nameof(AtlasWidth), "The D32 shadow atlas exceeds the 256 MiB render-target budget.");
	}
}

public readonly struct DirectionalShadowCascade
{
	public uint Index { get; }
	public float NearDepth { get; }
	public float FarDepth { get; }
	public float WorldUnitsPerTexel { get; }
	public uint AtlasX { get; }
	public uint AtlasY { get; }
	public uint Resolution { get; }
	public Matrix4x4 LightView { get; }
	public Matrix4x4 LightProjection { get; }
	public Matrix4x4 WorldToShadow { get; }

	internal DirectionalShadowCascade(DirectionalShadowCascadeValue value)
	{
		Index = value.Index;
		NearDepth = value.NearDepth;
		FarDepth = value.FarDepth;
		WorldUnitsPerTexel = value.WorldUnitsPerTexel;
		AtlasX = value.AtlasX;
		AtlasY = value.AtlasY;
		Resolution = value.Resolution;
		LightView = value.LightView;
		LightProjection = value.LightProjection;
		WorldToShadow = value.WorldToShadow;
	}
}
