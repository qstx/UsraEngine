#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <material/Color.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ursa {
enum class MaterialType { Unlit, PbrMetallicRoughness };
enum class AlphaMode { Opaque, Mask, Blend };
enum class TextureColorSpace { Linear, Srgb };
enum class MaterialTextureSlot { BaseColor, MetallicRoughness, Normal, Occlusion, Emissive };

struct MaterialSampler {
    bool linear = true;
    bool repeat = true;
};

class MaterialTexture final {
public:
    static Result<std::shared_ptr<const MaterialTexture>> create(uint32_t width, uint32_t height,
                                                                 TextureColorSpace colorSpace, Vector<uint8_t> rgba8);

    uint32_t width() const {
        return width_;
    }

    uint32_t height() const {
        return height_;
    }

    TextureColorSpace colorSpace() const {
        return colorSpace_;
    }

    const Vector<uint8_t> &pixels() const {
        return pixels_;
    }

private:
    MaterialTexture(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels);

    uint32_t width_;
    uint32_t height_;
    TextureColorSpace colorSpace_;
    Vector<uint8_t> pixels_;
};

using MaterialTexturePtr = std::shared_ptr<const MaterialTexture>;

struct MaterialParameters {
    Color baseColor{};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    Color emissive{0.0f, 0.0f, 0.0f, 1.0f};
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    MaterialSampler sampler{};
};

class MaterialInstance final {
public:
    explicit MaterialInstance(MaterialType type);

    MaterialType type() const {
        return type_;
    }

    uint64_t version() const {
        return version_;
    }

    const MaterialParameters &parameters() const {
        return parameters_;
    }

    Result<void> setBaseColor(Color color);

    Result<void> setMetallic(float value);

    Result<void> setRoughness(float value);

    Result<void> setNormalScale(float value);

    Result<void> setOcclusionStrength(float value);

    Result<void> setEmissive(Color color);

    Result<void> setAlphaMode(AlphaMode mode, float cutoff = 0.5f);

    Result<void> setSampler(MaterialSampler sampler);

    Result<void> setTexture(MaterialTextureSlot slot, MaterialTexturePtr texture);

    MaterialTexturePtr texture(MaterialTextureSlot slot) const;

    MaterialTexturePtr resolvedTexture(MaterialTextureSlot slot) const;

    std::shared_ptr<MaterialInstance> clone() const;

private:
    MaterialType type_;
    MaterialParameters parameters_;
    Array<MaterialTexturePtr, 5> textures_{};
    uint64_t version_ = 1;
};

MaterialTexturePtr defaultMaterialTexture(MaterialTextureSlot slot);
} // namespace ursa
