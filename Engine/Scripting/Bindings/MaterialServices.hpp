#pragma once

#include <BindingTypes.generated.hpp>
#include <Material/Material.hpp>

namespace Ursa::Scripting::Bindings
{
struct MaterialServices
{
	using Handle = Wire::Value_Ursa_NativeHandle;
	using Asset = Wire::Value_Ursa_NativeAssetReference;
	using OptionalAsset = Wire::Value_Ursa_NativeAssetReference_;
	using ColorValue = Wire::Value_Ursa_Color;
	using VectorValue = Wire::Value_Ursa_Vector4;
	using SamplerValue = Wire::Value_Ursa_MaterialSampler;
	static std::shared_ptr<MaterialInstance> Resolve(BindingCall& call, const Handle& material, bool writable = false);
	static std::shared_ptr<const Shader::ShaderAsset> ShaderFor(BindingCall& call,
	                                                            const std::shared_ptr<MaterialInstance>& material);
	static MaterialPropertyValue Property(BindingCall& call, const Handle& material, const Utf8View& name);
	static Result<void> SetProperty(BindingCall& call, const Handle& material, const Utf8View& name,
	                                MaterialPropertyValue value);
	static Result<Handle> Create(BindingCall& call, Int32 type);
	static Result<Handle> Load(BindingCall& call, const Asset& reference);
	static Result<Handle> Clone(BindingCall& call, const Handle& material);
	static Result<void> Destroy(BindingCall& call, const Handle& material);
	static Result<Wire::Value_Ursa_MaterialParametersValue> GetParameters(BindingCall& call, const Handle& material);
	static Result<void> SetBaseColor(BindingCall& call, const Handle& material, const ColorValue& value);
	static Result<void> SetEmissive(BindingCall& call, const Handle& material, const ColorValue& value);
	static Result<void> SetMetallic(BindingCall& call, const Handle& material, Float32 value);
	static Result<void> SetRoughness(BindingCall& call, const Handle& material, Float32 value);
	static Result<void> SetNormalScale(BindingCall& call, const Handle& material, Float32 value);
	static Result<void> SetOcclusionStrength(BindingCall& call, const Handle& material, Float32 value);
	static Result<void> SetAlphaMode(BindingCall& call, const Handle& material, Int32 value);
	static Result<void> SetAlphaCutoff(BindingCall& call, const Handle& material, Float32 value);
	static Result<SamplerValue> GetSampler(BindingCall& call, const Handle& material);
	static Result<void> SetSampler(BindingCall& call, const Handle& material, const SamplerValue& value);
	static Result<OptionalAsset> GetShader(BindingCall& call, const Handle& material);
	static Result<void> SetShader(BindingCall& call, const Handle& material, const Asset& value);
	static Result<OptionalAsset> GetTexture(BindingCall& call, const Handle& material, Int32 slot);
	static Result<void> SetTexture(BindingCall& call, const Handle& material, Int32 slot, const OptionalAsset& value);
	static Result<DataView> GetKeywords(BindingCall& call, const Handle& material);
	static Result<void> SetKeyword(BindingCall& call, const Handle& material, const Utf8View& name, bool enabled);
	static Result<Float32> GetFloat(BindingCall& call, const Handle& material, const Utf8View& name);
	static Result<Int32> GetInt(BindingCall& call, const Handle& material, const Utf8View& name);
	static Result<ColorValue> GetColor(BindingCall& call, const Handle& material, const Utf8View& name);
	static Result<VectorValue> GetVector(BindingCall& call, const Handle& material, const Utf8View& name);
	static Result<OptionalAsset> GetTextureProperty(BindingCall& call, const Handle& material, const Utf8View& name);
	static Result<void> SetFloat(BindingCall& call, const Handle& material, const Utf8View& name, Float32 value);
	static Result<void> SetInt(BindingCall& call, const Handle& material, const Utf8View& name, Int32 value);
	static Result<void> SetColor(BindingCall& call, const Handle& material, const Utf8View& name,
	                             const ColorValue& value);
	static Result<void> SetVector(BindingCall& call, const Handle& material, const Utf8View& name,
	                              const VectorValue& value);
	static Result<void> SetTextureProperty(BindingCall& call, const Handle& material, const Utf8View& name,
	                                       const OptionalAsset& value);
	static Result<void> RemovePropertyOverride(BindingCall& call, const Handle& material, const Utf8View& name);
};
} // namespace Ursa::Scripting::Bindings
