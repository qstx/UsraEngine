#include <Core/Types.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Material/Material.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace Ursa
{
namespace
{
const Shader::PropertyDeclaration* FindProperty(const Shader::ShaderAsset& shader, const String& identifier)
{
	for (const Shader::PropertyDeclaration& property : shader.properties)
	{
		if (property.identifier == identifier)
		{
			return &property;
		}
	}
	return nullptr;
}

bool HasPropertyAttribute(const Shader::PropertyDeclaration& property, const String& attribute)
{
	return std::find(property.attributes.begin(), property.attributes.end(), attribute) != property.attributes.end();
}

bool unit(Ursa::Float32 value)
{
	return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool Nonnegative(Ursa::Float32 value)
{
	return std::isfinite(value) && value >= 0.0f;
}

bool ValidColor(Color color, bool hdr)
{
	const auto valid = hdr ? Nonnegative : unit;
	return valid(color.red) && valid(color.green) && valid(color.blue) && unit(color.alpha);
}

Ursa::Size SlotIndex(MaterialTextureSlot slot)
{
	return static_cast<Ursa::Size>(slot);
}

bool ValidSlot(MaterialTextureSlot slot)
{
	return SlotIndex(slot) < 5;
}

TextureColorSpace RequiredColorSpace(MaterialTextureSlot slot)
{
	return slot == MaterialTextureSlot::BaseColor || slot == MaterialTextureSlot::Emissive ? TextureColorSpace::Srgb
	                                                                                       : TextureColorSpace::Linear;
}

String BuiltinTextureProperty(ResourceId shaderId, MaterialTextureSlot slot)
{
	if (shaderId == Shader::BuiltinUnlitShaderId())
	{
		return slot == MaterialTextureSlot::BaseColor ? "_MainTex" : String{};
	}
	if (shaderId != Shader::BuiltinPbrShaderId())
	{
		return {};
	}
	switch (slot)
	{
		case MaterialTextureSlot::BaseColor:
			return "_BaseMap";
		case MaterialTextureSlot::MetallicRoughness:
			return "_MetallicRoughnessMap";
		case MaterialTextureSlot::Normal:
			return "_NormalMap";
		case MaterialTextureSlot::Occlusion:
			return "_OcclusionMap";
		case MaterialTextureSlot::Emissive:
			return "_EmissiveMap";
	}
	return {};
}
} // namespace

MaterialPropertyValue MaterialPropertyValue::Float(Ursa::Float32 value)
{
	MaterialPropertyValue result;
	result.type = Shader::PropertyType::Float;
	result.floatValue = value;
	return result;
}

MaterialPropertyValue MaterialPropertyValue::Range(Ursa::Float32 value)
{
	MaterialPropertyValue result;
	result.type = Shader::PropertyType::Range;
	result.floatValue = value;
	return result;
}

MaterialPropertyValue MaterialPropertyValue::Int(Ursa::Int32 value)
{
	MaterialPropertyValue result;
	result.type = Shader::PropertyType::Int;
	result.intValue = value;
	return result;
}

MaterialPropertyValue MaterialPropertyValue::ColorValue(Color value)
{
	MaterialPropertyValue result;
	result.type = Shader::PropertyType::Color;
	result.vectorValue = {value.red, value.green, value.blue, value.alpha};
	return result;
}

MaterialPropertyValue MaterialPropertyValue::VectorValue(Array<Ursa::Float32, 4> value)
{
	MaterialPropertyValue result;
	result.type = Shader::PropertyType::Vector;
	result.vectorValue = std::move(value);
	return result;
}

MaterialPropertyValue MaterialPropertyValue::Texture2D(ResourceId id, MaterialTexturePtr value)
{
	MaterialPropertyValue result;
	result.type = Shader::PropertyType::Texture2D;
	result.textureId = id;
	result.texture = std::move(value);
	return result;
}

MaterialTexture::MaterialTexture(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight,
                                 TextureColorSpace textureColorSpace, Vector<Ursa::UInt8> texturePixels,
                                 Vector<TextureMip> textureMipmaps, MaterialSampler textureSampler)
    : width(textureWidth), height(textureHeight), colorSpace(textureColorSpace), pixels(std::move(texturePixels)),
      mipmaps(std::move(textureMipmaps)), sampler(textureSampler)
{
}

Result<MaterialTexturePtr> MaterialTexture::Create(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight,
                                                   TextureColorSpace textureColorSpace, Vector<Ursa::UInt8> rgba8,
                                                   Vector<TextureMip> textureMipmaps, MaterialSampler textureSampler)
{
	if (!textureWidth || !textureHeight ||
	    static_cast<Ursa::UInt64>(textureWidth) * textureHeight > std::numeric_limits<Ursa::Size>::max() / 4 ||
	    rgba8.size() != static_cast<Ursa::Size>(textureWidth) * textureHeight * 4)
	{
		return Result<MaterialTexturePtr>::Failure("Material texture dimensions or RGBA8 pixel byte count are invalid");
	}
	if (textureColorSpace != TextureColorSpace::Linear && textureColorSpace != TextureColorSpace::Srgb)
	{
		return Result<MaterialTexturePtr>::Failure("Material texture color space is invalid");
	}
	Ursa::UInt32 mipWidth = textureWidth;
	Ursa::UInt32 mipHeight = textureHeight;
	Ursa::UInt64 totalBytes = rgba8.size();
	for (const TextureMip& mipmap : textureMipmaps)
	{
		if (mipWidth == 1 && mipHeight == 1)
		{
			return Result<MaterialTexturePtr>::Failure("Material texture mip chain exceeds its dimensions");
		}
		mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
		const Ursa::UInt64 expectedBytes = static_cast<Ursa::UInt64>(mipWidth) * mipHeight * 4;
		if (mipmap.width != mipWidth || mipmap.height != mipHeight || mipmap.pixels.size() != expectedBytes)
		{
			return Result<MaterialTexturePtr>::Failure("Material texture mip dimensions or pixel bytes are invalid");
		}
		totalBytes += expectedBytes;
		if (totalBytes > 256ull * 1024 * 1024)
		{
			return Result<MaterialTexturePtr>::Failure("Material texture mip chain exceeds the size limit");
		}
	}
	return Result<MaterialTexturePtr>::Success(MaterialTexturePtr(new MaterialTexture(
	    textureWidth, textureHeight, textureColorSpace, std::move(rgba8), std::move(textureMipmaps), textureSampler)));
}

MaterialInstance::MaterialInstance(MaterialType materialType) : type(materialType)
{
}

Result<void> MaterialInstance::SetShader(ResourceId id)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!id)
	{
		return Result<void>::Failure("Shader-backed material requires a nonzero Shader resource ID");
	}
	if (shaderId == id)
	{
		return {};
	}
	if (id == Shader::BuiltinUnlitShaderId() || id == Shader::BuiltinPbrShaderId())
	{
		return MigrateLegacyToShader(id);
	}
	shaderId = id;
	shaderAsset.reset();
	++version;
	return {};
}

Result<void> MaterialInstance::SetShaderAsset(std::shared_ptr<const Shader::ShaderAsset> asset)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!asset || !asset->id || (shaderId && asset->id != shaderId))
	{
		return Result<void>::Failure("Shader asset ID does not match the material Shader reference");
	}
	if (!shaderId)
	{
		shaderId = asset->id;
	}
	auto valid = ValidateAgainstShader(*asset);
	if (!valid)
	{
		return valid;
	}
	shaderAsset = std::move(asset);
	SynchronizeBuiltinProperties();
	++version;
	return {};
}

Result<void> MaterialInstance::MigrateLegacyToShader(ResourceId id)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!id)
	{
		return Result<void>::Failure("Cannot migrate a material to a zero Shader resource ID");
	}
	Map<String, MaterialPropertyValue> overrides;
	overrides.emplace("_BaseColor", MaterialPropertyValue::ColorValue(parameters.baseColor));
	overrides.emplace("_AlphaMode", MaterialPropertyValue::Int(static_cast<Int32>(parameters.alphaMode)));
	overrides.emplace("_AlphaCutoff", MaterialPropertyValue::Range(parameters.alphaCutoff));
	if (type == MaterialType::PbrMetallicRoughness)
	{
		overrides.emplace("_Metallic", MaterialPropertyValue::Range(parameters.metallic));
		overrides.emplace("_Roughness", MaterialPropertyValue::Range(parameters.roughness));
		overrides.emplace("_NormalScale", MaterialPropertyValue::Float(parameters.normalScale));
		overrides.emplace("_OcclusionStrength", MaterialPropertyValue::Range(parameters.occlusionStrength));
		overrides.emplace("_Emissive", MaterialPropertyValue::ColorValue(parameters.emissive));
	}
	const Array<String, 5> unlitTextureNames{"_MainTex", {}, {}, {}, {}};
	const Array<String, 5> pbrTextureNames{"_BaseMap", "_MetallicRoughnessMap", "_NormalMap", "_OcclusionMap",
	                                       "_EmissiveMap"};
	for (Ursa::Size slot = 0; slot < textures.size(); ++slot)
	{
		if (!textures[slot])
		{
			continue;
		}
		if (!textureResourceIds[slot])
		{
			return Result<void>::Failure("Cannot migrate a material texture without a stable resource ID");
		}
		const String propertyName = type == MaterialType::Unlit ? unlitTextureNames[slot] : pbrTextureNames[slot];
		if (!propertyName.empty())
		{
			overrides.emplace(propertyName, MaterialPropertyValue::Texture2D(textureResourceIds[slot], textures[slot]));
		}
	}
	shaderId = id;
	if (id == Shader::BuiltinUnlitShaderId())
	{
		shaderAsset = std::make_shared<const Shader::ShaderAsset>(Shader::CreateBuiltinUnlitShaderAsset());
	}
	else if (id == Shader::BuiltinPbrShaderId())
	{
		shaderAsset = std::make_shared<const Shader::ShaderAsset>(Shader::CreateBuiltinPbrShaderAsset());
	}
	propertyOverrides = std::move(overrides);
	++version;
	return {};
}

Result<void> MaterialInstance::SetPropertyValue(const String& identifier, const Shader::ShaderAsset& shader,
                                                MaterialPropertyValue value)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!shaderId || shader.id != shaderId)
	{
		return Result<void>::Failure("Material Shader ID does not match the provided Shader asset");
	}
	const Shader::PropertyDeclaration* property = FindProperty(shader, identifier);
	if (!property)
	{
		return Result<void>::Failure("Shader does not declare material property " + identifier);
	}
	if (property->type != value.type)
	{
		return Result<void>::Failure("Material property " + identifier + " has a value with the wrong type");
	}
	if ((shader.id == Shader::BuiltinUnlitShaderId() || shader.id == Shader::BuiltinPbrShaderId()) &&
	    identifier == "_AlphaMode" &&
	    (value.intValue < 0 || value.intValue > static_cast<Ursa::Int32>(AlphaMode::Blend)))
	{
		return Result<void>::Failure("Built-in material Alpha Mode must be Opaque, Mask, or Blend");
	}
	if (value.type == Shader::PropertyType::Float && !std::isfinite(value.floatValue))
	{
		return Result<void>::Failure("Material Float property must be finite: " + identifier);
	}
	if (value.type == Shader::PropertyType::Range &&
	    (!std::isfinite(value.floatValue) || property->rangeMinimum > property->rangeMaximum ||
	     value.floatValue < property->rangeMinimum || value.floatValue > property->rangeMaximum))
	{
		return Result<void>::Failure("Material Range property is outside its declared bounds: " + identifier);
	}
	if (value.type == Shader::PropertyType::Color || value.type == Shader::PropertyType::Vector)
	{
		for (Ursa::Float32 component : value.vectorValue)
		{
			if (!std::isfinite(component))
			{
				return Result<void>::Failure("Material vector property must have finite components: " + identifier);
			}
		}
		if (value.type == Shader::PropertyType::Color && !HasPropertyAttribute(*property, "HDR"))
		{
			for (Ursa::Size component = 0; component < value.vectorValue.size(); ++component)
			{
				if (value.vectorValue[component] < 0.0f || value.vectorValue[component] > 1.0f)
				{
					return Result<void>::Failure("Material Color property must be in [0, 1]: " + identifier);
				}
			}
		}
	}
	if (value.type == Shader::PropertyType::Texture2D)
	{
		if (!value.textureId && !value.texture)
		{
			return Result<void>::Failure("Texture property override must reference a saved texture or be removed");
		}
		if (value.texture && !value.textureId)
		{
			return Result<void>::Failure("Material texture property requires a stable resource ID: " + identifier);
		}
		if (value.texture && HasPropertyAttribute(*property, "Normal") &&
		    value.texture->GetColorSpace() != TextureColorSpace::Linear)
		{
			return Result<void>::Failure("Normal texture properties require linear color space: " + identifier);
		}
	}
	propertyOverrides[identifier] = std::move(value);
	SynchronizeBuiltinProperties();
	++version;
	return {};
}

void MaterialInstance::SynchronizeBuiltinProperties()
{
	if (shaderId != Shader::BuiltinUnlitShaderId() && shaderId != Shader::BuiltinPbrShaderId())
	{
		return;
	}
	for (const auto& entry : propertyOverrides)
	{
		const MaterialPropertyValue& value = entry.second;
		if (entry.first == "_BaseColor" && value.type == Shader::PropertyType::Color)
		{
			parameters.baseColor = {value.vectorValue[0], value.vectorValue[1], value.vectorValue[2],
			                        value.vectorValue[3]};
		}
		else if (entry.first == "_Metallic" && value.type == Shader::PropertyType::Range)
		{
			parameters.metallic = value.floatValue;
		}
		else if (entry.first == "_Roughness" && value.type == Shader::PropertyType::Range)
		{
			parameters.roughness = value.floatValue;
		}
		else if (entry.first == "_NormalScale" && value.type == Shader::PropertyType::Float)
		{
			parameters.normalScale = value.floatValue;
		}
		else if (entry.first == "_OcclusionStrength" && value.type == Shader::PropertyType::Range)
		{
			parameters.occlusionStrength = value.floatValue;
		}
		else if (entry.first == "_Emissive" && value.type == Shader::PropertyType::Color)
		{
			parameters.emissive = {value.vectorValue[0], value.vectorValue[1], value.vectorValue[2],
			                       value.vectorValue[3]};
		}
		else if (entry.first == "_AlphaMode" && value.type == Shader::PropertyType::Int && value.intValue >= 0 &&
		         value.intValue <= static_cast<Ursa::Int32>(AlphaMode::Blend))
		{
			parameters.alphaMode = static_cast<AlphaMode>(value.intValue);
		}
		else if (entry.first == "_AlphaCutoff" && value.type == Shader::PropertyType::Range)
		{
			parameters.alphaCutoff = value.floatValue;
		}
	}
}

Result<void> MaterialInstance::ValidateAgainstShader(const Shader::ShaderAsset& shader) const
{
	if (!shaderId || shader.id != shaderId)
	{
		return Result<void>::Failure("Material Shader ID does not match the loaded Shader asset");
	}
	MaterialInstance candidate = *this;
	for (const auto& entry : propertyOverrides)
	{
		if (!FindProperty(shader, entry.first))
		{
			continue;
		}
		auto valid = candidate.SetPropertyValue(entry.first, shader, entry.second);
		if (!valid)
		{
			return Result<void>::Failure(valid.error);
		}
	}
	return {};
}

Result<MaterialPropertyValue> MaterialInstance::PropertyValue(const String& identifier,
                                                              const Shader::ShaderAsset& shader) const
{
	if (!shaderId || shader.id != shaderId)
	{
		return Result<MaterialPropertyValue>::Failure("Material Shader ID does not match the provided Shader asset");
	}
	auto overrideValue = propertyOverrides.find(identifier);
	if (overrideValue != propertyOverrides.end())
	{
		return Result<MaterialPropertyValue>::Success(overrideValue->second);
	}
	const Shader::PropertyDeclaration* property = FindProperty(shader, identifier);
	if (!property)
	{
		return Result<MaterialPropertyValue>::Failure("Shader does not declare material property " + identifier);
	}
	MaterialPropertyValue value;
	value.type = property->type;
	switch (property->type)
	{
		case Shader::PropertyType::Float:
		case Shader::PropertyType::Range:
			value.floatValue = property->defaultFloat;
			break;
		case Shader::PropertyType::Int:
			value.intValue = property->defaultInt;
			break;
		case Shader::PropertyType::Color:
		case Shader::PropertyType::Vector:
			value.vectorValue = property->defaultVector;
			break;
		case Shader::PropertyType::Texture2D:
			value.textureDefault = property->defaultTexture;
			break;
	}
	return Result<MaterialPropertyValue>::Success(std::move(value));
}

Result<void> MaterialInstance::RemovePropertyOverride(const String& identifier)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	const auto found = propertyOverrides.find(identifier);
	if (found == propertyOverrides.end())
	{
		return {};
	}
	propertyOverrides.erase(found);
	++version;
	return {};
}

Result<void> MaterialInstance::SetKeywordEnabled(const String& keyword, bool enabled)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (keyword.empty())
	{
		return Result<void>::Failure("Material keyword cannot be empty");
	}
	for (char character : keyword)
	{
		if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_')
		{
			return Result<void>::Failure("Material keyword contains an invalid character: " + keyword);
		}
	}
	auto found = std::find(keywords.begin(), keywords.end(), keyword);
	if (enabled && found == keywords.end())
	{
		keywords.push_back(keyword);
		std::sort(keywords.begin(), keywords.end());
		++version;
	}
	else if (!enabled && found != keywords.end())
	{
		keywords.erase(found);
		++version;
	}
	return {};
}

bool MaterialInstance::KeywordEnabled(const String& keyword) const
{
	return std::find(keywords.begin(), keywords.end(), keyword) != keywords.end();
}

Result<void> MaterialInstance::SetBaseColor(Color color)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!ValidColor(color, false))
	{
		return Result<void>::Failure("Material baseColor must have finite components in [0, 1]");
	}
	parameters.baseColor = color;
	if (shaderId == Shader::BuiltinUnlitShaderId() || shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_BaseColor"] = MaterialPropertyValue::ColorValue(color);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetMetallic(Ursa::Float32 value)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (type != MaterialType::PbrMetallicRoughness)
	{
		return Result<void>::Failure("Metallic is only valid for a PBR material");
	}
	if (!unit(value))
	{
		return Result<void>::Failure("Material metallic must be finite and in [0, 1]");
	}
	parameters.metallic = value;
	if (shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_Metallic"] = MaterialPropertyValue::Range(value);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetRoughness(Ursa::Float32 value)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (type != MaterialType::PbrMetallicRoughness)
	{
		return Result<void>::Failure("Roughness is only valid for a PBR material");
	}
	if (!unit(value))
	{
		return Result<void>::Failure("Material roughness must be finite and in [0, 1]");
	}
	parameters.roughness = value;
	if (shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_Roughness"] = MaterialPropertyValue::Range(value);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetNormalScale(Ursa::Float32 value)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (type != MaterialType::PbrMetallicRoughness)
	{
		return Result<void>::Failure("Normal scale is only valid for a PBR material");
	}
	if (!Nonnegative(value))
	{
		return Result<void>::Failure("Material normal scale must be finite and nonnegative");
	}
	parameters.normalScale = value;
	if (shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_NormalScale"] = MaterialPropertyValue::Float(value);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetOcclusionStrength(Ursa::Float32 value)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (type != MaterialType::PbrMetallicRoughness)
	{
		return Result<void>::Failure("Occlusion is only valid for a PBR material");
	}
	if (!unit(value))
	{
		return Result<void>::Failure("Material occlusion strength must be finite and in [0, 1]");
	}
	parameters.occlusionStrength = value;
	if (shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_OcclusionStrength"] = MaterialPropertyValue::Range(value);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetEmissive(Color color)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (type != MaterialType::PbrMetallicRoughness)
	{
		return Result<void>::Failure("Emissive is only valid for a PBR material");
	}
	if (!ValidColor(color, true))
	{
		return Result<void>::Failure("Material emissive color must be finite and nonnegative");
	}
	parameters.emissive = color;
	if (shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_Emissive"] = MaterialPropertyValue::ColorValue(color);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetAlphaMode(AlphaMode mode, Ursa::Float32 cutoff)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (mode != AlphaMode::Opaque && mode != AlphaMode::Mask && mode != AlphaMode::Blend)
	{
		return Result<void>::Failure("Material alpha mode is invalid");
	}
	if (!unit(cutoff))
	{
		return Result<void>::Failure("Material alpha cutoff must be finite and in [0, 1]");
	}
	parameters.alphaMode = mode;
	parameters.alphaCutoff = cutoff;
	if (shaderId == Shader::BuiltinPbrShaderId())
	{
		propertyOverrides["_AlphaMode"] = MaterialPropertyValue::Int(static_cast<Int32>(mode));
		propertyOverrides["_AlphaCutoff"] = MaterialPropertyValue::Range(cutoff);
	}
	++version;
	return {};
}

Result<void> MaterialInstance::SetSampler(MaterialSampler sampler)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	parameters.sampler = sampler;
	samplerOverridden = true;
	++version;
	return {};
}

Result<void> MaterialInstance::SetTexture(MaterialTextureSlot slot, MaterialTexturePtr texture)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!ValidSlot(slot))
	{
		return Result<void>::Failure("Material texture slot is invalid");
	}
	if (type == MaterialType::Unlit && slot != MaterialTextureSlot::BaseColor)
	{
		return Result<void>::Failure("Unlit material only accepts a base color texture");
	}
	if (texture && texture->GetColorSpace() != RequiredColorSpace(slot))
	{
		return Result<void>::Failure("Material texture color space is incompatible with its slot");
	}
	textures[SlotIndex(slot)] = std::move(texture);
	textureResourceIds[SlotIndex(slot)] = {};
	textureLocalFileIds[SlotIndex(slot)] = 0;
	if (IsShaderBacked())
	{
		const String propertyName = BuiltinTextureProperty(shaderId, slot);
		if (!propertyName.empty())
		{
			if (textures[SlotIndex(slot)])
			{
				MaterialPropertyValue value;
				value.type = Shader::PropertyType::Texture2D;
				value.texture = textures[SlotIndex(slot)];
				propertyOverrides[propertyName] = std::move(value);
			}
			else
			{
				propertyOverrides.erase(propertyName);
			}
		}
	}
	++version;
	return {};
}

MaterialTexturePtr MaterialInstance::Texture(MaterialTextureSlot slot) const
{
	return ValidSlot(slot) ? textures[SlotIndex(slot)] : MaterialTexturePtr{};
}

ResourceId MaterialInstance::TextureResourceId(MaterialTextureSlot slot) const
{
	return ValidSlot(slot) ? textureResourceIds[SlotIndex(slot)] : ResourceId{};
}

Result<void> MaterialInstance::SetTextureResourceId(MaterialTextureSlot slot, ResourceId id)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	if (!ValidSlot(slot) || (textures[SlotIndex(slot)] && !id))
	{
		return Result<void>::Failure("Invalid material texture slot or zero resource ID");
	}
	textureResourceIds[SlotIndex(slot)] = id;
	textureLocalFileIds[SlotIndex(slot)] = 0;
	if (shaderId)
	{
		const String propertyName = BuiltinTextureProperty(shaderId, slot);
		if (!propertyName.empty() && textures[SlotIndex(slot)] && id)
		{
			propertyOverrides[propertyName] = MaterialPropertyValue::Texture2D(id, textures[SlotIndex(slot)]);
		}
	}
	return {};
}

MaterialTexturePtr MaterialInstance::ResolvedTexture(MaterialTextureSlot slot) const
{
	auto selected = Texture(slot);
	return selected ? selected : DefaultMaterialTexture(slot);
}

std::shared_ptr<MaterialInstance> MaterialInstance::Clone() const
{
	if (!alive)
	{
		return nullptr;
	}
	auto clone = MakeShared<MaterialInstance>(Memory::MemoryTag::Assets, *this);
	clone->assetIdentity = {};
	clone->localIdentity = ResourceId::Generate();
	return clone;
}

MaterialTexturePtr DefaultMaterialTexture(MaterialTextureSlot slot)
{
	static const Array<MaterialTexturePtr, 5> defaults = {
	    MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {0, 255, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {128, 128, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::Linear, {255, 255, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255}).value,
	};
	return ValidSlot(slot) ? defaults[SlotIndex(slot)] : MaterialTexturePtr{};
}
} // namespace Ursa
