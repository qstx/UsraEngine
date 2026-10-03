#include <core/StringInterop.hpp>
#include <core/Containers.hpp>
#include <material/Material.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <class T> T take(ursa::Result<T> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
    return std::move(result.value);
}

void done(ursa::Result<void> result) {
    if (!result)
        throw std::runtime_error(ursa::toStdString(result.error));
}

void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void testParameters() {
    ursa::MaterialInstance pbr(ursa::MaterialType::PbrMetallicRoughness);
    done(pbr.setBaseColor({0.3f, 0.6f, 0.2f, 0.5f}));
    done(pbr.setMetallic(0.7f));
    done(pbr.setRoughness(0.2f));
    done(pbr.setNormalScale(1.4f));
    done(pbr.setOcclusionStrength(0.8f));
    done(pbr.setEmissive({2.0f, 0.2f, 0.0f, 1.0f}));
    done(pbr.setAlphaMode(ursa::AlphaMode::Mask, 0.35f));
    expect(pbr.parameters().metallic == 0.7f && pbr.parameters().roughness == 0.2f &&
               pbr.parameters().normalScale == 1.4f && pbr.parameters().occlusionStrength == 0.8f &&
               pbr.parameters().emissive.red == 2.0f && pbr.parameters().alphaCutoff == 0.35f,
           "Valid PBR parameters were not stored");
    const uint64_t version = pbr.version();
    const auto badMetallic = pbr.setMetallic(-0.1f);
    const auto badRoughness = pbr.setRoughness(std::numeric_limits<float>::quiet_NaN());
    const auto badCutoff = pbr.setAlphaMode(ursa::AlphaMode::Blend, 1.1f);
    expect(!badMetallic && badMetallic.error.find("metallic") != ursa::String::npos,
           "Invalid metallic lacked a diagnostic");
    expect(!badRoughness && badRoughness.error.find("roughness") != ursa::String::npos,
           "Invalid roughness lacked a diagnostic");
    expect(!badCutoff && badCutoff.error.find("cutoff") != ursa::String::npos,
           "Invalid alpha cutoff lacked a diagnostic");
    expect(pbr.parameters().metallic == 0.7f && pbr.parameters().roughness == 0.2f &&
               pbr.parameters().alphaMode == ursa::AlphaMode::Mask && pbr.version() == version,
           "Rejected material parameters changed the instance");
    ursa::MaterialInstance unlit(ursa::MaterialType::Unlit);
    expect(!unlit.setMetallic(0.5f) && !unlit.setNormalScale(1.0f), "Unlit accepted PBR-only parameters");
}

void testSharingAndClone() {
    auto shared = std::make_shared<ursa::MaterialInstance>(ursa::MaterialType::Unlit);
    auto first = shared;
    auto second = shared;
    const uint64_t originalVersion = shared->version();
    done(first->setBaseColor({1.0f, 0.0f, 0.0f, 1.0f}));
    expect(second->parameters().baseColor.red == 1.0f && second->parameters().baseColor.green == 0.0f &&
               second->version() == originalVersion + 1,
           "Shared material did not observe a modification");
    auto independent = shared->clone();
    done(independent->setBaseColor({0.0f, 0.0f, 1.0f, 1.0f}));
    expect(independent->parameters().baseColor.blue == 1.0f && shared->parameters().baseColor.red == 1.0f &&
               independent->version() == shared->version() + 1,
           "Material clone was not independent");
}

void testProceduralTextures() {
    const auto invalid = ursa::MaterialTexture::create(2, 2, ursa::TextureColorSpace::Srgb, {1, 2, 3, 4});
    expect(!invalid && invalid.error.find("pixel") != ursa::String::npos,
           "Incorrect pixel byte count lacked a diagnostic");
    expect(!ursa::MaterialTexture::create(0, 1, ursa::TextureColorSpace::Srgb, {}), "Zero-width texture was accepted");
    auto color =
        take(ursa::MaterialTexture::create(2, 1, ursa::TextureColorSpace::Srgb, {255, 0, 0, 255, 0, 255, 0, 128}));
    auto data = take(ursa::MaterialTexture::create(1, 1, ursa::TextureColorSpace::Linear, {128, 128, 255, 255}));
    ursa::MaterialInstance pbr(ursa::MaterialType::PbrMetallicRoughness);
    done(pbr.setTexture(ursa::MaterialTextureSlot::BaseColor, color));
    done(pbr.setTexture(ursa::MaterialTextureSlot::Normal, data));
    const uint64_t version = pbr.version();
    const auto mismatch = pbr.setTexture(ursa::MaterialTextureSlot::Normal, color);
    expect(!mismatch && mismatch.error.find("color space") != ursa::String::npos && pbr.version() == version &&
               pbr.texture(ursa::MaterialTextureSlot::Normal) == data,
           "Wrong texture color space changed the binding");
    expect(pbr.resolvedTexture(ursa::MaterialTextureSlot::Occlusion)->pixels()[0] == 255 &&
               pbr.resolvedTexture(ursa::MaterialTextureSlot::MetallicRoughness)->pixels()[1] == 255 &&
               pbr.resolvedTexture(ursa::MaterialTextureSlot::MetallicRoughness)->pixels()[2] == 255 &&
               pbr.resolvedTexture(ursa::MaterialTextureSlot::Emissive)->pixels()[0] == 255,
           "Default material textures have incorrect values");
    ursa::MaterialInstance unlit(ursa::MaterialType::Unlit);
    expect(!unlit.setTexture(ursa::MaterialTextureSlot::Normal, data), "Unlit accepted a normal texture");
}
} // namespace

int main() {
    try {
        testParameters();
        testSharingAndClone();
        testProceduralTextures();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
