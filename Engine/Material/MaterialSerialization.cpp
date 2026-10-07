#include <Material/Material.hpp>
#include <Serialization/ArchiveFields.hpp>
#include <Serialization/SerializationContext.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/MaterialResource.hpp>

namespace Ursa
{
namespace
{
constexpr Array<const char*, 5> slotNames{"baseColor", "metallicRoughness", "normal", "occlusion", "emissive"};
}

std::shared_ptr<MaterialTexture> MaterialTexture::CreateForDeserialization()
{
	return std::shared_ptr<MaterialTexture>(new MaterialTexture(1, 1, TextureColorSpace::Linear, {255, 255, 255, 255}));
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
	return result;
}

Result<void> MaterialTexture::Deserialize(const ArchiveReader& reader, SerializationContext&)
{
	auto loadedWidth = reader.Unsigned("width");
	auto loadedHeight = reader.Unsigned("height");
	auto space = reader.Text("colorSpace");
	auto data = reader.Bytes("pixels");
	if (!loadedWidth || !loadedHeight || !space || !data || loadedWidth.value > UINT32_MAX ||
	    loadedHeight.value > UINT32_MAX || (space.value != "Srgb" && space.value != "Linear"))
	{
		return Result<void>::Failure("Invalid texture archive fields");
	}
	auto candidate =
	    Create(UInt32(loadedWidth.value), UInt32(loadedHeight.value),
	           space.value == "Srgb" ? TextureColorSpace::Srgb : TextureColorSpace::Linear, std::move(data.value));
	if (!candidate)
	{
		return Result<void>::Failure(candidate.error);
	}
	width = candidate.value->width;
	height = candidate.value->height;
	colorSpace = candidate.value->colorSpace;
	pixels = candidate.value->pixels;
	return {};
}

Result<void> MaterialInstance::Serialize(ArchiveWriter& writer) const
{
	auto result = writer.Write("type", String(type == MaterialType::Unlit ? "Unlit" : "PbrMetallicRoughness"));
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
		auto id = writer.Context()->Identify(*textures[i]);
		if (!id)
		{
			return Result<void>::Failure(id.error);
		}
		result = textureFields.value.Write(slotNames[i], id.value.ToString());
		if (!result)
		{
			return result;
		}
	}
	return {};
}

Result<void> MaterialInstance::Deserialize(const ArchiveReader& reader, SerializationContext& context)
{
	auto loadedType = reader.Text("type");
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
		auto text = textureFields.value.Text(slotNames[i]);
		if (!text)
		{
			return Result<void>::Failure(text.error);
		}
		auto id = ResourceId::Parse(text.value);
		auto loader = context.Service<TextureResourceLoader>();
		if (!id || (!context.ResourceLoader() && (!loader || !*loader.value)))
		{
			return Result<void>::Failure("Invalid material texture reference");
		}
		auto loaded =
		    context.ResourceLoader() ? context.ResourceLoader()->LoadTexture(id.value) : (*loader.value)(id.value);
		if (!loaded)
		{
			return Result<void>::Failure(loaded.error);
		}
		result = candidate.SetTexture(MaterialTextureSlot(i), loaded.value);
		if (!result)
		{
			return result;
		}
	}
	type = candidate.type;
	parameters = candidate.parameters;
	textures = std::move(candidate.textures);
	++version;
	return {};
}
} // namespace Ursa
