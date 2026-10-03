#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <material/Color.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Ursa
{
enum class MaterialType
{
	unlit,
	pbrMetallicRoughness
};
enum class AlphaMode
{
	opaque,
	mask,
	blend
};
enum class TextureColorSpace
{
	linear,
	srgb
};
enum class MaterialTextureSlot
{
	baseColor,
	metallicRoughness,
	normal,
	occlusion,
	emissive
};

struct MaterialSampler
{
	bool linear = true;
	bool repeat = true;
};

class MaterialTexture final
{
public:
	static Result<std::shared_ptr<const MaterialTexture>> Create(uint32_t width, uint32_t height,
	                                                             TextureColorSpace colorSpace, Vector<uint8_t> rgba8);

	uint32_t GetWidth() const
	{
		return width;
	}

	uint32_t GetHeight() const
	{
		return height;
	}

	TextureColorSpace GetColorSpace() const
	{
		return colorSpace;
	}

	const Vector<uint8_t>& GetPixels() const
	{
		return pixels;
	}

private:
	MaterialTexture(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels);

	uint32_t width;
	uint32_t height;
	TextureColorSpace colorSpace;
	Vector<uint8_t> pixels;
};

using MaterialTexturePtr = std::shared_ptr<const MaterialTexture>;

struct MaterialParameters
{
	Color baseColor{};
	float metallic = 0.0f;
	float roughness = 1.0f;
	float normalScale = 1.0f;
	float occlusionStrength = 1.0f;
	Color emissive{0.0f, 0.0f, 0.0f, 1.0f};
	AlphaMode alphaMode = AlphaMode::opaque;
	float alphaCutoff = 0.5f;
	MaterialSampler sampler{};
};

class MaterialInstance final
{
public:
	explicit MaterialInstance(MaterialType type);

	MaterialType Type() const
	{
		return type;
	}

	uint64_t Version() const
	{
		return version;
	}

	const MaterialParameters& Parameters() const
	{
		return parameters;
	}

	Result<void> SetBaseColor(Color color);

	Result<void> SetMetallic(float value);

	Result<void> SetRoughness(float value);

	Result<void> SetNormalScale(float value);

	Result<void> SetOcclusionStrength(float value);

	Result<void> SetEmissive(Color color);

	Result<void> SetAlphaMode(AlphaMode mode, float cutoff = 0.5f);

	Result<void> SetSampler(MaterialSampler sampler);

	Result<void> SetTexture(MaterialTextureSlot slot, MaterialTexturePtr texture);

	MaterialTexturePtr Texture(MaterialTextureSlot slot) const;

	MaterialTexturePtr ResolvedTexture(MaterialTextureSlot slot) const;

	std::shared_ptr<MaterialInstance> Clone() const;

private:
	MaterialType type;
	MaterialParameters parameters;
	Array<MaterialTexturePtr, 5> textures{};
	uint64_t version = 1;
};

MaterialTexturePtr DefaultMaterialTexture(MaterialTextureSlot slot);
} // namespace Ursa
