#include <material/Material.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace ursa {
namespace {
bool unit(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool nonnegative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

bool validColor(Color color, bool hdr) {
    const auto valid = hdr ? nonnegative : unit;
    return valid(color.red) && valid(color.green) && valid(color.blue) && unit(color.alpha);
}

size_t slotIndex(MaterialTextureSlot slot) {
    return static_cast<size_t>(slot);
}

bool validSlot(MaterialTextureSlot slot) {
    return slotIndex(slot) < 5;
}

TextureColorSpace requiredColorSpace(MaterialTextureSlot slot) {
    return slot == MaterialTextureSlot::BaseColor || slot == MaterialTextureSlot::Emissive ? TextureColorSpace::Srgb
                                                                                           : TextureColorSpace::Linear;
}
} // namespace

MaterialTexture::MaterialTexture(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels)
    : width_(width), height_(height), colorSpace_(colorSpace), pixels_(std::move(pixels)) {
}

Result<MaterialTexturePtr> MaterialTexture::create(uint32_t width, uint32_t height, TextureColorSpace colorSpace,
                                                   Vector<uint8_t> rgba8) {
    if (!width || !height || static_cast<uint64_t>(width) * height > std::numeric_limits<size_t>::max() / 4 ||
        rgba8.size() != static_cast<size_t>(width) * height * 4)
        return Result<MaterialTexturePtr>::failure("Material texture dimensions or RGBA8 pixel byte count are invalid");
    if (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb)
        return Result<MaterialTexturePtr>::failure("Material texture color space is invalid");
    return Result<MaterialTexturePtr>::success(
        MaterialTexturePtr(new MaterialTexture(width, height, colorSpace, std::move(rgba8))));
}

MaterialInstance::MaterialInstance(MaterialType type) : type_(type) {
}

Result<void> MaterialInstance::setBaseColor(Color color) {
    if (!validColor(color, false))
        return Result<void>::failure("Material baseColor must have finite components in [0, 1]");
    parameters_.baseColor = color;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setMetallic(float value) {
    if (type_ != MaterialType::PbrMetallicRoughness)
        return Result<void>::failure("Metallic is only valid for a PBR material");
    if (!unit(value))
        return Result<void>::failure("Material metallic must be finite and in [0, 1]");
    parameters_.metallic = value;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setRoughness(float value) {
    if (type_ != MaterialType::PbrMetallicRoughness)
        return Result<void>::failure("Roughness is only valid for a PBR material");
    if (!unit(value))
        return Result<void>::failure("Material roughness must be finite and in [0, 1]");
    parameters_.roughness = value;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setNormalScale(float value) {
    if (type_ != MaterialType::PbrMetallicRoughness)
        return Result<void>::failure("Normal scale is only valid for a PBR material");
    if (!nonnegative(value))
        return Result<void>::failure("Material normal scale must be finite and nonnegative");
    parameters_.normalScale = value;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setOcclusionStrength(float value) {
    if (type_ != MaterialType::PbrMetallicRoughness)
        return Result<void>::failure("Occlusion is only valid for a PBR material");
    if (!unit(value))
        return Result<void>::failure("Material occlusion strength must be finite and in [0, 1]");
    parameters_.occlusionStrength = value;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setEmissive(Color color) {
    if (type_ != MaterialType::PbrMetallicRoughness)
        return Result<void>::failure("Emissive is only valid for a PBR material");
    if (!validColor(color, true))
        return Result<void>::failure("Material emissive color must be finite and nonnegative");
    parameters_.emissive = color;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setAlphaMode(AlphaMode mode, float cutoff) {
    if (mode != AlphaMode::Opaque && mode != AlphaMode::Mask && mode != AlphaMode::Blend)
        return Result<void>::failure("Material alpha mode is invalid");
    if (!unit(cutoff))
        return Result<void>::failure("Material alpha cutoff must be finite and in [0, 1]");
    parameters_.alphaMode = mode;
    parameters_.alphaCutoff = cutoff;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setSampler(MaterialSampler sampler) {
    parameters_.sampler = sampler;
    ++version_;
    return {};
}

Result<void> MaterialInstance::setTexture(MaterialTextureSlot slot, MaterialTexturePtr texture) {
    if (!validSlot(slot))
        return Result<void>::failure("Material texture slot is invalid");
    if (type_ == MaterialType::Unlit && slot != MaterialTextureSlot::BaseColor)
        return Result<void>::failure("Unlit material only accepts a base color texture");
    if (texture && texture->colorSpace() != requiredColorSpace(slot))
        return Result<void>::failure("Material texture color space is incompatible with its slot");
    textures_[slotIndex(slot)] = std::move(texture);
    ++version_;
    return {};
}

MaterialTexturePtr MaterialInstance::texture(MaterialTextureSlot slot) const {
    return validSlot(slot) ? textures_[slotIndex(slot)] : MaterialTexturePtr{};
}

MaterialTexturePtr MaterialInstance::resolvedTexture(MaterialTextureSlot slot) const {
    auto selected = texture(slot);
    return selected ? selected : defaultMaterialTexture(slot);
}

std::shared_ptr<MaterialInstance> MaterialInstance::clone() const {
    return std::make_shared<MaterialInstance>(*this);
}

MaterialTexturePtr defaultMaterialTexture(MaterialTextureSlot slot) {
    static const Array<MaterialTexturePtr, 5> defaults = {
        MaterialTexture::create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255}).value,
        MaterialTexture::create(1, 1, TextureColorSpace::Linear, {0, 255, 255, 255}).value,
        MaterialTexture::create(1, 1, TextureColorSpace::Linear, {128, 128, 255, 255}).value,
        MaterialTexture::create(1, 1, TextureColorSpace::Linear, {255, 255, 255, 255}).value,
        MaterialTexture::create(1, 1, TextureColorSpace::Srgb, {255, 255, 255, 255}).value,
    };
    return validSlot(slot) ? defaults[slotIndex(slot)] : MaterialTexturePtr{};
}
} // namespace ursa
