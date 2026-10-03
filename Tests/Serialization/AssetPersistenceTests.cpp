#include <Core/StringInterop.hpp>
#include <Core/Containers.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/TextureResource.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void TestTexture(const std::filesystem::path& directory)
{
	const auto path = directory / "checker.ursatex";
	const auto id = Ursa::ResourceId::Generate();
	const Ursa::Vector<uint8_t> pixels{255, 30, 20, 255, 10, 200, 80, 128, 0, 0, 0, 0, 45, 55, 230, 240};
	const auto texture = Ursa::MaterialTexture::Create(2, 2, Ursa::TextureColorSpace::srgb, pixels);
	Expect(static_cast<bool>(texture), "Cannot create source texture");
	Expect(static_cast<bool>(Ursa::SaveTextureResource(path, id, *texture.value)), "Cannot save texture resource");
	const auto restored = Ursa::LoadTextureResource(path);
	Expect(restored && restored.value.id == id && restored.value.texture->GetWidth() == 2 &&
	           restored.value.texture->GetHeight() == 2 &&
	           restored.value.texture->GetColorSpace() == Ursa::TextureColorSpace::srgb &&
	           restored.value.texture->GetPixels() == pixels,
	       "Texture resource roundtrip lost data");

	std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
	const auto truncated = Ursa::LoadTextureResource(path);
	Expect(!truncated && truncated.error.find("byte count") != Ursa::String::npos,
	       "Truncated texture resource was accepted");
	std::filesystem::remove(path);
}

void TestMaterial(const std::filesystem::path& directory)
{
	const auto colorPath = directory / "color.ursatex";
	const auto normalPath = directory / "normal.ursatex";
	const auto materialPath = directory / "surface.ursamat";
	const auto materialId = Ursa::ResourceId::Generate();
	const auto colorId = Ursa::ResourceId::Generate();
	const auto normalId = Ursa::ResourceId::Generate();
	auto color = Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::srgb, {210, 80, 40, 200});
	auto normal = Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::linear, {128, 128, 255, 255});
	Expect(color && normal, "Could not create test textures");
	Expect(static_cast<bool>(Ursa::SaveTextureResource(colorPath, colorId, *color.value)) &&
	           static_cast<bool>(Ursa::SaveTextureResource(normalPath, normalId, *normal.value)),
	       "Could not save material textures");
	Ursa::MaterialInstance source(Ursa::MaterialType::pbrMetallicRoughness);
	Expect(static_cast<bool>(source.SetBaseColor({0.8f, 0.3f, 0.2f, 0.7f})) &&
	           static_cast<bool>(source.SetMetallic(0.35f)) && static_cast<bool>(source.SetRoughness(0.62f)) &&
	           static_cast<bool>(source.SetNormalScale(1.4f)) && static_cast<bool>(source.SetOcclusionStrength(0.5f)) &&
	           static_cast<bool>(source.SetEmissive({0.2f, 0.1f, 0.05f, 1.0f})) &&
	           static_cast<bool>(source.SetAlphaMode(Ursa::AlphaMode::blend, 0.4f)) &&
	           static_cast<bool>(source.SetSampler({false, false})) &&
	           static_cast<bool>(source.SetTexture(Ursa::MaterialTextureSlot::baseColor, color.value)) &&
	           static_cast<bool>(source.SetTexture(Ursa::MaterialTextureSlot::normal, normal.value)),
	       "Could not configure source material");
	const auto saved = Ursa::SaveMaterialResource(materialPath, materialId, source,
	                                              [&](Ursa::MaterialTexturePtr texture)
	                                              {
		                                              if (texture == color.value)
		                                              {
			                                              return Ursa::Result<Ursa::ResourceId>::Success(colorId);
		                                              }
		                                              if (texture == normal.value)
		                                              {
			                                              return Ursa::Result<Ursa::ResourceId>::Success(normalId);
		                                              }
		                                              return Ursa::Result<Ursa::ResourceId>::Failure("Unknown texture");
	                                              });
	Expect(static_cast<bool>(saved), "Could not save PBR material");
	const auto restored =
	    Ursa::LoadMaterialResource(materialPath,
	                               [&](Ursa::ResourceId id)
	                               {
		                               auto loaded = Ursa::LoadTextureResource(id == colorId ? colorPath : normalPath);
		                               if (!loaded || loaded.value.id != id)
		                               {
			                               return Ursa::Result<Ursa::MaterialTexturePtr>::Failure("Wrong texture ID");
		                               }
		                               return Ursa::Result<Ursa::MaterialTexturePtr>::Success(loaded.value.texture);
	                               });
	Expect(restored && restored.value.id == materialId &&
	           restored.value.material->Type() == Ursa::MaterialType::pbrMetallicRoughness &&
	           restored.value.material->Parameters().baseColor.red == 0.8f &&
	           restored.value.material->Parameters().metallic == 0.35f &&
	           restored.value.material->Parameters().roughness == 0.62f &&
	           restored.value.material->Parameters().normalScale == 1.4f &&
	           restored.value.material->Parameters().occlusionStrength == 0.5f &&
	           restored.value.material->Parameters().emissive.red == 0.2f &&
	           restored.value.material->Parameters().alphaMode == Ursa::AlphaMode::blend &&
	           restored.value.material->Parameters().alphaCutoff == 0.4f &&
	           !restored.value.material->Parameters().sampler.linear &&
	           !restored.value.material->Parameters().sampler.repeat &&
	           restored.value.material->Texture(Ursa::MaterialTextureSlot::baseColor)->GetPixels() ==
	               color.value->GetPixels() &&
	           restored.value.material->Texture(Ursa::MaterialTextureSlot::normal)->GetPixels() ==
	               normal.value->GetPixels(),
	       "PBR material roundtrip lost data");

	const auto invalid = directory / "bad.ursamat";
	{
		std::ofstream output(invalid);
		output << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << Ursa::ToStdString(materialId.ToString())
		       << "\",\"type\":\"Unlit\",\"parameters\":{\"baseColor\":[1,1,1,1],"
		          "\"alphaMode\":\"Opaque\",\"alphaCutoff\":0.5,\"sampler\":{\"linear\":true,"
		          "\"repeat\":true}},\"textures\":{\"normal\":\""
		       << Ursa::ToStdString(normalId.ToString()) << "\"}}";
	}
	const auto rejected = Ursa::LoadMaterialResource(invalid, {});
	Expect(!rejected && rejected.error.find("normal") != Ursa::String::npos, "Unlit accepted a normal texture");
	const auto invalidRange = directory / "bad-range.ursamat";
	{
		std::ofstream output(invalidRange);
		output << "{\"format\":\"UrsaMaterial\",\"version\":1,\"id\":\"" << Ursa::ToStdString(materialId.ToString())
		       << "\",\"type\":\"PbrMetallicRoughness\",\"parameters\":{\"baseColor\":[1,1,1,1],"
		          "\"alphaMode\":\"Opaque\",\"alphaCutoff\":0.5,\"sampler\":{\"linear\":true,"
		          "\"repeat\":true},\"metallic\":0,\"roughness\":2,\"normalScale\":1,"
		          "\"occlusionStrength\":1,\"emissive\":[0,0,0,1]},\"textures\":{}}";
	}
	const auto rejectedRange = Ursa::LoadMaterialResource(invalidRange, {});
	Expect(!rejectedRange && rejectedRange.error.find("roughness") != Ursa::String::npos,
	       "Out-of-range material parameter was accepted");
	std::filesystem::remove(colorPath);
	std::filesystem::remove(normalPath);
	std::filesystem::remove(materialPath);
	std::filesystem::remove(invalid);
	std::filesystem::remove(invalidRange);
}

void TestSession(const std::filesystem::path& directory)
{
	const auto root = directory / "session";
	std::filesystem::create_directory(root);
	const auto textureId = Ursa::ResourceId::Generate();
	const auto firstId = Ursa::ResourceId::Generate();
	const auto secondId = Ursa::ResourceId::Generate();
	const auto texturePath = root / "shared.ursatex";
	const auto firstPath = root / "first.ursamat";
	const auto secondPath = root / "second.ursamat";
	auto red = Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::srgb, {255, 0, 0, 255});
	Expect(red && static_cast<bool>(Ursa::SaveTextureResource(texturePath, textureId, *red.value)),
	       "Could not save shared texture");
	Ursa::MaterialInstance source(Ursa::MaterialType::unlit);
	Expect(static_cast<bool>(source.SetTexture(Ursa::MaterialTextureSlot::baseColor, red.value)),
	       "Could not bind shared texture");
	auto resolve = [&](Ursa::MaterialTexturePtr selected)
	{
		return selected == red.value ? Ursa::Result<Ursa::ResourceId>::Success(textureId)
		                             : Ursa::Result<Ursa::ResourceId>::Failure("Unexpected texture");
	};
	Expect(static_cast<bool>(Ursa::SaveMaterialResource(firstPath, firstId, source, resolve)) &&
	           static_cast<bool>(Ursa::SaveMaterialResource(secondPath, secondId, source, resolve)),
	       "Could not save shared materials");
	auto opened = Ursa::ResourceSession::Open({root});
	Expect(static_cast<bool>(opened), "Could not open resource session");
	auto first = opened.value->LoadMaterial(firstId);
	auto firstAgain = opened.value->LoadMaterial(firstId);
	auto second = opened.value->LoadMaterial(secondId);
	Expect(first && firstAgain && second && first.value == firstAgain.value &&
	           first.value->Texture(Ursa::MaterialTextureSlot::baseColor) ==
	               second.value->Texture(Ursa::MaterialTextureSlot::baseColor),
	       "Resource session did not preserve shared identity");

	auto blue = Ursa::MaterialTexture::Create(1, 1, Ursa::TextureColorSpace::srgb, {0, 0, 255, 255});
	Expect(blue && static_cast<bool>(Ursa::SaveTextureResource(texturePath, textureId, *blue.value)),
	       "Could not update texture on disk");
	auto refreshed = opened.value->Refreshed();
	Expect(static_cast<bool>(refreshed), "Could not refresh resource session");
	auto reloaded = refreshed.value->LoadMaterial(firstId);
	Expect(reloaded && reloaded.value != first.value &&
	           reloaded.value->Texture(Ursa::MaterialTextureSlot::baseColor)->GetPixels()[0] == 0 &&
	           first.value->Texture(Ursa::MaterialTextureSlot::baseColor)->GetPixels()[0] == 255,
	       "Fresh session failed to see disk update or modified old resource");
	std::filesystem::remove(texturePath);
	std::filesystem::remove(firstPath);
	std::filesystem::remove(secondPath);
	std::filesystem::remove(root);
}
} // namespace

int main()
{
	try
	{
		const auto directory =
		    std::filesystem::temp_directory_path() /
		    ("UrsaAssetPersistenceTests-" + Ursa::ToStdString(Ursa::ResourceId::Generate().ToString()));
		std::filesystem::create_directory(directory);
		TestTexture(directory);
		TestMaterial(directory);
		TestSession(directory);
		std::filesystem::remove(directory);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
