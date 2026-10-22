#include <Scripting/Bindings/MaterialServices.hpp>
#include <Scripting/Bindings/RuntimeServices.hpp>
#include <Scripting/Bindings/BindingAssets.hpp>
#include <Scripting/Bindings/BindingCollections.hpp>
#include <Scripting/Bindings/BindingHandles.hpp>
#include <Scripting/Bindings/BindingMath.hpp>
#include <Scripting/ManagedSceneApi.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Core/Memory/MemoryFactories.hpp>

namespace Ursa::Scripting::Bindings
{
std::shared_ptr<MaterialInstance> MaterialServices::Resolve(BindingCall& call, const Handle& material, bool writable)
{
	auto& api = RuntimeServices::SceneApi(call);
	if (writable)
	{
		api.RequireWritableResource(ReadManagedHandle(material));
	}
	return TakeBindingResult(api.ResolveMaterial(ReadManagedHandle(material)));
}

std::shared_ptr<const Shader::ShaderAsset>
MaterialServices::ShaderFor(BindingCall& call, const std::shared_ptr<MaterialInstance>& material)
{
	auto shader = material->Shader();
	auto& api = RuntimeServices::SceneApi(call);
	if (!shader && api.Resources())
	{
		shader = TakeBindingResult(api.Resources()->LoadShader(material->ShaderId()));
	}
	if (!shader)
	{
		throw std::runtime_error("Material property access requires Shader metadata");
	}
	return shader;
}

MaterialPropertyValue MaterialServices::Property(BindingCall& call, const Handle& material, const Utf8View& name)
{
	auto target = Resolve(call, material);
	return TakeBindingResult(target->PropertyValue(ReadString(name), *ShaderFor(call, target)));
}

Result<void> MaterialServices::SetProperty(BindingCall& call, const Handle& material, const Utf8View& name,
                                           MaterialPropertyValue value)
{
	auto target = Resolve(call, material, true);
	return target->SetPropertyValue(ReadString(name), *ShaderFor(call, target), std::move(value));
}

Result<MaterialServices::Handle> MaterialServices::Create(BindingCall& call, Int32 type)
{
	if (type < 0 || type > 1)
	{
		return Result<Handle>::Failure("Unknown Material type");
	}
	auto& api = RuntimeServices::SceneApi(call);
	auto material = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets,
	                                             type == 0 ? MaterialType::Unlit : MaterialType::PbrMetallicRoughness);
	const auto shaderId = type == 0 ? Shader::BuiltinUnlitShaderId() : Shader::BuiltinPbrShaderId();
	CompleteBindingResult(material->MigrateLegacyToShader(shaderId));
	auto shader = !api.CpuBuiltinShaders() && api.Resources()
	                  ? TakeBindingResult(api.Resources()->LoadShader(shaderId))
	                  : MakeShared<const Shader::ShaderAsset>(Memory::MemoryTag::Assets,
	                                                          type == 0 ? Shader::CreateBuiltinUnlitShaderAsset()
	                                                                    : Shader::CreateBuiltinPbrShaderAsset());
	CompleteBindingResult(material->SetShaderAsset(std::move(shader)));
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMaterial(material, true))));
}

Result<MaterialServices::Handle> MaterialServices::Load(BindingCall& call, const Asset& reference)
{
	auto& api = RuntimeServices::SceneApi(call);
	if (!api.Resources())
	{
		return Result<Handle>::Failure("Material loading requires a ResourceSession");
	}
	const auto asset = ReadAssetValue(reference);
	return Result<Handle>::Success(WriteManagedHandle<Handle>(TakeBindingResult(
	    api.RegisterMaterial(TakeBindingResult(api.Resources()->LoadMaterial(asset.guid, asset.localFileId))))));
}

Result<MaterialServices::Handle> MaterialServices::Clone(BindingCall& call, const Handle& material)
{
	auto& api = RuntimeServices::SceneApi(call);
	return Result<Handle>::Success(
	    WriteManagedHandle<Handle>(TakeBindingResult(api.RegisterMaterial(Resolve(call, material)->Clone(), true))));
}

Result<void> MaterialServices::Destroy(BindingCall& call, const Handle& material)
{
	auto& api = RuntimeServices::SceneApi(call);
	CompleteBindingResult(Resolve(call, material, true)->Destroy());
	CompleteBindingResult(api.handles->Invalidate(ReadManagedHandle(material)));
	api.ownedMaterials.erase(material.F_index);
	return {};
}

Result<Wire::Value_Ursa_MaterialParametersValue> MaterialServices::GetParameters(BindingCall& call,
                                                                                 const Handle& material)
{
	const auto target = Resolve(call, material);
	const auto& source = target->Parameters();
	Wire::Value_Ursa_MaterialParametersValue value;
	value.F_BaseColor = WriteColor<ColorValue>(source.baseColor);
	value.F_Emissive = WriteColor<ColorValue>(source.emissive);
	value.F_Metallic = source.metallic;
	value.F_Roughness = source.roughness;
	value.F_NormalScale = source.normalScale;
	value.F_OcclusionStrength = source.occlusionStrength;
	value.F_AlphaCutoff = source.alphaCutoff;
	value.F_AlphaMode = Int32(source.alphaMode);
	return Result<Wire::Value_Ursa_MaterialParametersValue>::Success(value);
}

Result<void> MaterialServices::SetBaseColor(BindingCall& call, const Handle& material, const ColorValue& value)
{
	return Resolve(call, material, true)->SetBaseColor(ReadColor(value));
}

Result<void> MaterialServices::SetEmissive(BindingCall& call, const Handle& material, const ColorValue& value)
{
	return Resolve(call, material, true)->SetEmissive(ReadColor(value));
}

Result<void> MaterialServices::SetMetallic(BindingCall& call, const Handle& material, Float32 value)
{
	return Resolve(call, material, true)->SetMetallic(value);
}

Result<void> MaterialServices::SetRoughness(BindingCall& call, const Handle& material, Float32 value)
{
	return Resolve(call, material, true)->SetRoughness(value);
}

Result<void> MaterialServices::SetNormalScale(BindingCall& call, const Handle& material, Float32 value)
{
	return Resolve(call, material, true)->SetNormalScale(value);
}

Result<void> MaterialServices::SetOcclusionStrength(BindingCall& call, const Handle& material, Float32 value)
{
	return Resolve(call, material, true)->SetOcclusionStrength(value);
}

Result<void> MaterialServices::SetAlphaMode(BindingCall& call, const Handle& material, Int32 value)
{
	if (value < 0 || value > 2)
	{
		return Result<void>::Failure("Unknown Material alpha mode");
	}
	auto target = Resolve(call, material, true);
	return target->SetAlphaMode(AlphaMode(value), target->Parameters().alphaCutoff);
}

Result<void> MaterialServices::SetAlphaCutoff(BindingCall& call, const Handle& material, Float32 value)
{
	auto target = Resolve(call, material, true);
	return target->SetAlphaMode(target->Parameters().alphaMode, value);
}

Result<MaterialServices::SamplerValue> MaterialServices::GetSampler(BindingCall& call, const Handle& material)
{
	const auto source = Resolve(call, material)->Parameters().sampler;
	return Result<SamplerValue>::Success({source.linear ? 1u : 0u, source.repeat ? 1u : 0u});
}

Result<void> MaterialServices::SetSampler(BindingCall& call, const Handle& material, const SamplerValue& value)
{
	return Resolve(call, material, true)->SetSampler({value.F_Linear != 0, value.F_Repeat != 0});
}

Result<MaterialServices::OptionalAsset> MaterialServices::GetShader(BindingCall& call, const Handle& material)
{
	return Result<OptionalAsset>::Success(
	    WriteOptionalAssetValue<OptionalAsset>(call.arena, {Resolve(call, material)->ShaderId(), 0}));
}

Result<void> MaterialServices::SetShader(BindingCall& call, const Handle& material, const Asset& value)
{
	auto& api = RuntimeServices::SceneApi(call);
	const auto asset = ReadAssetValue(value);
	if (!api.Resources() || asset.localFileId)
	{
		return Result<void>::Failure("Shader assignment requires a ResourceSession and root Shader asset");
	}
	return Resolve(call, material, true)->SetShaderAsset(TakeBindingResult(api.Resources()->LoadShader(asset.guid)));
}

Result<MaterialServices::OptionalAsset> MaterialServices::GetTexture(BindingCall& call, const Handle& material,
                                                                     Int32 slot)
{
	if (slot < 0 || slot > 4)
	{
		return Result<OptionalAsset>::Failure("Unknown Material texture slot");
	}
	return Result<OptionalAsset>::Success(WriteOptionalAssetValue<OptionalAsset>(
	    call.arena, Resolve(call, material)->TextureReference(MaterialTextureSlot(slot))));
}

Result<void> MaterialServices::SetTexture(BindingCall& call, const Handle& material, Int32 slot,
                                          const OptionalAsset& value)
{
	if (slot < 0 || slot > 4)
	{
		return Result<void>::Failure("Unknown Material texture slot");
	}
	auto& api = RuntimeServices::SceneApi(call);
	AssetReference reference;
	MaterialTexturePtr texture;
	if (value.present)
	{
		if (!api.Resources())
		{
			return Result<void>::Failure("Texture assignment requires a ResourceSession");
		}
		reference = ReadAssetValue(value.F_value);
		texture = TakeBindingResult(api.Resources()->LoadTexture(reference.guid, reference.localFileId));
	}
	return Resolve(call, material, true)->SetTextureReference(MaterialTextureSlot(slot), reference, std::move(texture));
}

Result<DataView> MaterialServices::GetKeywords(BindingCall& call, const Handle& material)
{
	return Result<DataView>::Success(WriteArray<Utf8View>(call.arena, Resolve(call, material)->Keywords(),
	                                                      [&](const String& v) { return call.arena.StringValue(v); }));
}

Result<void> MaterialServices::SetKeyword(BindingCall& call, const Handle& material, const Utf8View& name, bool enabled)
{
	return Resolve(call, material, true)->SetKeywordEnabled(ReadString(name), enabled);
}

Result<Float32> MaterialServices::GetFloat(BindingCall& call, const Handle& material, const Utf8View& name)
{
	const auto value = Property(call, material, name);
	if (value.type != Shader::PropertyType::Float && value.type != Shader::PropertyType::Range)
	{
		return Result<Float32>::Failure("Shader property is not Float/Range");
	}
	return Result<Float32>::Success(value.floatValue);
}

Result<Int32> MaterialServices::GetInt(BindingCall& call, const Handle& material, const Utf8View& name)
{
	const auto value = Property(call, material, name);
	if (value.type != Shader::PropertyType::Int)
	{
		return Result<Int32>::Failure("Shader property is not Int");
	}
	return Result<Int32>::Success(value.intValue);
}

Result<MaterialServices::ColorValue> MaterialServices::GetColor(BindingCall& call, const Handle& material,
                                                                const Utf8View& name)
{
	const auto value = Property(call, material, name);
	if (value.type != Shader::PropertyType::Color)
	{
		return Result<ColorValue>::Failure("Shader property is not Color");
	}
	return Result<ColorValue>::Success(
	    {value.vectorValue[0], value.vectorValue[1], value.vectorValue[2], value.vectorValue[3]});
}

Result<MaterialServices::VectorValue> MaterialServices::GetVector(BindingCall& call, const Handle& material,
                                                                  const Utf8View& name)
{
	const auto value = Property(call, material, name);
	if (value.type != Shader::PropertyType::Vector)
	{
		return Result<VectorValue>::Failure("Shader property is not Vector");
	}
	return Result<VectorValue>::Success(
	    {value.vectorValue[0], value.vectorValue[1], value.vectorValue[2], value.vectorValue[3]});
}

Result<MaterialServices::OptionalAsset> MaterialServices::GetTextureProperty(BindingCall& call, const Handle& material,
                                                                             const Utf8View& name)
{
	const auto value = Property(call, material, name);
	if (value.type != Shader::PropertyType::Texture2D)
	{
		return Result<OptionalAsset>::Failure("Shader property is not Texture2D");
	}
	return Result<OptionalAsset>::Success(
	    WriteOptionalAssetValue<OptionalAsset>(call.arena, {value.textureId, value.textureLocalFileId}));
}

Result<void> MaterialServices::SetFloat(BindingCall& call, const Handle& material, const Utf8View& name, Float32 value)
{
	auto current = Property(call, material, name);
	if (current.type != Shader::PropertyType::Float && current.type != Shader::PropertyType::Range)
	{
		return Result<void>::Failure("Shader property is not Float/Range");
	}
	current.floatValue = value;
	return SetProperty(call, material, name, std::move(current));
}

Result<void> MaterialServices::SetInt(BindingCall& call, const Handle& material, const Utf8View& name, Int32 value)
{
	auto current = Property(call, material, name);
	if (current.type != Shader::PropertyType::Int)
	{
		return Result<void>::Failure("Shader property is not Int");
	}
	current.intValue = value;
	return SetProperty(call, material, name, std::move(current));
}

Result<void> MaterialServices::SetColor(BindingCall& call, const Handle& material, const Utf8View& name,
                                        const ColorValue& value)
{
	auto current = Property(call, material, name);
	if (current.type != Shader::PropertyType::Color)
	{
		return Result<void>::Failure("Shader property is not Color");
	}
	current.vectorValue = {value.F_R, value.F_G, value.F_B, value.F_A};
	return SetProperty(call, material, name, std::move(current));
}

Result<void> MaterialServices::SetVector(BindingCall& call, const Handle& material, const Utf8View& name,
                                         const VectorValue& value)
{
	auto current = Property(call, material, name);
	if (current.type != Shader::PropertyType::Vector)
	{
		return Result<void>::Failure("Shader property is not Vector");
	}
	current.vectorValue = {value.F_X, value.F_Y, value.F_Z, value.F_W};
	return SetProperty(call, material, name, std::move(current));
}

Result<void> MaterialServices::SetTextureProperty(BindingCall& call, const Handle& material, const Utf8View& name,
                                                  const OptionalAsset& value)
{
	auto current = Property(call, material, name);
	if (current.type != Shader::PropertyType::Texture2D)
	{
		return Result<void>::Failure("Shader property is not Texture2D");
	}
	MaterialPropertyValue texture;
	texture.type = current.type;
	if (value.present)
	{
		auto& api = RuntimeServices::SceneApi(call);
		if (!api.Resources())
		{
			return Result<void>::Failure("Texture property assignment requires a ResourceSession");
		}
		const auto reference = ReadAssetValue(value.F_value);
		texture.textureId = reference.guid;
		texture.textureLocalFileId = reference.localFileId;
		texture.texture = TakeBindingResult(api.Resources()->LoadTexture(reference.guid, reference.localFileId));
	}
	else
	{
		return Resolve(call, material, true)->RemovePropertyOverride(ReadString(name));
	}
	return SetProperty(call, material, name, std::move(texture));
}

Result<void> MaterialServices::RemovePropertyOverride(BindingCall& call, const Handle& material, const Utf8View& name)
{
	Property(call, material, name);
	return Resolve(call, material, true)->RemovePropertyOverride(ReadString(name));
}
} // namespace Ursa::Scripting::Bindings
