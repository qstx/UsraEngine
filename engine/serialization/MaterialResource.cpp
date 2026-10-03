#include <serialization/JsonInterop.hpp>
#include <serialization/MaterialResource.hpp>
#include <serialization/JsonResource.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace ursa {
namespace {
using Json = nlohmann::json;

constexpr Array<const char *, 5> slotNames{"baseColor", "metallicRoughness", "normal", "occlusion", "emissive"};

Json colorJson(Color color) {
    return Json::array({color.red, color.green, color.blue, color.alpha});
}

Result<float> readNumber(const Json &object, const char *key, const String &context) {
    auto field = object.find(key);
    if (field == object.end() || !field->is_number())
        return Result<float>::failure("Invalid or missing " + context + "." + key);
    const double value = field->get<double>();
    if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
        value > std::numeric_limits<float>::max())
        return Result<float>::failure("Out-of-range " + context + "." + key);
    return Result<float>::success(static_cast<float>(value));
}

Result<Color> readColor(const Json &object, const char *key, const String &context) {
    auto field = object.find(key);
    if (field == object.end() || !field->is_array() || field->size() != 4)
        return Result<Color>::failure("Invalid or missing " + context + "." + key);
    Color color;
    Array<float *, 4> channels{&color.red, &color.green, &color.blue, &color.alpha};
    for (size_t index = 0; index < channels.size(); ++index) {
        if (!(*field)[index].is_number())
            return Result<Color>::failure("Invalid " + context + "." + key + " channel " +
                                          toUrsaString(std::to_string(index)));
        const double value = (*field)[index].get<double>();
        if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
            value > std::numeric_limits<float>::max())
            return Result<Color>::failure("Out-of-range " + context + "." + key);
        *channels[index] = static_cast<float>(value);
    }
    return Result<Color>::success(color);
}

Result<bool> readBoolean(const Json &object, const char *key, const String &context) {
    auto field = object.find(key);
    if (field == object.end() || !field->is_boolean())
        return Result<bool>::failure("Invalid or missing " + context + "." + key);
    return Result<bool>::success(field->get<bool>());
}

Result<void> contextualize(Result<void> result, const String &context) {
    return result ? result : Result<void>::failure(context + ": " + result.error);
}

Result<void> loadParameters(const Json &fields, MaterialInstance &material, const String &context) {
    if (!fields.is_object())
        return Result<void>::failure("Invalid " + context + ".parameters");
    const auto base = readColor(fields, "baseColor", context);
    if (!base)
        return Result<void>::failure(base.error);
    auto assigned = contextualize(material.setBaseColor(base.value), context + ".baseColor");
    if (!assigned)
        return assigned;

    auto alpha = fields.find("alphaMode");
    if (alpha == fields.end() || !alpha->is_string())
        return Result<void>::failure("Invalid or missing " + context + ".alphaMode");
    AlphaMode mode;
    const auto name = alpha->get<String>();
    if (name == "Opaque")
        mode = AlphaMode::Opaque;
    else if (name == "Mask")
        mode = AlphaMode::Mask;
    else if (name == "Blend")
        mode = AlphaMode::Blend;
    else
        return Result<void>::failure("Unknown " + context + ".alphaMode: " + name);
    const auto cutoff = readNumber(fields, "alphaCutoff", context);
    if (!cutoff)
        return Result<void>::failure(cutoff.error);
    assigned = contextualize(material.setAlphaMode(mode, cutoff.value), context + ".alphaMode");
    if (!assigned)
        return assigned;

    auto sampler = fields.find("sampler");
    if (sampler == fields.end() || !sampler->is_object())
        return Result<void>::failure("Invalid or missing " + context + ".sampler");
    const auto linear = readBoolean(*sampler, "linear", context + ".sampler");
    const auto repeat = readBoolean(*sampler, "repeat", context + ".sampler");
    if (!linear || !repeat)
        return Result<void>::failure(!linear ? linear.error : repeat.error);
    assigned = material.setSampler({linear.value, repeat.value});
    if (!assigned)
        return assigned;

    constexpr Array<const char *, 5> pbrKeys{"metallic", "roughness", "normalScale", "occlusionStrength", "emissive"};
    if (material.type() == MaterialType::Unlit) {
        for (const char *key : pbrKeys)
            if (fields.contains(key))
                return Result<void>::failure("Unlit material cannot contain " + context + "." + key);
        return {};
    }

    const auto metallic = readNumber(fields, "metallic", context);
    const auto roughness = readNumber(fields, "roughness", context);
    const auto normalScale = readNumber(fields, "normalScale", context);
    const auto occlusion = readNumber(fields, "occlusionStrength", context);
    const auto emissive = readColor(fields, "emissive", context);
    if (!metallic || !roughness || !normalScale || !occlusion || !emissive)
        return Result<void>::failure(!metallic      ? metallic.error
                                     : !roughness   ? roughness.error
                                     : !normalScale ? normalScale.error
                                     : !occlusion   ? occlusion.error
                                                    : emissive.error);
    const Array<Result<void>, 5> results{material.setMetallic(metallic.value), material.setRoughness(roughness.value),
                                         material.setNormalScale(normalScale.value),
                                         material.setOcclusionStrength(occlusion.value),
                                         material.setEmissive(emissive.value)};
    for (size_t index = 0; index < results.size(); ++index)
        if (!results[index])
            return Result<void>::failure(context + "." + pbrKeys[index] + ": " + results[index].error);
    return {};
}
} // namespace

Result<void> saveMaterialResource(const std::filesystem::path &path, ResourceId id, const MaterialInstance &material,
                                  const TextureIdResolver &resolveTexture) {
    if (!id || path.extension() != ".ursamat")
        return Result<void>::failure("Invalid material resource ID or .ursamat path: " + pathToUrsaString(path));
    const auto type = material.type();
    if (type != MaterialType::Unlit && type != MaterialType::PbrMetallicRoughness)
        return Result<void>::failure("Invalid material type at " + pathToUrsaString(path));
    const auto &parameters = material.parameters();
    Json fields{{"baseColor", colorJson(parameters.baseColor)},
                {"alphaMode", parameters.alphaMode == AlphaMode::Opaque ? "Opaque"
                              : parameters.alphaMode == AlphaMode::Mask ? "Mask"
                                                                        : "Blend"},
                {"alphaCutoff", parameters.alphaCutoff},
                {"sampler", {{"linear", parameters.sampler.linear}, {"repeat", parameters.sampler.repeat}}}};
    if (type == MaterialType::PbrMetallicRoughness) {
        fields["metallic"] = parameters.metallic;
        fields["roughness"] = parameters.roughness;
        fields["normalScale"] = parameters.normalScale;
        fields["occlusionStrength"] = parameters.occlusionStrength;
        fields["emissive"] = colorJson(parameters.emissive);
    }
    Json textures = Json::object();
    for (size_t index = 0; index < slotNames.size(); ++index) {
        const auto selected = material.texture(static_cast<MaterialTextureSlot>(index));
        if (!selected)
            continue;
        if (!resolveTexture)
            return Result<void>::failure("No texture ID resolver for " + pathToUrsaString(path));
        const auto resolved = resolveTexture(selected);
        if (!resolved || !resolved.value)
            return Result<void>::failure("Cannot resolve texture slot " + String(slotNames[index]) + " in " +
                                         pathToUrsaString(path) + ": " + resolved.error);
        textures[slotNames[index]] = resolved.value.toString();
    }
    const Json document{{"format", "UrsaMaterial"},
                        {"version", resourceFormatVersion},
                        {"id", id.toString()},
                        {"type", type == MaterialType::Unlit ? "Unlit" : "PbrMetallicRoughness"},
                        {"parameters", std::move(fields)},
                        {"textures", std::move(textures)}};
    return writeJsonResource(path, document);
}

Result<LoadedMaterialResource> loadMaterialResource(const std::filesystem::path &path,
                                                    const TextureResourceLoader &loadTexture) {
    auto document = readJsonResource(path, ResourceKind::Material);
    if (!document)
        return Result<LoadedMaterialResource>::failure(document.error);
    auto typeField = document.value.find("type");
    if (typeField == document.value.end() || !typeField->is_string())
        return Result<LoadedMaterialResource>::failure("Invalid material type at " + pathToUrsaString(path));
    const auto typeName = typeField->get<String>();
    MaterialType type;
    if (typeName == "Unlit")
        type = MaterialType::Unlit;
    else if (typeName == "PbrMetallicRoughness")
        type = MaterialType::PbrMetallicRoughness;
    else
        return Result<LoadedMaterialResource>::failure("Unknown material type " + typeName + " at " +
                                                       pathToUrsaString(path));
    auto material = std::make_shared<MaterialInstance>(type);
    auto fields = document.value.find("parameters");
    if (fields == document.value.end())
        return Result<LoadedMaterialResource>::failure("Missing material parameters at " + pathToUrsaString(path));
    auto parameters = loadParameters(*fields, *material, pathToUrsaString(path) + ".parameters");
    if (!parameters)
        return Result<LoadedMaterialResource>::failure(parameters.error);

    auto textures = document.value.find("textures");
    if (textures == document.value.end() || !textures->is_object())
        return Result<LoadedMaterialResource>::failure("Invalid material textures at " + pathToUrsaString(path));
    for (auto field = textures->begin(); field != textures->end(); ++field) {
        const auto matched =
            std::find_if(slotNames.begin(), slotNames.end(), [&](const char *name) { return field.key() == name; });
        if (matched == slotNames.end() || !field.value().is_string())
            return Result<LoadedMaterialResource>::failure("Invalid material texture slot " +
                                                           toUrsaString(field.key()) + " at " + pathToUrsaString(path));
        const auto slot = static_cast<MaterialTextureSlot>(std::distance(slotNames.begin(), matched));
        if (type == MaterialType::Unlit && slot != MaterialTextureSlot::BaseColor)
            return Result<LoadedMaterialResource>::failure("Unlit material cannot use texture slot " +
                                                           toUrsaString(field.key()) + " at " + pathToUrsaString(path));
        auto textureId = ResourceId::parse(field.value().get<String>());
        if (!textureId)
            return Result<LoadedMaterialResource>::failure("Invalid texture ID in " + pathToUrsaString(path) +
                                                           ".textures." + toUrsaString(field.key()) + ": " +
                                                           textureId.error);
        if (!loadTexture)
            return Result<LoadedMaterialResource>::failure("No texture loader for " + pathToUrsaString(path));
        auto loaded = loadTexture(textureId.value);
        if (!loaded)
            return Result<LoadedMaterialResource>::failure("Cannot load texture " + textureId.value.toString() +
                                                           " for " + pathToUrsaString(path) + ": " + loaded.error);
        auto assigned = material->setTexture(slot, std::move(loaded.value));
        if (!assigned)
            return Result<LoadedMaterialResource>::failure("Invalid texture slot " + toUrsaString(field.key()) +
                                                           " in " + pathToUrsaString(path) + ": " + assigned.error);
    }

    auto header = readResourceHeader(path);
    if (!header)
        return Result<LoadedMaterialResource>::failure(header.error);
    return Result<LoadedMaterialResource>::success({header.value.id, std::move(material)});
}
} // namespace ursa
