#include <Assets/TextureImporter.hpp>

#include <Core/StringInterop.hpp>
#include <Serialization/TextureResource.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stb_image.h>
#include <stb_image_resize2.h>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

constexpr Ursa::UInt32 textureImporterVersion = 1;
constexpr Ursa::UInt32 maxTextureImportDimension = 16384;
const String textureImporterId = "ursa.Texture";

String TextureTypeName(TextureImportType type)
{
	switch (type)
	{
		case TextureImportType::Default:
			return "Default";
		case TextureImportType::NormalMap:
			return "NormalMap";
		case TextureImportType::LinearData:
			return "LinearData";
	}
	return {};
}

Result<TextureImportType> ParseTextureType(const String& value)
{
	if (value == "Default")
	{
		return Result<TextureImportType>::Success(TextureImportType::Default);
	}
	if (value == "NormalMap")
	{
		return Result<TextureImportType>::Success(TextureImportType::NormalMap);
	}
	if (value == "LinearData")
	{
		return Result<TextureImportType>::Success(TextureImportType::LinearData);
	}
	return Result<TextureImportType>::Failure("Unknown texture import type " + value);
}

Result<void> ValidateSettings(const TextureImportSettings& settings)
{
	if (TextureTypeName(settings.type).empty() || settings.maxDimension == 0 ||
	    settings.maxDimension > maxTextureImportDimension ||
	    (settings.type != TextureImportType::Default && settings.sRgb))
	{
		return Result<void>::Failure("Invalid texture import settings");
	}
	return {};
}

bool IsSupportedImage(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return false;
	}
	String extension = path.relative.substr(dot);
	for (char& value : extension)
	{
		if (value >= 'A' && value <= 'Z')
		{
			value = static_cast<char>(value - 'A' + 'a');
		}
	}
	return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

Result<Vector<Ursa::UInt8>> ResizePixels(const Vector<Ursa::UInt8>& source, Ursa::UInt32 sourceWidth,
                                         Ursa::UInt32 sourceHeight, Ursa::UInt32 targetWidth, Ursa::UInt32 targetHeight,
                                         bool sRgb)
{
	Vector<Ursa::UInt8> result(static_cast<Ursa::Size>(targetWidth) * targetHeight * 4);
	unsigned char* resized =
	    sRgb ? stbir_resize_uint8_srgb(source.data(), static_cast<int>(sourceWidth), static_cast<int>(sourceHeight), 0,
	                                   result.data(), static_cast<int>(targetWidth), static_cast<int>(targetHeight), 0,
	                                   STBIR_RGBA)
	         : stbir_resize_uint8_linear(source.data(), static_cast<int>(sourceWidth), static_cast<int>(sourceHeight),
	                                     0, result.data(), static_cast<int>(targetWidth),
	                                     static_cast<int>(targetHeight), 0, STBIR_RGBA);
	if (!resized)
	{
		return Result<Vector<Ursa::UInt8>>::Failure("Image resize failed");
	}
	return Result<Vector<Ursa::UInt8>>::Success(std::move(result));
}

void NormalizeNormalMap(Vector<Ursa::UInt8>& pixels)
{
	for (Ursa::Size offset = 0; offset + 3 < pixels.size(); offset += 4)
	{
		Ursa::Float32 x = static_cast<Ursa::Float32>(pixels[offset]) / 127.5f - 1.0f;
		Ursa::Float32 y = static_cast<Ursa::Float32>(pixels[offset + 1]) / 127.5f - 1.0f;
		Ursa::Float32 z = static_cast<Ursa::Float32>(pixels[offset + 2]) / 127.5f - 1.0f;
		const Ursa::Float32 length = std::sqrt(x * x + y * y + z * z);
		if (length > 0.00001f)
		{
			x /= length;
			y /= length;
			z /= length;
		}
		pixels[offset] = static_cast<Ursa::UInt8>(std::clamp((x * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
		pixels[offset + 1] = static_cast<Ursa::UInt8>(std::clamp((y * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
		pixels[offset + 2] = static_cast<Ursa::UInt8>(std::clamp((z * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
	}
}

Result<MaterialTexturePtr> DecodeTexture(const VirtualFileSystem& filesystem, const VirtualPath& source,
                                         const TextureImportSettings& settings)
{
	auto encoded = filesystem.Read(source);
	if (!encoded)
	{
		return Result<MaterialTexturePtr>::Failure(encoded.error);
	}
	if (encoded.value.empty() || encoded.value.size() > static_cast<Ursa::Size>((std::numeric_limits<int>::max)()))
	{
		return Result<MaterialTexturePtr>::Failure("Image source is empty or too large to decode at " +
		                                           source.ToString());
	}
	int decodedWidth = 0;
	int decodedHeight = 0;
	int sourceChannels = 0;
	int sourceWidth = 0;
	int sourceHeight = 0;
	int sourceComponentCount = 0;
	if (!stbi_info_from_memory(encoded.value.data(), static_cast<int>(encoded.value.size()), &sourceWidth,
	                           &sourceHeight, &sourceComponentCount) ||
	    sourceWidth <= 0 || sourceHeight <= 0 ||
	    static_cast<Ursa::UInt64>(sourceWidth) * static_cast<Ursa::UInt64>(sourceHeight) > 256ull * 1024 * 1024 / 4)
	{
		return Result<MaterialTexturePtr>::Failure("Invalid image dimensions or header at " + source.ToString());
	}
	std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
	    stbi_load_from_memory(encoded.value.data(), static_cast<int>(encoded.value.size()), &decodedWidth,
	                          &decodedHeight, &sourceChannels, STBI_rgb_alpha),
	    &stbi_image_free);
	if (!decoded || decodedWidth <= 0 || decodedHeight <= 0)
	{
		const char* reason = stbi_failure_reason();
		return Result<MaterialTexturePtr>::Failure("Cannot decode image " + source.ToString() +
		                                           (reason ? ": " + ToUrsaString(reason) : String{}));
	}
	(void)sourceChannels;
	Ursa::UInt32 width = static_cast<Ursa::UInt32>(decodedWidth);
	Ursa::UInt32 height = static_cast<Ursa::UInt32>(decodedHeight);
	if (static_cast<Ursa::UInt64>(width) * height > 256ull * 1024 * 1024 / 4)
	{
		return Result<MaterialTexturePtr>::Failure("Decoded image exceeds the supported pixel count at " +
		                                           source.ToString());
	}
	Vector<Ursa::UInt8> pixels(static_cast<Ursa::Size>(width) * height * 4);
	std::copy(decoded.get(), decoded.get() + pixels.size(), pixels.begin());
	const Ursa::UInt32 largest = (std::max)(width, height);
	if (largest > settings.maxDimension)
	{
		const double scale = static_cast<double>(settings.maxDimension) / largest;
		const Ursa::UInt32 newWidth = (std::max)(1u, static_cast<Ursa::UInt32>(std::floor(width * scale + 0.5)));
		const Ursa::UInt32 newHeight = (std::max)(1u, static_cast<Ursa::UInt32>(std::floor(height * scale + 0.5)));
		auto resized = ResizePixels(pixels, width, height, newWidth, newHeight, settings.sRgb);
		if (!resized)
		{
			return Result<MaterialTexturePtr>::Failure(resized.error + " at " + source.ToString());
		}
		pixels = std::move(resized.value);
		width = newWidth;
		height = newHeight;
	}
	if (!settings.alpha)
	{
		for (Ursa::Size offset = 3; offset < pixels.size(); offset += 4)
		{
			pixels[offset] = 255;
		}
	}
	if (settings.type == TextureImportType::NormalMap)
	{
		NormalizeNormalMap(pixels);
	}

	Vector<TextureMip> mipmaps;
	if (settings.generateMipmaps)
	{
		Vector<Ursa::UInt8> previous = pixels;
		Ursa::UInt32 mipWidth = width;
		Ursa::UInt32 mipHeight = height;
		while (mipWidth > 1 || mipHeight > 1)
		{
			const Ursa::UInt32 nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
			const Ursa::UInt32 nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;
			auto resized = ResizePixels(previous, mipWidth, mipHeight, nextWidth, nextHeight, settings.sRgb);
			if (!resized)
			{
				return Result<MaterialTexturePtr>::Failure(resized.error + " while generating mipmaps at " +
				                                           source.ToString());
			}
			if (settings.type == TextureImportType::NormalMap)
			{
				NormalizeNormalMap(resized.value);
			}
			previous = resized.value;
			mipmaps.push_back({nextWidth, nextHeight, std::move(resized.value)});
			mipWidth = nextWidth;
			mipHeight = nextHeight;
		}
	}
	const TextureColorSpace colorSpace = settings.sRgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
	auto materialTexture = MaterialTexture::Create(
	    width, height, colorSpace, std::move(pixels), std::move(mipmaps),
	    {settings.filter == TextureFilterMode::Linear, settings.wrap == TextureWrapMode::Repeat});
	return materialTexture ? materialTexture
	                       : Result<MaterialTexturePtr>::Failure(materialTexture.error + " at " + source.ToString());
}
} // namespace

nlohmann::json TextureImportSettings::ToJson() const
{
	return {{"type", ToStdString(TextureTypeName(type))},
	        {"sRgb", sRgb},
	        {"alpha", alpha},
	        {"generateMipmaps", generateMipmaps},
	        {"filter", filter == TextureFilterMode::Point ? "Point" : "Linear"},
	        {"wrap", wrap == TextureWrapMode::Repeat ? "Repeat" : "Clamp"},
	        {"maxDimension", maxDimension}};
}

Result<TextureImportSettings> TextureImportSettings::FromJson(const nlohmann::json& value)
{
	if (!value.is_object())
	{
		return Result<TextureImportSettings>::Failure("Texture import settings must be an object");
	}
	TextureImportSettings settings;
	if (value.empty())
	{
		return Result<TextureImportSettings>::Success(settings);
	}
	if (!value.contains("type") || !value["type"].is_string() || !value.contains("sRgb") ||
	    !value["sRgb"].is_boolean() || !value.contains("alpha") || !value["alpha"].is_boolean() ||
	    !value.contains("generateMipmaps") || !value["generateMipmaps"].is_boolean() || !value.contains("filter") ||
	    !value["filter"].is_string() || !value.contains("wrap") || !value["wrap"].is_string() ||
	    !value.contains("maxDimension") || !value["maxDimension"].is_number_unsigned())
	{
		return Result<TextureImportSettings>::Failure("Texture import settings contain invalid or missing fields");
	}
	auto type = ParseTextureType(ToUrsaString(value["type"].get<std::string>()));
	if (!type)
	{
		return Result<TextureImportSettings>::Failure(type.error);
	}
	const std::string filter = value["filter"].get<std::string>();
	const std::string wrap = value["wrap"].get<std::string>();
	if (filter != "Point" && filter != "Linear")
	{
		return Result<TextureImportSettings>::Failure("Unknown texture filter mode");
	}
	if (wrap != "Repeat" && wrap != "Clamp")
	{
		return Result<TextureImportSettings>::Failure("Unknown texture wrap mode");
	}
	settings.type = type.value;
	settings.sRgb = value["sRgb"].get<bool>();
	settings.alpha = value["alpha"].get<bool>();
	settings.generateMipmaps = value["generateMipmaps"].get<bool>();
	settings.filter = filter == "Point" ? TextureFilterMode::Point : TextureFilterMode::Linear;
	settings.wrap = wrap == "Repeat" ? TextureWrapMode::Repeat : TextureWrapMode::Clamp;
	settings.maxDimension = value["maxDimension"].get<Ursa::UInt32>();
	auto valid = ValidateSettings(settings);
	return valid ? Result<TextureImportSettings>::Success(settings)
	             : Result<TextureImportSettings>::Failure(valid.error);
}

Result<MaterialTexturePtr> TextureImporter::DecodeSourceImage(const VirtualFileSystem& filesystem,
                                                              const VirtualPath& source)
{
	if (!IsSupportedImage(source))
	{
		return Result<MaterialTexturePtr>::Failure("Texture importer supports PNG and JPEG files: " +
		                                           source.ToString());
	}
	TextureImportSettings previewSettings;
	previewSettings.generateMipmaps = false;
	previewSettings.maxDimension = maxTextureImportDimension;
	return DecodeTexture(filesystem, source, previewSettings);
}

Result<TextureImportSettings> TextureImporter::Settings(const AssetDatabase& database, const VirtualPath& source)
{
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<TextureImportSettings>::Failure(metadata.error);
	}
	if (metadata.value.importer != textureImporterId)
	{
		return Result<TextureImportSettings>::Success(TextureImportSettings{});
	}
	return TextureImportSettings::FromJson(metadata.value.importSettings);
}

Result<void> TextureImporter::SetSettings(AssetDatabase& database, const VirtualPath& source,
                                          const TextureImportSettings& settings)
{
	auto valid = ValidateSettings(settings);
	if (!valid)
	{
		return valid;
	}
	return database.SetImporterSettings(source, textureImporterId, textureImporterVersion, settings.ToJson());
}

Result<TextureImportResult> TextureImporter::Import(AssetDatabase& database, const VirtualPath& source,
                                                    const TextureImportSettings& settings, const String& target)
{
	if (!IsSupportedImage(source))
	{
		return Result<TextureImportResult>::Failure("Texture importer supports PNG and JPEG files: " +
		                                            source.ToString());
	}
	auto valid = ValidateSettings(settings);
	if (!valid)
	{
		return Result<TextureImportResult>::Failure(valid.error);
	}
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<TextureImportResult>::Failure(metadata.error);
	}
	const Json serializedSettings = settings.ToJson();
	if (metadata.value.importer != textureImporterId || metadata.value.importerVersion != textureImporterVersion ||
	    metadata.value.importSettings != serializedSettings)
	{
		auto savedSettings =
		    database.SetImporterSettings(source, textureImporterId, textureImporterVersion, serializedSettings);
		if (!savedSettings)
		{
			return Result<TextureImportResult>::Failure(savedSettings.error);
		}
		metadata = database.Metadata(source);
		if (!metadata)
		{
			return Result<TextureImportResult>::Failure(metadata.error);
		}
	}
	auto cached =
	    database.CheckImport(source, textureImporterId, textureImporterVersion, target, serializedSettings, {});
	if (!cached)
	{
		return Result<TextureImportResult>::Failure(cached.error);
	}
	const ResourceId guid = metadata.value.guid;
	if (!cached.value.upToDate)
	{
		auto decoded = DecodeTexture(database.Filesystem(), source, settings);
		if (!decoded)
		{
			return Result<TextureImportResult>::Failure(decoded.error);
		}
		auto memory = std::make_shared<MemoryFileProvider>();
		VirtualFileSystem staging;
		auto mounted = staging.MountProvider(projectScheme, memory, MountAccess::ReadWrite);
		if (!mounted)
		{
			return Result<TextureImportResult>::Failure(mounted.error);
		}
		auto stagedPath = VirtualPath::Parse("prj:Imported.ursatex");
		if (!stagedPath)
		{
			return Result<TextureImportResult>::Failure(stagedPath.error);
		}
		auto saved = SaveTextureResource(staging, stagedPath.value, guid, *decoded.value);
		if (!saved)
		{
			return Result<TextureImportResult>::Failure(saved.error);
		}
		auto artifactBytes = staging.Read(stagedPath.value);
		if (!artifactBytes)
		{
			return Result<TextureImportResult>::Failure(artifactBytes.error);
		}
		Vector<ImportedArtifact> artifacts;
		artifacts.push_back({0, "Texture", std::move(artifactBytes.value)});
		auto published = database.PublishImport(source, textureImporterId, textureImporterVersion, target,
		                                        serializedSettings, {}, artifacts);
		if (!published)
		{
			return Result<TextureImportResult>::Failure(published.error);
		}
	}
	auto artifactPath = database.Resolve(guid, 0);
	if (!artifactPath)
	{
		return Result<TextureImportResult>::Failure(artifactPath.error);
	}
	auto loaded = LoadTextureResource(database.Filesystem(), artifactPath.value);
	if (!loaded)
	{
		return Result<TextureImportResult>::Failure(loaded.error);
	}
	return Result<TextureImportResult>::Success(
	    {guid, std::move(artifactPath.value), std::move(loaded.value.texture), cached.value.upToDate});
}
} // namespace Ursa::Editor
