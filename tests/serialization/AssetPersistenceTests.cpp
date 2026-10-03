#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <serialization/MaterialResource.hpp>
#include <serialization/ResourceSession.hpp>
#include <serialization/TextureResource.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void testTexture(const std::filesystem::path &directory) {
    const auto path = directory / "checker.ursatex";
    const auto id = ursa::ResourceId::generate();
    const ursa::Vector<uint8_t> pixels{255, 30, 20, 255, 10, 200, 80, 128, 0, 0, 0, 0, 45, 55, 230, 240};
    const auto texture = ursa::MaterialTexture::create(2, 2, ursa::TextureColorSpace::Srgb, pixels);
    expect(static_cast<bool>(texture), "Cannot create source texture");
    expect(static_cast<bool>(ursa::saveTextureResource(path, id, *texture.value)), "Cannot save texture resource");
    const auto restored = ursa::loadTextureResource(path);
    expect(restored && restored.value.id == id && restored.value.texture->width() == 2 &&
               restored.value.texture->height() == 2 &&
               restored.value.texture->colorSpace() == ursa::TextureColorSpace::Srgb &&
               restored.value.texture->pixels() == pixels,
           "Texture resource roundtrip lost data");

    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
    const auto truncated = ursa::loadTextureResource(path);
    expect(!truncated && truncated.error.find("byte count") != ursa::String::npos,
           "Truncated texture resource was accepted");
    std::filesystem::remove(path);
}

void testMaterial(const std::filesystem::path &directory) {
    const auto colorPath = directory / "color.ursatex";
    const auto normalPath = directory / "normal.ursatex";
    const auto materialPath = directory / "surface.ursamat";
    const auto materialId = ursa::ResourceId::generate();
    const auto colorId = ursa::ResourceId::generate();
    const auto normalId = ursa::ResourceId::generate();
    auto color = ursa::MaterialTexture::create(1, 1, ursa::TextureColorSpace::Srgb, {210, 80, 40, 200});
    auto normal = ursa::MaterialTexture::create(1, 1, ursa::TextureColorSpace::Linear, {128, 128, 255, 255});
    expect(color && normal, "Could not create test textures");
    expect(static_cast<bool>(ursa::saveTextureResource(colorPath, colorId, *color.value)) &&
               static_cast<bool>(ursa::saveTextureResource(normalPath, normalId, *normal.value)),
           "Could not save material textures");
    ursa::MaterialInstance source(ursa::MaterialType::PbrMetallicRoughness);
    expect(static_cast<bool>(source.setBaseColor({0.8f, 0.3f, 0.2f, 0.7f})) &&
               static_cast<bool>(source.setMetallic(0.35f)) && static_cast<bool>(source.setRoughness(0.62f)) &&
               static_cast<bool>(source.setNormalScale(1.4f)) && static_cast<bool>(source.setOcclusionStrength(0.5f)) &&
               static_cast<bool>(source.setEmissive({0.2f, 0.1f, 0.05f, 1.0f})) &&
               static_cast<bool>(source.setAlphaMode(ursa::AlphaMode::Blend, 0.4f)) &&
               static_cast<bool>(source.setSampler({false, false})) &&
               static_cast<bool>(source.setTexture(ursa::MaterialTextureSlot::BaseColor, color.value)) &&
               static_cast<bool>(source.setTexture(ursa::MaterialTextureSlot::Normal, normal.value)),
           "Could not configure source material");
    const auto saved =
        ursa::saveMaterialResource(materialPath, materialId, source, [&](ursa::MaterialTexturePtr texture) {
            if (texture == color.value)
                return ursa::Result<ursa::ResourceId>::success(colorId);
            if (texture == normal.value)
                return ursa::Result<ursa::ResourceId>::success(normalId);
            return ursa::Result<ursa::ResourceId>::failure("Unknown texture");
        });
    expect(static_cast<bool>(saved), "Could not save PBR material");
    const auto restored = ursa::loadMaterialResource(materialPath, [&](ursa::ResourceId id) {
        auto loaded = ursa::loadTextureResource(id == colorId ? colorPath : normalPath);
        if (!loaded || loaded.value.id != id)
            return ursa::Result<ursa::MaterialTexturePtr>::failure("Wrong texture ID");
        return ursa::Result<ursa::MaterialTexturePtr>::success(loaded.value.texture);
    });
    expect(restored && restored.value.id == materialId &&
               restored.value.material->type() == ursa::MaterialType::PbrMetallicRoughness &&
               restored.value.material->parameters().baseColor.red == 0.8f &&
               restored.value.material->parameters().metallic == 0.35f &&
               restored.value.material->parameters().roughness == 0.62f &&
               restored.value.material->parameters().normalScale == 1.4f &&
               restored.value.material->parameters().occlusionStrength == 0.5f &&
               restored.value.material->parameters().emissive.red == 0.2f &&
               restored.value.material->parameters().alphaMode == ursa::AlphaMode::Blend &&
               restored.value.material->parameters().alphaCutoff == 0.4f &&
               !restored.value.material->parameters().sampler.linear &&
               !restored.value.material->parameters().sampler.repeat &&
               restored.value.material->texture(ursa::MaterialTextureSlot::BaseColor)->pixels() ==
                   color.value->pixels() &&
               restored.value.material->texture(ursa::MaterialTextureSlot::Normal)->pixels() == normal.value->pixels(),
           "PBR material roundtrip lost data");

    const auto invalid = directory / "bad.ursamat";
    {
        std::ofstream output(invalid);
        output << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << ursa::toStdString(materialId.toString())
               << "\",\"type\":\"Unlit\",\"parameters\":{\"baseColor\":[1,1,1,1],"
                  "\"alphaMode\":\"Opaque\",\"alphaCutoff\":0.5,\"sampler\":{\"linear\":true,"
                  "\"repeat\":true}},\"textures\":{\"normal\":\""
               << ursa::toStdString(normalId.toString()) << "\"}}";
    }
    const auto rejected = ursa::loadMaterialResource(invalid, {});
    expect(!rejected && rejected.error.find("normal") != ursa::String::npos, "Unlit accepted a normal texture");
    const auto invalidRange = directory / "bad-range.ursamat";
    {
        std::ofstream output(invalidRange);
        output << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << ursa::toStdString(materialId.toString())
               << "\",\"type\":\"PbrMetallicRoughness\",\"parameters\":{\"baseColor\":[1,1,1,1],"
                  "\"alphaMode\":\"Opaque\",\"alphaCutoff\":0.5,\"sampler\":{\"linear\":true,"
                  "\"repeat\":true},\"metallic\":0,\"roughness\":2,\"normalScale\":1,"
                  "\"occlusionStrength\":1,\"emissive\":[0,0,0,1]},\"textures\":{}}";
    }
    const auto rejectedRange = ursa::loadMaterialResource(invalidRange, {});
    expect(!rejectedRange && rejectedRange.error.find("roughness") != ursa::String::npos,
           "Out-of-range material parameter was accepted");
    std::filesystem::remove(colorPath);
    std::filesystem::remove(normalPath);
    std::filesystem::remove(materialPath);
    std::filesystem::remove(invalid);
    std::filesystem::remove(invalidRange);
}

void testSession(const std::filesystem::path &directory) {
    const auto root = directory / "session";
    std::filesystem::create_directory(root);
    const auto textureId = ursa::ResourceId::generate();
    const auto firstId = ursa::ResourceId::generate();
    const auto secondId = ursa::ResourceId::generate();
    const auto texturePath = root / "shared.ursatex";
    const auto firstPath = root / "first.ursamat";
    const auto secondPath = root / "second.ursamat";
    auto red = ursa::MaterialTexture::create(1, 1, ursa::TextureColorSpace::Srgb, {255, 0, 0, 255});
    expect(red && static_cast<bool>(ursa::saveTextureResource(texturePath, textureId, *red.value)),
           "Could not save shared texture");
    ursa::MaterialInstance source(ursa::MaterialType::Unlit);
    expect(static_cast<bool>(source.setTexture(ursa::MaterialTextureSlot::BaseColor, red.value)),
           "Could not bind shared texture");
    auto resolve = [&](ursa::MaterialTexturePtr selected) {
        return selected == red.value ? ursa::Result<ursa::ResourceId>::success(textureId)
                                     : ursa::Result<ursa::ResourceId>::failure("Unexpected texture");
    };
    expect(static_cast<bool>(ursa::saveMaterialResource(firstPath, firstId, source, resolve)) &&
               static_cast<bool>(ursa::saveMaterialResource(secondPath, secondId, source, resolve)),
           "Could not save shared materials");
    auto opened = ursa::ResourceSession::open({root});
    expect(static_cast<bool>(opened), "Could not open resource session");
    auto first = opened.value->loadMaterial(firstId);
    auto firstAgain = opened.value->loadMaterial(firstId);
    auto second = opened.value->loadMaterial(secondId);
    expect(first && firstAgain && second && first.value == firstAgain.value &&
               first.value->texture(ursa::MaterialTextureSlot::BaseColor) ==
                   second.value->texture(ursa::MaterialTextureSlot::BaseColor),
           "Resource session did not preserve shared identity");

    auto blue = ursa::MaterialTexture::create(1, 1, ursa::TextureColorSpace::Srgb, {0, 0, 255, 255});
    expect(blue && static_cast<bool>(ursa::saveTextureResource(texturePath, textureId, *blue.value)),
           "Could not update texture on disk");
    auto refreshed = opened.value->refreshed();
    expect(static_cast<bool>(refreshed), "Could not refresh resource session");
    auto reloaded = refreshed.value->loadMaterial(firstId);
    expect(reloaded && reloaded.value != first.value &&
               reloaded.value->texture(ursa::MaterialTextureSlot::BaseColor)->pixels()[0] == 0 &&
               first.value->texture(ursa::MaterialTextureSlot::BaseColor)->pixels()[0] == 255,
           "Fresh session failed to see disk update or modified old resource");
    std::filesystem::remove(texturePath);
    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
    std::filesystem::remove(root);
}
} // namespace

int main() {
    try {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("UrsaAssetPersistenceTests-" + ursa::toStdString(ursa::ResourceId::generate().toString()));
        std::filesystem::create_directory(directory);
        testTexture(directory);
        testMaterial(directory);
        testSession(directory);
        std::filesystem::remove(directory);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
