using System;
using System.Text.Json;
namespace Ursa;

public enum MaterialType { Unlit, PbrMetallicRoughness }

public enum MaterialTextureSlot { BaseColor, MetallicRoughness, Normal, Occlusion, Emissive }

public struct MaterialSampler { public bool Linear; public bool Repeat; }

public enum AlphaMode { Opaque, Mask, Blend }

public class Material : RenderResource
{
	internal Material(NativeHandle handle) : base(handle) { }

	internal static Material? FromHandle(NativeHandle handle) => Wrap(handle, value => new Material(value));

	public static Material Create(MaterialType type = MaterialType.Unlit) => FromHandle(EngineApi.ResourceRequest<NativeHandle>(new
	{
		operation = "material.create", type = type.ToString()
	}))!;

	public static Material Load(AssetReference reference) => FromHandle(EngineApi.ResourceRequest<NativeHandle>(new
	{
		operation = "material.load", guid = reference.Guid, localFileId = reference.LocalFileId
	}))!;

	public Material Clone() => FromHandle(EngineApi.ResourceRequest<NativeHandle>(new { operation = "material.clone", handle = Handle }))!;

	public void Destroy() => EngineApi.ResourceRequest(new { operation = "material.destroy", handle = Handle });

	private T Get<T>(string name) => EngineApi.ResourceRequest(new { operation = "material.get", handle = Handle }).GetProperty(name).Deserialize<T>(EngineApi.ResourceJson)!;

	private void Set(string operation, object data) => EngineApi.ResourceRequest(new { operation, handle = Handle, data });

	public Color Color { get => Get<Color>("baseColor"); set => Set("material.color", value); }
	public Color BaseColor { get => Color; set => Color = value; }
	public Color Emissive { get => Get<Color>("emissive"); set => Set("material.emissive", value); }
	public float Metallic { get => Get<float>("metallic"); set => Set("material.metallic", value); }
	public float Roughness { get => Get<float>("roughness"); set => Set("material.roughness", value); }
	public float NormalScale { get => Get<float>("normalScale"); set => Set("material.normalScale", value); }
	public float OcclusionStrength { get => Get<float>("occlusionStrength"); set => Set("material.occlusionStrength", value); }
	public float AlphaCutoff { get => Get<float>("alphaCutoff"); set => Set("material.alphaCutoff", value); }
	public AlphaMode AlphaMode { get => Get<AlphaMode>("alphaMode"); set => Set("material.alphaMode", value); }

	public MaterialSampler Sampler
	{
		get => EngineApi.ResourceRequest<MaterialSampler>(new { operation = "material.sampler.get", handle = Handle });
		set => Set("material.sampler.set", value);
	}

	public AssetReference? Shader
	{
		get => EngineApi.ResourceRequest<AssetReference?>(new { operation = "material.shader.get", handle = Handle });
		set => Set("material.shader.set", value ?? throw new ArgumentNullException(nameof(value)));
	}

	public string[] Keywords => EngineApi.ResourceRequest<string[]>(new { operation = "material.keywords", handle = Handle });

	public TextureAsset? GetTexture(MaterialTextureSlot slot)
	{
		var reference = EngineApi.ResourceRequest<AssetReference?>(new { operation = "material.texture.get", handle = Handle, slot = (uint)slot });
		return reference.HasValue ? TextureAsset.Load(reference.Value) : null;
	}

	public void SetTexture(MaterialTextureSlot slot, TextureAsset? texture) => EngineApi.ResourceRequest(new
	{
		operation = "material.texture.set", handle = Handle, slot = (uint)slot, data = texture?.Reference
	});

	private T GetProperty<T>(string name) => EngineApi.ResourceRequest<T>(new { operation = "material.property.get", handle = Handle, name });

	private void SetProperty(string name, string type, object? data) => EngineApi.ResourceRequest(new { operation = "material.property.set", handle = Handle, name, type, data });

	public float GetFloat(string name) => GetProperty<float>(name);
	public int GetInt(string name) => GetProperty<int>(name);
	public Color GetColor(string name) => GetProperty<Color>(name);
	public Vector4 GetVector(string name) => GetProperty<Vector4>(name);

	public TextureAsset? GetTexture(string name)
	{
		var reference = GetProperty<AssetReference?>(name);
		return reference.HasValue ? TextureAsset.Load(reference.Value) : null;
	}

	public void SetFloat(string name, float value) => SetProperty(name, "Float", value);
	public void SetInt(string name, int value) => SetProperty(name, "Int", value);
	public void SetColor(string name, Color value) => SetProperty(name, "Color", value);
	public void SetVector(string name, Vector4 value) => SetProperty(name, "Vector", value);
	public void SetTexture(string name, TextureAsset? texture) => SetProperty(name, "Texture2D", texture?.Reference);

	public void RemovePropertyOverride(string name) => EngineApi.ResourceRequest(new { operation = "material.property.remove", handle = Handle, name });

	public void SetKeywordEnabled(string name, bool enabled) => EngineApi.ResourceRequest(new
	{
		operation = "material.keyword", handle = Handle, name, data = enabled
	});
}
