#include <Material/Material.hpp>
#include <Serialization/ArchiveFields.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/AssetReference.hpp>

namespace Ursa
{
namespace
{
constexpr Array<const char*, 5> slotNames{"baseColor", "metallicRoughness", "normal", "occlusion", "emissive"};

String PropertyTypeName(Shader::PropertyType type)
{
	switch (type)
	{
		case Shader::PropertyType::Float:
			return "Float";
		case Shader::PropertyType::Range:
			return "Range";
		case Shader::PropertyType::Int:
			return "Int";
		case Shader::PropertyType::Color:
			return "Color";
		case Shader::PropertyType::Vector:
			return "Vector";
		case Shader::PropertyType::Texture2D:
			return "Texture2D";
	}
	return "Unknown";
}

Result<Shader::PropertyType> ParsePropertyType(const String& type)
{
	if (type == "Float")
	{
		return Result<Shader::PropertyType>::Success(Shader::PropertyType::Float);
	}
	if (type == "Range")
	{
		return Result<Shader::PropertyType>::Success(Shader::PropertyType::Range);
	}
	if (type == "Int")
	{
		return Result<Shader::PropertyType>::Success(Shader::PropertyType::Int);
	}
	if (type == "Color")
	{
		return Result<Shader::PropertyType>::Success(Shader::PropertyType::Color);
	}
	if (type == "Vector")
	{
		return Result<Shader::PropertyType>::Success(Shader::PropertyType::Vector);
	}
	if (type == "Texture2D")
	{
		return Result<Shader::PropertyType>::Success(Shader::PropertyType::Texture2D);
	}
	return Result<Shader::PropertyType>::Failure("Unknown material Shader property value type " + type);
}

Result<void> WritePropertyValue(ArchiveWriter& writer, const MaterialPropertyValue& value)
{
	auto result = writer.Write("type", PropertyTypeName(value.type));
	if (!result)
	{
		return result;
	}
	switch (value.type)
	{
		case Shader::PropertyType::Float:
		case Shader::PropertyType::Range:
			return writer.Write("float", Float64(value.floatValue));
		case Shader::PropertyType::Int:
			return writer.Write("int", Int64(value.intValue));
		case Shader::PropertyType::Color:
		case Shader::PropertyType::Vector:
			return WriteNumbers(
			    writer, "vector",
			    {value.vectorValue[0], value.vectorValue[1], value.vectorValue[2], value.vectorValue[3]});
		case Shader::PropertyType::Texture2D:
			if (!value.textureId && !value.textureDefault.empty())
			{
				return writer.Write("textureDefault", value.textureDefault);
			}
			if (!value.textureId)
			{
				if (!value.texture || !writer.Context())
				{
					return Result<void>::Failure("Texture property override requires a stable resource ID");
				}
				auto id = writer.Context()->Identify(*value.texture);
				return id ? WriteAssetReference(writer, "textureId", {id.value, 0}) : Result<void>::Failure(id.error);
			}
			return WriteAssetReference(writer, "textureId", {value.textureId, value.textureLocalFileId});
	}
	return Result<void>::Failure("Unknown material Shader property value type");
}

Result<MaterialPropertyValue> ReadPropertyValue(const ArchiveReader& reader, SerializationContext& context)
{
	auto typeName = reader.Text("type");
	if (!typeName)
	{
		return Result<MaterialPropertyValue>::Failure(typeName.error);
	}
	auto type = ParsePropertyType(typeName.value);
	if (!type)
	{
		return Result<MaterialPropertyValue>::Failure(type.error);
	}
	MaterialPropertyValue value;
	value.type = type.value;
	switch (value.type)
	{
		case Shader::PropertyType::Float:
		case Shader::PropertyType::Range:
		{
			auto number = reader.Number("float");
			if (!number || !std::isfinite(number.value))
			{
				return Result<MaterialPropertyValue>::Failure("Invalid material Shader floating point override");
			}
			value.floatValue = Float32(number.value);
			break;
		}
		case Shader::PropertyType::Int:
		{
			auto number = reader.Integer("int");
			if (!number || number.value < INT32_MIN || number.value > INT32_MAX)
			{
				return Result<MaterialPropertyValue>::Failure("Invalid material Shader integer override");
			}
			value.intValue = Int32(number.value);
			break;
		}
		case Shader::PropertyType::Color:
		case Shader::PropertyType::Vector:
		{
			auto components = ReadNumbers(reader, "vector", 4);
			if (!components)
			{
				return Result<MaterialPropertyValue>::Failure(components.error);
			}
			for (Size index = 0; index < value.vectorValue.size(); ++index)
			{
				if (!std::isfinite(components.value[index]))
				{
					return Result<MaterialPropertyValue>::Failure("Non-finite material Shader vector component");
				}
				value.vectorValue[index] = components.value[index];
			}
			break;
		}
		case Shader::PropertyType::Texture2D:
		{
			if (reader.Contains("textureDefault"))
			{
				auto name = reader.Text("textureDefault");
				if (!name || (name.value != "white" && name.value != "black" && name.value != "normal"))
				{
					return Result<MaterialPropertyValue>::Failure("Invalid Shader texture default");
				}
				value.textureDefault = name.value;
				break;
			}
			auto reference = ReadAssetReference(reader, "textureId");
			if (!reference)
			{
				return Result<MaterialPropertyValue>::Failure(reference.error);
			}
			auto loader = context.Service<TextureResourceLoader>();
			if (!context.ResourceLoader() && (!loader || !*loader.value))
			{
				return Result<MaterialPropertyValue>::Failure("Missing texture resource loader");
			}
			auto texture = context.ResourceLoader() ? context.ResourceLoader()->LoadTexture(reference.value.guid,
			                                                                                reference.value.localFileId)
			                                        : (*loader.value)(reference.value.guid);
			if (!texture)
			{
				return Result<MaterialPropertyValue>::Failure(texture.error);
			}
			value.textureId = reference.value.guid;
			value.textureLocalFileId = reference.value.localFileId;
			value.texture = std::move(texture.value);
			break;
		}
	}
	return Result<MaterialPropertyValue>::Success(std::move(value));
}
} // namespace

std::shared_ptr<MaterialTexture> MaterialTexture::CreateForDeserialization()
{
	return std::shared_ptr<MaterialTexture>(
	    new MaterialTexture(1, 1, TextureColorSpace::Linear, {255, 255, 255, 255}, {}, MaterialSampler{}));
}

Result<void> MaterialTexture::Serialize(ArchiveWriter& writer) const
{
	auto result = writer.Write("width", UInt64(width));
	if (result)
	{
		result = writer.Write("height", UInt64(height));
	}
	if (result)
	{
		result = writer.Write("colorSpace", String(colorSpace == TextureColorSpace::Srgb ? "Srgb" : "Linear"));
	}
	if (result)
	{
		result = writer.WriteBytes("pixels", pixels);
	}
	if (!result)
	{
		return result;
	}
	auto mipArray = writer.Array("mipmaps");
	if (!mipArray)
	{
		return Result<void>::Failure(mipArray.error);
	}
	for (const TextureMip& mipmap : mipmaps)
	{
		auto mip = mipArray.value.AddObject();
		if (!mip)
		{
			return Result<void>::Failure(mip.error);
		}
		result = mip.value.Write("width", UInt64(mipmap.width));
		if (result)
		{
			result = mip.value.Write("height", UInt64(mipmap.height));
		}
		if (result)
		{
			result = mip.value.WriteBytes("pixels", mipmap.pixels);
		}
		if (!result)
		{
			return result;
		}
	}
	auto samplerWriter = writer.Object("sampler");
	if (!samplerWriter)
	{
		return Result<void>::Failure(samplerWriter.error);
	}
	result = samplerWriter.value.Write("linear", sampler.linear);
	if (result)
	{
		result = samplerWriter.value.Write("repeat", sampler.repeat);
	}
	return result;
}

Result<void> MaterialTexture::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto loadedWidth = reader.Unsigned("width");
	auto loadedHeight = reader.Unsigned("height");
	auto space = reader.Text("colorSpace");
	auto data = reader.Bytes("pixels");
	Vector<TextureMip> loadedMipmaps;
	MaterialSampler loadedSampler;
	if (reader.Contains("sampler"))
	{
		auto samplerReader = reader.Object("sampler");
		auto linear =
		    samplerReader ? samplerReader.value.Boolean("linear") : Result<bool>::Failure(samplerReader.error);
		auto repeat =
		    samplerReader ? samplerReader.value.Boolean("repeat") : Result<bool>::Failure(samplerReader.error);
		if (!linear || !repeat)
		{
			return Result<void>::Failure("Invalid texture sampler archive fields");
		}
		loadedSampler = {linear.value, repeat.value};
	}
	if (reader.Contains("mipmaps"))
	{
		auto mipArray = reader.Array("mipmaps");
		if (!mipArray)
		{
			return Result<void>::Failure(mipArray.error);
		}
		for (const ArchiveReader& mipReader : mipArray.value)
		{
			auto mipWidth = mipReader.Unsigned("width");
			auto mipHeight = mipReader.Unsigned("height");
			auto mipPixels = mipReader.Bytes("pixels");
			if (!mipWidth || !mipHeight || !mipPixels || mipWidth.value > UINT32_MAX || mipHeight.value > UINT32_MAX)
			{
				return Result<void>::Failure("Invalid texture mip archive fields");
			}
			loadedMipmaps.push_back({UInt32(mipWidth.value), UInt32(mipHeight.value), std::move(mipPixels.value)});
		}
	}
	if (!loadedWidth || !loadedHeight || !space || !data || loadedWidth.value > UINT32_MAX ||
	    loadedHeight.value > UINT32_MAX || (space.value != "Srgb" && space.value != "Linear"))
	{
		return Result<void>::Failure("Invalid texture archive fields");
	}
	auto candidate = Create(UInt32(loadedWidth.value), UInt32(loadedHeight.value),
	                        space.value == "Srgb" ? TextureColorSpace::Srgb : TextureColorSpace::Linear,
	                        std::move(data.value), std::move(loadedMipmaps), loadedSampler);
	if (!candidate)
	{
		return Result<void>::Failure(candidate.error);
	}
	width = candidate.value->width;
	height = candidate.value->height;
	colorSpace = candidate.value->colorSpace;
	pixels = candidate.value->pixels;
	mipmaps = candidate.value->mipmaps;
	sampler = candidate.value->sampler;
	return {};
}

Result<void> MaterialInstance::Serialize(ArchiveWriter& writer) const
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	auto result = writer.Write("type", String(type == MaterialType::Unlit ? "Unlit" : "PbrMetallicRoughness"));
	if (result)
	{
		result = writer.Write("samplerOverridden", samplerOverridden);
	}
	if (result && IsShaderBacked())
	{
		result = WriteAssetReference(writer, "shaderId", {shaderId, 0});
	}
	auto fields = writer.Object("parameters");
	if (!fields)
	{
		return Result<void>::Failure(fields.error);
	}
	auto sampler = fields.value.Object("sampler");
	if (!sampler)
	{
		return Result<void>::Failure(sampler.error);
	}

	if (result)
	{
		result = WriteNumbers(fields.value, "baseColor",
		                      {parameters.baseColor.red, parameters.baseColor.green, parameters.baseColor.blue,
		                       parameters.baseColor.alpha});
	}
	if (result)
	{
		result = fields.value.Write("alphaMode", String(parameters.alphaMode == AlphaMode::Opaque ? "Opaque"
		                                                : parameters.alphaMode == AlphaMode::Mask ? "Mask"
		                                                                                          : "Blend"));
	}
	if (result)
	{
		result = fields.value.Write("alphaCutoff", Float64(parameters.alphaCutoff));
	}
	if (result)
	{
		result = sampler.value.Write("linear", parameters.sampler.linear);
	}
	if (result)
	{
		result = sampler.value.Write("repeat", parameters.sampler.repeat);
	}
	if (result && type == MaterialType::PbrMetallicRoughness)
	{
		result = fields.value.Write("metallic", Float64(parameters.metallic));
		if (result)
		{
			result = fields.value.Write("roughness", Float64(parameters.roughness));
		}
		if (result)
		{
			result = fields.value.Write("normalScale", Float64(parameters.normalScale));
		}
		if (result)
		{
			result = fields.value.Write("occlusionStrength", Float64(parameters.occlusionStrength));
		}
		if (result)
		{
			result = WriteNumbers(fields.value, "emissive",
			                      {parameters.emissive.red, parameters.emissive.green, parameters.emissive.blue,
			                       parameters.emissive.alpha});
		}
	}
	if (!result)
	{
		return result;
	}
	auto textureFields = writer.Object("textures");
	if (!textureFields)
	{
		return Result<void>::Failure(textureFields.error);
	}
	for (Size i = 0; i < textures.size(); ++i)
	{
		if (!textures[i])
		{
			continue;
		}
		if (!writer.Context())
		{
			return Result<void>::Failure("Missing texture ID resolver");
		}
		ResourceId id = textureResourceIds[i];
		if (!id && writer.Context())
		{
			auto identified = writer.Context()->Identify(*textures[i]);
			if (!identified)
			{
				return Result<void>::Failure(identified.error);
			}
			id = identified.value;
		}
		if (!id)
		{
			return Result<void>::Failure("Material texture has no stable resource ID");
		}
		result = WriteAssetReference(textureFields.value, slotNames[i], {id, textureLocalFileIds[i]});
		if (!result)
		{
			return result;
		}
	}
	if (IsShaderBacked())
	{
		auto overrides = writer.Object("propertyOverrides");
		if (!overrides)
		{
			return Result<void>::Failure(overrides.error);
		}
		for (const auto& entry : propertyOverrides)
		{
			auto value = overrides.value.Object(entry.first);
			if (!value)
			{
				return Result<void>::Failure(value.error);
			}
			result = WritePropertyValue(value.value, entry.second);
			if (!result)
			{
				return result;
			}
		}
		auto keywordArray = writer.Array("keywords");
		if (!keywordArray)
		{
			return Result<void>::Failure(keywordArray.error);
		}
		for (const String& keyword : keywords)
		{
			result = keywordArray.value.Append(keyword);
			if (!result)
			{
				return result;
			}
		}
	}
	return {};
}

Result<void> MaterialInstance::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	if (!alive)
	{
		return Result<void>::Failure("Material has been destroyed");
	}
	auto loadedType = reader.Text("type");
	if (!loadedType)
	{
		return Result<void>::Failure(loadedType.error);
	}
	if (!reader.Contains("shaderId"))
	{
		return DeserializeLegacy(reader, context);
	}
	auto shaderReference = ReadAssetReference(reader, "shaderId");
	auto overrides = reader.Object("propertyOverrides");
	auto keywordReaders = reader.Array("keywords");
	if (!shaderReference || shaderReference.value.localFileId != 0 || !overrides || !keywordReaders)
	{
		return Result<void>::Failure(!shaderReference ? shaderReference.error
		                             : shaderReference.value.localFileId != 0
		                                 ? "Shader sub-asset references are not supported by this loader"
		                             : !overrides ? overrides.error
		                                          : keywordReaders.error);
	}
	MaterialInstance candidate(loadedType.value == "PbrMetallicRoughness" ? MaterialType::PbrMetallicRoughness
	                                                                      : MaterialType::Unlit);
	auto restoredLegacy = candidate.DeserializeLegacy(reader, context);
	if (!restoredLegacy)
	{
		return restoredLegacy;
	}
	candidate.shaderId = shaderReference.value.guid;
	auto names = overrides.value.Fields();
	if (!names)
	{
		return Result<void>::Failure(names.error);
	}
	for (const String& propertyName : names.value)
	{
		auto valueReader = overrides.value.Object(propertyName);
		if (!valueReader)
		{
			return Result<void>::Failure(valueReader.error);
		}
		auto value = ReadPropertyValue(valueReader.value, context);
		if (!value)
		{
			return Result<void>::Failure("Material property " + propertyName + ": " + value.error);
		}
		candidate.propertyOverrides.emplace(propertyName, std::move(value.value));
	}
	for (const ArchiveReader& keywordReader : keywordReaders.value)
	{
		auto keyword = keywordReader.TextValue();
		if (!keyword)
		{
			return Result<void>::Failure(keyword.error);
		}
		auto enabled = candidate.SetKeywordEnabled(keyword.value, true);
		if (!enabled)
		{
			return enabled;
		}
	}
	candidate.assetIdentity = assetIdentity;
	candidate.localIdentity = localIdentity;
	*this = std::move(candidate);
	++version;
	return {};
}

Result<void> MaterialInstance::DeserializeLegacy(const ArchiveReader& reader, SerializationContext& context)
{
	auto loadedType = reader.Text("type");
	if (!loadedType)
	{
		return Result<void>::Failure(loadedType.error);
	}
	auto fields = reader.Object("parameters");
	if (!fields)
	{
		return Result<void>::Failure(fields.error);
	}
	auto sampler = fields.value.Object("sampler");
	if (!sampler)
	{
		return Result<void>::Failure(sampler.error);
	}

	auto base = ReadNumbers(fields.value, "baseColor", 4);
	auto alpha = fields.value.Text("alphaMode");
	auto cutoff = fields.value.Number("alphaCutoff");
	auto linear = sampler.value.Boolean("linear");
	auto repeat = sampler.value.Boolean("repeat");
	if (!loadedType || !base || !alpha || !cutoff || !linear || !repeat ||
	    (alpha.value != "Opaque" && alpha.value != "Mask" && alpha.value != "Blend") ||
	    (loadedType.value != "Unlit" && loadedType.value != "PbrMetallicRoughness"))
	{
		return Result<void>::Failure("Invalid material archive fields");
	}
	MaterialInstance candidate(loadedType.value == "Unlit" ? MaterialType::Unlit : MaterialType::PbrMetallicRoughness);
	if (candidate.type == MaterialType::Unlit)
	{
		for (const char* field : {"metallic", "roughness", "normalScale", "occlusionStrength", "emissive"})
		{
			if (fields.value.Contains(field))
			{
				return Result<void>::Failure("Unlit material cannot contain " + String(field));
			}
		}
	}
	auto result = candidate.SetBaseColor({base.value[0], base.value[1], base.value[2], base.value[3]});
	if (result)
	{
		result = candidate.SetAlphaMode(alpha.value == "Opaque" ? AlphaMode::Opaque
		                                : alpha.value == "Mask" ? AlphaMode::Mask
		                                                        : AlphaMode::Blend,
		                                Float32(cutoff.value));
	}
	if (result)
	{
		result = candidate.SetSampler({linear.value, repeat.value});
	}
	if (!result)
	{
		return result;
	}
	if (reader.Contains("samplerOverridden"))
	{
		auto samplerOverrideFlag = reader.Boolean("samplerOverridden");
		if (!samplerOverrideFlag)
		{
			return Result<void>::Failure("Invalid material sampler override flag");
		}
		candidate.samplerOverridden = samplerOverrideFlag.value;
	}
	else
	{
		candidate.samplerOverridden = true;
	}
	if (candidate.type == MaterialType::PbrMetallicRoughness)
	{
		auto metallic = fields.value.Number("metallic");
		auto roughness = fields.value.Number("roughness");
		auto normal = fields.value.Number("normalScale");
		auto occlusion = fields.value.Number("occlusionStrength");
		auto emissive = ReadNumbers(fields.value, "emissive", 4);
		if (!metallic || !roughness || !normal || !occlusion || !emissive)
		{
			return Result<void>::Failure("Invalid PBR archive fields");
		}
		result = candidate.SetMetallic(Float32(metallic.value));
		if (result)
		{
			result = candidate.SetRoughness(Float32(roughness.value));
		}
		if (result)
		{
			result = candidate.SetNormalScale(Float32(normal.value));
		}
		if (result)
		{
			result = candidate.SetOcclusionStrength(Float32(occlusion.value));
		}
		if (result)
		{
			result =
			    candidate.SetEmissive({emissive.value[0], emissive.value[1], emissive.value[2], emissive.value[3]});
		}
		if (!result)
		{
			return result;
		}
	}
	auto textureFields = reader.Object("textures");
	if (!textureFields)
	{
		return Result<void>::Failure(textureFields.error);
	}
	auto names = textureFields.value.Fields();
	if (!names)
	{
		return Result<void>::Failure(names.error);
	}
	for (const auto& field : names.value)
	{
		if (candidate.type == MaterialType::Unlit && field != "baseColor")
		{
			return Result<void>::Failure("Unlit material cannot use texture slot " + field);
		}
		if (std::find(slotNames.begin(), slotNames.end(), field) == slotNames.end())
		{
			return Result<void>::Failure("Invalid material texture slot " + field);
		}
	}
	for (Size i = 0; i < textures.size(); ++i)
	{
		if (!textureFields.value.Contains(slotNames[i]))
		{
			continue;
		}
		auto reference = ReadAssetReference(textureFields.value, slotNames[i]);
		auto loader = context.Service<TextureResourceLoader>();
		if (!reference || (reference.value.localFileId != 0 && !context.ResourceLoader()) ||
		    (!context.ResourceLoader() && (!loader || !*loader.value)))
		{
			return Result<void>::Failure("Invalid material texture reference");
		}
		auto loaded = context.ResourceLoader()
		                  ? context.ResourceLoader()->LoadTexture(reference.value.guid, reference.value.localFileId)
		                  : (*loader.value)(reference.value.guid);
		if (!loaded)
		{
			return Result<void>::Failure(loaded.error);
		}
		result = candidate.SetTexture(MaterialTextureSlot(i), loaded.value);
		if (!result)
		{
			return result;
		}
		candidate.textureResourceIds[i] = reference.value.guid;
		candidate.textureLocalFileIds[i] = reference.value.localFileId;
	}
	type = candidate.type;
	parameters = candidate.parameters;
	samplerOverridden = candidate.samplerOverridden;
	textures = std::move(candidate.textures);
	textureResourceIds = std::move(candidate.textureResourceIds);
	textureLocalFileIds = std::move(candidate.textureLocalFileIds);
	++version;
	return {};
}
} // namespace Ursa
