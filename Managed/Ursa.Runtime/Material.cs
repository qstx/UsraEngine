using System;
namespace Ursa;

[Ursa.Bindings.WireValue]
public enum MaterialType { Unlit, PbrMetallicRoughness }

[Ursa.Bindings.WireValue]
public enum MaterialTextureSlot { BaseColor, MetallicRoughness, Normal, Occlusion, Emissive }

[Ursa.Bindings.WireValue]
public enum MaterialPropertyKind : uint { Float, Range, Int, Color, Vector, Texture2D }

[Ursa.Bindings.WireValue]
public struct MaterialPropertyDescriptor
{
	public string Identifier;
	public string DisplayName;
	public MaterialPropertyKind Type;
	public float RangeMinimum;
	public float RangeMaximum;
}

[Ursa.Bindings.WireValue]
public struct MaterialSampler { public bool Linear; public bool Repeat; }

[Ursa.Bindings.WireValue]
public enum AlphaMode { Opaque, Mask, Blend }

public class Material : RenderResource
{
	internal Material(NativeHandle handle) : base(handle) { }

	internal static Material? FromHandle(NativeHandle handle) => Wrap(handle, value => new Material(value));

	public static Material Create(MaterialType type = MaterialType.Unlit) => FromHandle(NativeMaterialBindings.Create(type))!;
	public static Material Create(AssetReference shader) =>
		FromHandle(NativeMaterialBindings.CreateFromShader(NativeAssetReference.From(shader)))!;

	public static Material Load(AssetReference reference) => FromHandle(NativeMaterialBindings.Load(NativeAssetReference.From(reference)))!;

	public Material Clone() => FromHandle(NativeMaterialBindings.Clone(Handle))!;

	public void Destroy() => NativeMaterialBindings.Destroy(Handle);

	public Color Color { get => NativeMaterialBindings.GetParameters(Handle).BaseColor; set => NativeMaterialBindings.SetBaseColor(Handle, value); }
	public Color BaseColor { get => Color; set => Color = value; }
	public Color Emissive { get => NativeMaterialBindings.GetParameters(Handle).Emissive; set => NativeMaterialBindings.SetEmissive(Handle, value); }
	public float Metallic { get => NativeMaterialBindings.GetParameters(Handle).Metallic; set => NativeMaterialBindings.SetMetallic(Handle, value); }
	public float Roughness { get => NativeMaterialBindings.GetParameters(Handle).Roughness; set => NativeMaterialBindings.SetRoughness(Handle, value); }
	public float NormalScale { get => NativeMaterialBindings.GetParameters(Handle).NormalScale; set => NativeMaterialBindings.SetNormalScale(Handle, value); }
	public float OcclusionStrength { get => NativeMaterialBindings.GetParameters(Handle).OcclusionStrength; set => NativeMaterialBindings.SetOcclusionStrength(Handle, value); }
	public float AlphaCutoff { get => NativeMaterialBindings.GetParameters(Handle).AlphaCutoff; set => NativeMaterialBindings.SetAlphaCutoff(Handle, value); }
	public AlphaMode AlphaMode { get => NativeMaterialBindings.GetParameters(Handle).AlphaMode; set => NativeMaterialBindings.SetAlphaMode(Handle, value); }

	public MaterialSampler Sampler
	{
		get => NativeMaterialBindings.GetSampler(Handle);
		set => NativeMaterialBindings.SetSampler(Handle, value);
	}

	public AssetReference? Shader
	{
		get => NativeMaterialBindings.GetShader(Handle)?.ToManaged();
		set => NativeMaterialBindings.SetShader(Handle, NativeAssetReference.From(value ?? throw new ArgumentNullException(nameof(value))));
	}
	public AssetReference? AssetIdentity => NativeMaterialBindings.GetAssetIdentity(Handle)?.ToManaged();
	public MaterialPropertyDescriptor[] ShaderProperties => NativeMaterialBindings.GetPropertyDescriptors(Handle);

	public string[] Keywords => NativeMaterialBindings.GetKeywords(Handle);

	public TextureAsset? GetTexture(MaterialTextureSlot slot)
	{
		var reference = NativeMaterialBindings.GetTexture(Handle, slot);
		return reference.HasValue ? TextureAsset.Load(reference.Value.ToManaged()) : null;
	}

	public void SetTexture(MaterialTextureSlot slot, Texture? texture)
	{
		if (ImportExecution.Active) { SetImportTexture(slot, string.Empty, texture); return; }
		NativeMaterialBindings.SetTexture(Handle, slot, texture is null ? null : NativeAssetReference.From(texture.Reference));
	}

	public float GetFloat(string name) => NativeMaterialBindings.GetFloat(Handle, name);
	public int GetInt(string name) => NativeMaterialBindings.GetInt(Handle, name);
	public Color GetColor(string name) => NativeMaterialBindings.GetColor(Handle, name);
	public Vector4 GetVector(string name) => NativeMaterialBindings.GetVector(Handle, name);

	public TextureAsset? GetTexture(string name)
	{
		var reference = NativeMaterialBindings.GetTextureProperty(Handle, name);
		return reference.HasValue ? TextureAsset.Load(reference.Value.ToManaged()) : null;
	}

	public void SetFloat(string name, float value) => NativeMaterialBindings.SetFloat(Handle, name, value);
	public void SetInt(string name, int value) => NativeMaterialBindings.SetInt(Handle, name, value);
	public void SetColor(string name, Color value) => NativeMaterialBindings.SetColor(Handle, name, value);
	public void SetVector(string name, Vector4 value) => NativeMaterialBindings.SetVector(Handle, name, value);
	public void SetTexture(string name, Texture? texture)
	{
		ArgumentException.ThrowIfNullOrEmpty(name);
		if (ImportExecution.Active) { SetImportTexture(default, name ?? throw new ArgumentNullException(nameof(name)), texture); return; }
		NativeMaterialBindings.SetTextureProperty(Handle, name, texture is null ? null : NativeAssetReference.From(texture.Reference));
	}

	private void SetImportTexture(MaterialTextureSlot slot, string name, Texture? texture)
	{
		NativeHandle candidate = default;
		NativeAssetReference? reference = null;
		if (texture != null)
		{
			if (string.IsNullOrEmpty(texture.Reference.Guid))
			{
				candidate = texture.Handle;
			}
			else reference = NativeAssetReference.From(texture.Reference);
		}
		NativeImportBindings.SetMaterialTexture(Handle, candidate, reference, slot, name);
	}

	public void RemovePropertyOverride(string name) => NativeMaterialBindings.RemovePropertyOverride(Handle, name);

	public void SetKeywordEnabled(string name, bool enabled) => NativeMaterialBindings.SetKeyword(Handle, name, enabled);
}
