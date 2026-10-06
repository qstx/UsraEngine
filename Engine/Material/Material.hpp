#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Material/Color.hpp>
#include <cstdint>
#include <memory>

namespace Ursa
{
enum class MaterialType
{
	Unlit,
	PbrMetallicRoughness
};
enum class AlphaMode
{
	Opaque,
	Mask,
	Blend
};
enum class TextureColorSpace
{
	Linear,
	Srgb
};
enum class MaterialTextureSlot
{
	BaseColor,
	MetallicRoughness,
	Normal,
	Occlusion,
	Emissive
};

struct MaterialSampler
{
	bool linear = true;
	bool repeat = true;
};

class MaterialTexture final
{
public:
	static Result<std::shared_ptr<const MaterialTexture>> Create(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight,
	                                                             TextureColorSpace textureColorSpace,
	                                                             Vector<Ursa::UInt8> rgba8);

	Ursa::UInt32 GetWidth() const
	{
		return width;
	}

	Ursa::UInt32 GetHeight() const
	{
		return height;
	}

	TextureColorSpace GetColorSpace() const
	{
		return colorSpace;
	}

	const Vector<Ursa::UInt8>& GetPixels() const
	{
		return pixels;
	}

private:
	MaterialTexture(Ursa::UInt32 textureWidth, Ursa::UInt32 textureHeight, TextureColorSpace textureColorSpace,
	                Vector<Ursa::UInt8> texturePixels);

	Ursa::UInt32 width;
	Ursa::UInt32 height;
	TextureColorSpace colorSpace;
	Vector<Ursa::UInt8> pixels;
};

using MaterialTexturePtr = std::shared_ptr<const MaterialTexture>;

struct MaterialParameters
{
	Color baseColor{};
	Ursa::Float32 metallic = 0.0f;
	Ursa::Float32 roughness = 1.0f;
	Ursa::Float32 normalScale = 1.0f;
	Ursa::Float32 occlusionStrength = 1.0f;
	Color emissive{0.0f, 0.0f, 0.0f, 1.0f};
	AlphaMode alphaMode = AlphaMode::Opaque;
	Ursa::Float32 alphaCutoff = 0.5f;
	MaterialSampler sampler{};
};

class MaterialInstance final
{
public:
	explicit MaterialInstance(MaterialType materialType);

	MaterialType Type() const
	{
		return type;
	}

	Ursa::UInt64 Version() const
	{
		return version;
	}

	const MaterialParameters& Parameters() const
	{
		return parameters;
	}

	Result<void> SetBaseColor(Color color);

	Result<void> SetMetallic(Ursa::Float32 value);

	Result<void> SetRoughness(Ursa::Float32 value);

	Result<void> SetNormalScale(Ursa::Float32 value);

	Result<void> SetOcclusionStrength(Ursa::Float32 value);

	Result<void> SetEmissive(Color color);

	Result<void> SetAlphaMode(AlphaMode mode, Ursa::Float32 cutoff = 0.5f);

	Result<void> SetSampler(MaterialSampler sampler);

	Result<void> SetTexture(MaterialTextureSlot slot, MaterialTexturePtr texture);

	MaterialTexturePtr Texture(MaterialTextureSlot slot) const;

	MaterialTexturePtr ResolvedTexture(MaterialTextureSlot slot) const;

	std::shared_ptr<MaterialInstance> Clone() const;

private:
	MaterialType type;
	MaterialParameters parameters;
	Array<MaterialTexturePtr, 5> textures{};
	Ursa::UInt64 version = 1;
};

MaterialTexturePtr DefaultMaterialTexture(MaterialTextureSlot slot);
} // namespace Ursa
