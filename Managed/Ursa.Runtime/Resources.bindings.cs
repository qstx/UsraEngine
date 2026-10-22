using Ursa.Bindings;

namespace Ursa;

[BindingModule("Runtime")]
[NativeHeader("Scripting/Bindings/MaterialServices.hpp")]
internal static partial class NativeMaterialBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::Create")]
	internal static partial NativeHandle Create(MaterialType type);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::Load")]
	internal static partial NativeHandle Load(NativeAssetReference reference);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::Clone")]
	internal static partial NativeHandle Clone([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::Destroy")]
	internal static partial void Destroy([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetParameters")]
	internal static partial MaterialParametersValue GetParameters([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetBaseColor")]
	internal static partial void SetBaseColor([HandleKind(5)] NativeHandle material, Color value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetEmissive")]
	internal static partial void SetEmissive([HandleKind(5)] NativeHandle material, Color value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetMetallic")]
	internal static partial void SetMetallic([HandleKind(5)] NativeHandle material, float value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetRoughness")]
	internal static partial void SetRoughness([HandleKind(5)] NativeHandle material, float value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetNormalScale")]
	internal static partial void SetNormalScale([HandleKind(5)] NativeHandle material, float value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetOcclusionStrength")]
	internal static partial void SetOcclusionStrength([HandleKind(5)] NativeHandle material, float value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetAlphaMode")]
	internal static partial void SetAlphaMode([HandleKind(5)] NativeHandle material, AlphaMode value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetAlphaCutoff")]
	internal static partial void SetAlphaCutoff([HandleKind(5)] NativeHandle material, float value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetSampler")]
	internal static partial MaterialSampler GetSampler([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetSampler")]
	internal static partial void SetSampler([HandleKind(5)] NativeHandle material, MaterialSampler value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetShader")]
	internal static partial NativeAssetReference? GetShader([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetShader")]
	internal static partial void SetShader([HandleKind(5)] NativeHandle material, NativeAssetReference value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetTexture")]
	internal static partial NativeAssetReference? GetTexture([HandleKind(5)] NativeHandle material, MaterialTextureSlot slot);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetTexture")]
	internal static partial void SetTexture([HandleKind(5)] NativeHandle material, MaterialTextureSlot slot, NativeAssetReference? value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetKeywords")]
	internal static partial string[] GetKeywords([HandleKind(5)] NativeHandle material);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetKeyword")]
	internal static partial void SetKeyword([HandleKind(5)] NativeHandle material, string name, bool enabled);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetFloat")]
	internal static partial float GetFloat([HandleKind(5)] NativeHandle material, string name);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetInt")]
	internal static partial int GetInt([HandleKind(5)] NativeHandle material, string name);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetColor")]
	internal static partial Color GetColor([HandleKind(5)] NativeHandle material, string name);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetVector")]
	internal static partial Vector4 GetVector([HandleKind(5)] NativeHandle material, string name);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::GetTextureProperty")]
	internal static partial NativeAssetReference? GetTextureProperty([HandleKind(5)] NativeHandle material, string name);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetFloat")]
	internal static partial void SetFloat([HandleKind(5)] NativeHandle material, string name, float value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetInt")]
	internal static partial void SetInt([HandleKind(5)] NativeHandle material, string name, int value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetColor")]
	internal static partial void SetColor([HandleKind(5)] NativeHandle material, string name, Color value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetVector")]
	internal static partial void SetVector([HandleKind(5)] NativeHandle material, string name, Vector4 value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::SetTextureProperty")]
	internal static partial void SetTextureProperty([HandleKind(5)] NativeHandle material, string name, NativeAssetReference? value);
	[NativeMethod("Ursa::Scripting::Bindings::MaterialServices::RemovePropertyOverride")]
	internal static partial void RemovePropertyOverride([HandleKind(5)] NativeHandle material, string name);
}

[BindingModule("Runtime")]
[NativeHeader("Scripting/Bindings/ResourceServices.hpp")]
internal static partial class NativeResourceBindings
{
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::LoadTexture")]
	internal static partial NativeHandle LoadTexture(NativeAssetReference reference);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::GetTextureInfo")]
	internal static partial Texture2DInfo GetTextureInfo([HandleKind(3)] NativeHandle texture);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::GetTexturePixels")]
	internal static partial Color[] GetTexturePixels([HandleKind(3)] NativeHandle texture);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::IsValid")]
	internal static partial bool IsValid(NativeHandle resource);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::GetMeshBinding")]
	internal static partial NativeHandle? GetMeshBinding([HandleKind(2)] NativeHandle component, bool instance);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::SetMeshBinding")]
	internal static partial void SetMeshBinding([HandleKind(2)] NativeHandle component, NativeHandle? mesh, bool instance);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::GetMaterialBindings")]
	internal static partial NativeHandle?[] GetMaterialBindings([HandleKind(2)] NativeHandle component, bool instance, bool first);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::SetMaterialBindings")]
	internal static partial void SetMaterialBindings([HandleKind(2)] NativeHandle component, NativeHandle?[] materials, bool shared, bool first);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::FindComputeKernel")]
	internal static partial int FindComputeKernel(NativeAssetReference reference, string name);
	[NativeMethod("Ursa::Scripting::Bindings::ResourceServices::GetComputeThreadGroupSize")]
	internal static partial ComputeThreadGroupSize GetComputeThreadGroupSize(NativeAssetReference reference, int kernel);
}
