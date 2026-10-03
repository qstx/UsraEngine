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

namespace
{
template <class T> T Take(Ursa::Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Ursa::Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(Ursa::ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void TestParameters()
{
	Ursa::MaterialInstance pbr(Ursa::MaterialType::pbrMetallicRoughness);
	done(pbr.SetBaseColor({0.3f, 0.6f, 0.2f, 0.5f}));
	done(pbr.SetMetallic(0.7f));
	done(pbr.SetRoughness(0.2f));
	done(pbr.SetNormalScale(1.4f));
	done(pbr.SetOcclusionStrength(0.8f));
	done(pbr.SetEmissive({2.0f, 0.2f, 0.0f, 1.0f}));
	done(pbr.SetAlphaMode(Ursa::AlphaMode::mask, 0.35f));
	Expect(pbr.Parameters().metallic == 0.7f && pbr.Parameters().roughness == 0.2f &&
	           pbr.Parameters().normalScale == 1.4f && pbr.Parameters().occlusionStrength == 0.8f &&
	           pbr.Parameters().emissive.red == 2.0f && pbr.Parameters().alphaCutoff == 0.35f,
	       "Valid PBR parameters were not stored");
	const uint64_t version = pbr.Version();
	const auto badMetallic = pbr.SetMetallic(-0.1f);
	const auto badRoughness = pbr.SetRoughness(std::numeric_limits<float>::quiet_NaN());
	const auto badCutoff = pbr.SetAlphaMode(Ursa::AlphaMode::blend, 1.1f);
	Expect(!badMetallic && badMetallic.error.find("metallic") != Ursa::String::npos,
	       "Invalid metallic lacked a diagnostic");
	Expect(!badRoughness && badRoughness.error.find("roughness") != Ursa::String::npos,
	       "Invalid roughness lacked a diagnostic");
	Expect(!badCutoff && badCutoff.error.find("cutoff") != Ursa::String::npos,
	       "Invalid alpha cutoff lacked a diagnostic");
	Expect(pbr.Parameters().metallic == 0.7f && pbr.Parameters().roughness == 0.2f &&
	           pbr.Parameters().alphaMode == Ursa::AlphaMode::mask && pbr.Version() == version,
	       "Rejected material parameters changed the instance");
	Ursa::MaterialInstance unlit(Ursa::MaterialType::unlit);
	Expect(!unlit.SetMetallic(0.5f) && !unlit.SetNormalScale(1.0f), "Unlit accepted PBR-only parameters");
}

void TestSharingAndClone()
{
	auto shared = std::make_shared<Ursa::MaterialInstance>(Ursa::MaterialType::unlit);
	auto first = shared;
	auto second = shared;
	const uint64_t originalVersion = shared->Version();
	done(first->SetBaseColor({1.0f, 0.0f, 0.0f, 1.0f}));
	Expect(second->Parameters().baseColor.red == 1.0f && second->Parameters().baseColor.green == 0.0f &&
	           second->Version() == originalVersion + 1,
	       "Shared material did not observe a modification");
	auto independent = shared->Clone();
	done(independent->SetBaseColor({0.0f, 0.0f, 1.0f, 1.0f}));
	Expect(independent->Parameters().baseColor.blue == 1.0f && shared->Parameters().baseColor.red == 1.0f &&
	           independent->Version() == shared->Version() + 1,
	       "Material clone was not independent");
}

void TestProceduralTextures()
{
	const auto invalid = Ursa::MaterialTexture::Create(2, 2, Ursa::TextureColorSpace::srgb, {1, 2, 3, 4});
	Expect(!invalid && invalid.error.find("pixel") != Ursa::String::npos,
	       "Incorrect pixel byte count lacked a diagnostic");
	Expect(!Ursa::MaterialTexture::Create(0, 1, Ursa::TextureColorSpace::srgb, {}), "Zero-width texture was accepted");
	auto color =
	    Take(Ursa::MaterialTexture::Create(2, 1, Ursa::TextureColorSpace::srgb, {255, 0, 0, 255, 0, 255, 0, 128}));
	auto data = Take(Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::linear, {128, 128, 255, 255}));
	Ursa::MaterialInstance pbr(Ursa::MaterialType::pbrMetallicRoughness);
	done(pbr.SetTexture(Ursa::MaterialTextureSlot::baseColor, color));
	done(pbr.SetTexture(Ursa::MaterialTextureSlot::normal, data));
	const uint64_t version = pbr.Version();
	const auto mismatch = pbr.SetTexture(Ursa::MaterialTextureSlot::normal, color);
	Expect(!mismatch && mismatch.error.find("color space") != Ursa::String::npos && pbr.Version() == version &&
	           pbr.Texture(Ursa::MaterialTextureSlot::normal) == data,
	       "Wrong texture color space changed the binding");
	Expect(pbr.ResolvedTexture(Ursa::MaterialTextureSlot::occlusion)->GetPixels()[0] == 255 &&
	           pbr.ResolvedTexture(Ursa::MaterialTextureSlot::metallicRoughness)->GetPixels()[1] == 255 &&
	           pbr.ResolvedTexture(Ursa::MaterialTextureSlot::metallicRoughness)->GetPixels()[2] == 255 &&
	           pbr.ResolvedTexture(Ursa::MaterialTextureSlot::emissive)->GetPixels()[0] == 255,
	       "Default material textures have incorrect values");
	Ursa::MaterialInstance unlit(Ursa::MaterialType::unlit);
	Expect(!unlit.SetTexture(Ursa::MaterialTextureSlot::normal, data), "Unlit accepted a normal texture");
}
} // namespace

int main()
{
	try
	{
		TestParameters();
		TestSharingAndClone();
		TestProceduralTextures();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
