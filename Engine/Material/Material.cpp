#include <Material/Material.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace Ursa
{
namespace
{
bool unit(float value)
{
	return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool Nonnegative(float value)
{
	return std::isfinite(value) && value >= 0.0f;
}

bool ValidColor(Color color, bool hdr)
{
	const auto valid = hdr ? Nonnegative : unit;
	return valid(color.red) && valid(color.green) && valid(color.blue) && unit(color.alpha);
}

size_t SlotIndex(MaterialTextureSlot slot)
{
	return static_cast<size_t>(slot);
}

bool ValidSlot(MaterialTextureSlot slot)
{
	return SlotIndex(slot) < 5;
}

TextureColorSpace RequiredColorSpace(MaterialTextureSlot slot)
{
	return slot == MaterialTextureSlot::baseColor || slot == MaterialTextureSlot::emissive ? TextureColorSpace::srgb
	                                                                                       : TextureColorSpace::linear;
}
} // namespace

MaterialTexture::MaterialTexture(uint32_t width, uint32_t height, TextureColorSpace colorSpace, Vector<uint8_t> pixels)
    : width(width), height(height), colorSpace(colorSpace), pixels(std::move(pixels))
{
}

Result<MaterialTexturePtr> MaterialTexture::Create(uint32_t width, uint32_t height, TextureColorSpace colorSpace,
                                                   Vector<uint8_t> rgba8)
{
	if (!width || !height || static_cast<uint64_t>(width) * height > std::numeric_limits<size_t>::max() / 4 ||
	    rgba8.size() != static_cast<size_t>(width) * height * 4)
	{
		return Result<MaterialTexturePtr>::Failure("Material texture dimensions or RGBA8 pixel byte count are invalid");
	}
	if (colorSpace != TextureColorSpace::linear && colorSpace != TextureColorSpace::srgb)
	{
		return Result<MaterialTexturePtr>::Failure("Material texture color space is invalid");
	}
	return Result<MaterialTexturePtr>::Success(
	    MaterialTexturePtr(new MaterialTexture(width, height, colorSpace, std::move(rgba8))));
}

MaterialInstance::MaterialInstance(MaterialType type) : type(type)
{
}

Result<void> MaterialInstance::SetBaseColor(Color color)
{
	if (!ValidColor(color, false))
	{
		return Result<void>::Failure("Material baseColor must have finite components in [0, 1]");
	}
	parameters.baseColor = color;
	++version;
	return {};
}

Result<void> MaterialInstance::SetMetallic(float value)
{
	if (type != MaterialType::pbrMetallicRoughness)
	{
		return Result<void>::Failure("Metallic is only valid for a PBR material");
	}
	if (!unit(value))
	{
		return Result<void>::Failure("Material metallic must be finite and in [0, 1]");
	}
	parameters.metallic = value;
	++version;
	return {};
}

Result<void> MaterialInstance::SetRoughness(float value)
{
	if (type != MaterialType::pbrMetallicRoughness)
	{
		return Result<void>::Failure("Roughness is only valid for a PBR material");
	}
	if (!unit(value))
	{
		return Result<void>::Failure("Material roughness must be finite and in [0, 1]");
	}
	parameters.roughness = value;
	++version;
	return {};
}

Result<void> MaterialInstance::SetNormalScale(float value)
{
	if (type != MaterialType::pbrMetallicRoughness)
	{
		return Result<void>::Failure("Normal scale is only valid for a PBR material");
	}
	if (!Nonnegative(value))
	{
		return Result<void>::Failure("Material normal scale must be finite and nonnegative");
	}
	parameters.normalScale = value;
	++version;
	return {};
}

Result<void> MaterialInstance::SetOcclusionStrength(float value)
{
	if (type != MaterialType::pbrMetallicRoughness)
	{
		return Result<void>::Failure("Occlusion is only valid for a PBR material");
	}
	if (!unit(value))
	{
		return Result<void>::Failure("Material occlusion strength must be finite and in [0, 1]");
	}
	parameters.occlusionStrength = value;
	++version;
	return {};
}

Result<void> MaterialInstance::SetEmissive(Color color)
{
	if (type != MaterialType::pbrMetallicRoughness)
	{
		return Result<void>::Failure("Emissive is only valid for a PBR material");
	}
	if (!ValidColor(color, true))
	{
		return Result<void>::Failure("Material emissive color must be finite and nonnegative");
	}
	parameters.emissive = color;
	++version;
	return {};
}

Result<void> MaterialInstance::SetAlphaMode(AlphaMode mode, float cutoff)
{
	if (mode != AlphaMode::opaque && mode != AlphaMode::mask && mode != AlphaMode::blend)
	{
		return Result<void>::Failure("Material alpha mode is invalid");
	}
	if (!unit(cutoff))
	{
		return Result<void>::Failure("Material alpha cutoff must be finite and in [0, 1]");
	}
	parameters.alphaMode = mode;
	parameters.alphaCutoff = cutoff;
	++version;
	return {};
}

Result<void> MaterialInstance::SetSampler(MaterialSampler sampler)
{
	parameters.sampler = sampler;
	++version;
	return {};
}

Result<void> MaterialInstance::SetTexture(MaterialTextureSlot slot, MaterialTexturePtr texture)
{
	if (!ValidSlot(slot))
	{
		return Result<void>::Failure("Material texture slot is invalid");
	}
	if (type == MaterialType::unlit && slot != MaterialTextureSlot::baseColor)
	{
		return Result<void>::Failure("Unlit material only accepts a base color texture");
	}
	if (texture && texture->GetColorSpace() != RequiredColorSpace(slot))
	{
		return Result<void>::Failure("Material texture color space is incompatible with its slot");
	}
	textures[SlotIndex(slot)] = std::move(texture);
	++version;
	return {};
}

MaterialTexturePtr MaterialInstance::Texture(MaterialTextureSlot slot) const
{
	return ValidSlot(slot) ? textures[SlotIndex(slot)] : MaterialTexturePtr{};
}

MaterialTexturePtr MaterialInstance::ResolvedTexture(MaterialTextureSlot slot) const
{
	auto selected = Texture(slot);
	return selected ? selected : DefaultMaterialTexture(slot);
}

std::shared_ptr<MaterialInstance> MaterialInstance::Clone() const
{
	return std::make_shared<MaterialInstance>(*this);
}

MaterialTexturePtr DefaultMaterialTexture(MaterialTextureSlot slot)
{
	static const Array<MaterialTexturePtr, 5> defaults = {
	    MaterialTexture::Create(1, 1, TextureColorSpace::srgb, {255, 255, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::linear, {0, 255, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::linear, {128, 128, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::linear, {255, 255, 255, 255}).value,
	    MaterialTexture::Create(1, 1, TextureColorSpace::srgb, {255, 255, 255, 255}).value,
	};
	return ValidSlot(slot) ? defaults[SlotIndex(slot)] : MaterialTexturePtr{};
}
} // namespace Ursa
