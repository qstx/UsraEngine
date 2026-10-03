#include <serialization/JsonInterop.hpp>
#include <serialization/MaterialResource.hpp>
#include <serialization/JsonResource.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace Ursa
{
namespace
{
using Json = nlohmann::json;

constexpr Array<const char*, 5> slotNames{"baseColor", "metallicRoughness", "normal", "occlusion", "emissive"};

Json ColorJson(Color color)
{
	return Json::array({color.red, color.green, color.blue, color.alpha});
}

Result<float> ReadNumber(const Json& object, const char* key, const String& context)
{
	auto field = object.find(key);
	if (field == object.end() || !field->is_number())
	{
		return Result<float>::Failure("Invalid or missing " + context + "." + key);
	}
	const double value = field->get<double>();
	if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
	    value > std::numeric_limits<float>::max())
	{
		return Result<float>::Failure("Out-of-range " + context + "." + key);
	}
	return Result<float>::Success(static_cast<float>(value));
}

Result<Color> ReadColor(const Json& object, const char* key, const String& context)
{
	auto field = object.find(key);
	if (field == object.end() || !field->is_array() || field->size() != 4)
	{
		return Result<Color>::Failure("Invalid or missing " + context + "." + key);
	}
	Color color;
	Array<float*, 4> channels{&color.red, &color.green, &color.blue, &color.alpha};
	for (size_t index = 0; index < channels.size(); ++index)
	{
		if (!(*field)[index].is_number())
		{
			return Result<Color>::Failure("Invalid " + context + "." + key + " channel " +
			                              ToUrsaString(std::to_string(index)));
		}
		const double value = (*field)[index].get<double>();
		if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
		    value > std::numeric_limits<float>::max())
		{
			return Result<Color>::Failure("Out-of-range " + context + "." + key);
		}
		*channels[index] = static_cast<float>(value);
	}
	return Result<Color>::Success(color);
}

Result<bool> ReadBoolean(const Json& object, const char* key, const String& context)
{
	auto field = object.find(key);
	if (field == object.end() || !field->is_boolean())
	{
		return Result<bool>::Failure("Invalid or missing " + context + "." + key);
	}
	return Result<bool>::Success(field->get<bool>());
}

Result<void> Contextualize(Result<void> result, const String& context)
{
	return result ? result : Result<void>::Failure(context + ": " + result.error);
}

Result<void> LoadParameters(const Json& fields, MaterialInstance& material, const String& context)
{
	if (!fields.is_object())
	{
		return Result<void>::Failure("Invalid " + context + ".parameters");
	}
	const auto base = ReadColor(fields, "baseColor", context);
	if (!base)
	{
		return Result<void>::Failure(base.error);
	}
	auto assigned = Contextualize(material.SetBaseColor(base.value), context + ".baseColor");
	if (!assigned)
	{
		return assigned;
	}

	auto alpha = fields.find("alphaMode");
	if (alpha == fields.end() || !alpha->is_string())
	{
		return Result<void>::Failure("Invalid or missing " + context + ".alphaMode");
	}
	AlphaMode mode;
	const auto name = alpha->get<String>();
	if (name == "Opaque")
	{
		mode = AlphaMode::opaque;
	}
	else if (name == "Mask")
	{
		mode = AlphaMode::mask;
	}
	else if (name == "Blend")
	{
		mode = AlphaMode::blend;
	}
	else
	{
		return Result<void>::Failure("Unknown " + context + ".alphaMode: " + name);
	}
	const auto cutoff = ReadNumber(fields, "alphaCutoff", context);
	if (!cutoff)
	{
		return Result<void>::Failure(cutoff.error);
	}
	assigned = Contextualize(material.SetAlphaMode(mode, cutoff.value), context + ".alphaMode");
	if (!assigned)
	{
		return assigned;
	}

	auto sampler = fields.find("sampler");
	if (sampler == fields.end() || !sampler->is_object())
	{
		return Result<void>::Failure("Invalid or missing " + context + ".sampler");
	}
	const auto linear = ReadBoolean(*sampler, "linear", context + ".sampler");
	const auto repeat = ReadBoolean(*sampler, "repeat", context + ".sampler");
	if (!linear || !repeat)
	{
		return Result<void>::Failure(!linear ? linear.error : repeat.error);
	}
	assigned = material.SetSampler({linear.value, repeat.value});
	if (!assigned)
	{
		return assigned;
	}

	constexpr Array<const char*, 5> pbrKeys{"metallic", "roughness", "normalScale", "occlusionStrength", "emissive"};
	if (material.Type() == MaterialType::unlit)
	{
		for (const char* key : pbrKeys)
		{
			if (fields.contains(key))
			{
				return Result<void>::Failure("Unlit material cannot contain " + context + "." + key);
			}
		}
		return {};
	}

	const auto metallic = ReadNumber(fields, "metallic", context);
	const auto roughness = ReadNumber(fields, "roughness", context);
	const auto normalScale = ReadNumber(fields, "normalScale", context);
	const auto occlusion = ReadNumber(fields, "occlusionStrength", context);
	const auto emissive = ReadColor(fields, "emissive", context);
	if (!metallic || !roughness || !normalScale || !occlusion || !emissive)
	{
		return Result<void>::Failure(!metallic      ? metallic.error
		                             : !roughness   ? roughness.error
		                             : !normalScale ? normalScale.error
		                             : !occlusion   ? occlusion.error
		                                            : emissive.error);
	}
	const Array<Result<void>, 5> results{material.SetMetallic(metallic.value), material.SetRoughness(roughness.value),
	                                     material.SetNormalScale(normalScale.value),
	                                     material.SetOcclusionStrength(occlusion.value),
	                                     material.SetEmissive(emissive.value)};
	for (size_t index = 0; index < results.size(); ++index)
	{
		if (!results[index])
		{
			return Result<void>::Failure(context + "." + pbrKeys[index] + ": " + results[index].error);
		}
	}
	return {};
}
} // namespace

Result<void> SaveMaterialResource(const std::filesystem::path& path, ResourceId id, const MaterialInstance& material,
                                  const TextureIdResolver& resolveTexture)
{
	if (!id || path.extension() != ".ursamat")
	{
		return Result<void>::Failure("Invalid material resource ID or .ursamat path: " + PathToUrsaString(path));
	}
	const auto type = material.Type();
	if (type != MaterialType::unlit && type != MaterialType::pbrMetallicRoughness)
	{
		return Result<void>::Failure("Invalid material type at " + PathToUrsaString(path));
	}
	const auto& parameters = material.Parameters();
	Json fields{{"baseColor", ColorJson(parameters.baseColor)},
	            {"alphaMode", parameters.alphaMode == AlphaMode::opaque ? "Opaque"
	                          : parameters.alphaMode == AlphaMode::mask ? "Mask"
	                                                                    : "Blend"},
	            {"alphaCutoff", parameters.alphaCutoff},
	            {"sampler", {{"linear", parameters.sampler.linear}, {"repeat", parameters.sampler.repeat}}}};
	if (type == MaterialType::pbrMetallicRoughness)
	{
		fields["metallic"] = parameters.metallic;
		fields["roughness"] = parameters.roughness;
		fields["normalScale"] = parameters.normalScale;
		fields["occlusionStrength"] = parameters.occlusionStrength;
		fields["emissive"] = ColorJson(parameters.emissive);
	}
	Json textures = Json::object();
	for (size_t index = 0; index < slotNames.size(); ++index)
	{
		const auto selected = material.Texture(static_cast<MaterialTextureSlot>(index));
		if (!selected)
		{
			continue;
		}
		if (!resolveTexture)
		{
			return Result<void>::Failure("No texture ID resolver for " + PathToUrsaString(path));
		}
		const auto resolved = resolveTexture(selected);
		if (!resolved || !resolved.value)
		{
			return Result<void>::Failure("Cannot resolve texture slot " + String(slotNames[index]) + " in " +
			                             PathToUrsaString(path) + ": " + resolved.error);
		}
		textures[slotNames[index]] = resolved.value.ToString();
	}
	const Json document{{"format", "UrsaMaterial"},
	                    {"version", resourceFormatVersion},
	                    {"id", id.ToString()},
	                    {"type", type == MaterialType::unlit ? "Unlit" : "PbrMetallicRoughness"},
	                    {"parameters", std::move(fields)},
	                    {"textures", std::move(textures)}};
	return WriteJsonResource(path, document);
}

Result<LoadedMaterialResource> LoadMaterialResource(const std::filesystem::path& path,
                                                    const TextureResourceLoader& loadTexture)
{
	auto document = ReadJsonResource(path, ResourceKind::material);
	if (!document)
	{
		return Result<LoadedMaterialResource>::Failure(document.error);
	}
	auto typeField = document.value.find("type");
	if (typeField == document.value.end() || !typeField->is_string())
	{
		return Result<LoadedMaterialResource>::Failure("Invalid material type at " + PathToUrsaString(path));
	}
	const auto typeName = typeField->get<String>();
	MaterialType type;
	if (typeName == "Unlit")
	{
		type = MaterialType::unlit;
	}
	else if (typeName == "PbrMetallicRoughness")
	{
		type = MaterialType::pbrMetallicRoughness;
	}
	else
	{
		return Result<LoadedMaterialResource>::Failure("Unknown material type " + typeName + " at " +
		                                               PathToUrsaString(path));
	}
	auto material = std::make_shared<MaterialInstance>(type);
	auto fields = document.value.find("parameters");
	if (fields == document.value.end())
	{
		return Result<LoadedMaterialResource>::Failure("Missing material parameters at " + PathToUrsaString(path));
	}
	auto parameters = LoadParameters(*fields, *material, PathToUrsaString(path) + ".parameters");
	if (!parameters)
	{
		return Result<LoadedMaterialResource>::Failure(parameters.error);
	}

	auto textures = document.value.find("textures");
	if (textures == document.value.end() || !textures->is_object())
	{
		return Result<LoadedMaterialResource>::Failure("Invalid material textures at " + PathToUrsaString(path));
	}
	for (auto field = textures->begin(); field != textures->end(); ++field)
	{
		const auto matched =
		    std::find_if(slotNames.begin(), slotNames.end(), [&](const char* name) { return field.key() == name; });
		if (matched == slotNames.end() || !field.value().is_string())
		{
			return Result<LoadedMaterialResource>::Failure("Invalid material texture slot " +
			                                               ToUrsaString(field.key()) + " at " + PathToUrsaString(path));
		}
		const auto slot = static_cast<MaterialTextureSlot>(std::distance(slotNames.begin(), matched));
		if (type == MaterialType::unlit && slot != MaterialTextureSlot::baseColor)
		{
			return Result<LoadedMaterialResource>::Failure("Unlit material cannot use texture slot " +
			                                               ToUrsaString(field.key()) + " at " + PathToUrsaString(path));
		}
		auto textureId = ResourceId::Parse(field.value().get<String>());
		if (!textureId)
		{
			return Result<LoadedMaterialResource>::Failure("Invalid texture ID in " + PathToUrsaString(path) +
			                                               ".textures." + ToUrsaString(field.key()) + ": " +
			                                               textureId.error);
		}
		if (!loadTexture)
		{
			return Result<LoadedMaterialResource>::Failure("No texture loader for " + PathToUrsaString(path));
		}
		auto loaded = loadTexture(textureId.value);
		if (!loaded)
		{
			return Result<LoadedMaterialResource>::Failure("Cannot load texture " + textureId.value.ToString() +
			                                               " for " + PathToUrsaString(path) + ": " + loaded.error);
		}
		auto assigned = material->SetTexture(slot, std::move(loaded.value));
		if (!assigned)
		{
			return Result<LoadedMaterialResource>::Failure("Invalid texture slot " + ToUrsaString(field.key()) +
			                                               " in " + PathToUrsaString(path) + ": " + assigned.error);
		}
	}

	auto header = ReadResourceHeader(path);
	if (!header)
	{
		return Result<LoadedMaterialResource>::Failure(header.error);
	}
	return Result<LoadedMaterialResource>::Success({header.value.id, std::move(material)});
}
} // namespace Ursa
