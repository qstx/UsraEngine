#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Material/Material.hpp>

namespace Ursa::Editor
{
enum class TextureImportType
{
	Default,
	NormalMap,
	LinearData
};

enum class TextureFilterMode
{
	Point,
	Linear
};

enum class TextureWrapMode
{
	Repeat,
	Clamp
};

struct TextureImportSettings
{
	TextureImportType type = TextureImportType::Default;
	bool sRgb = true;
	bool alpha = true;
	bool generateMipmaps = true;
	TextureFilterMode filter = TextureFilterMode::Linear;
	TextureWrapMode wrap = TextureWrapMode::Repeat;
	Ursa::UInt32 maxDimension = 4096;

	nlohmann::json ToJson() const;

	static Result<TextureImportSettings> FromJson(const nlohmann::json& value);
};

struct TextureImportResult
{
	ResourceId guid;
	VirtualPath artifactPath;
	MaterialTexturePtr texture;
	bool cacheHit = false;
};

class TextureImporter final
{
public:
	static Result<MaterialTexturePtr> DecodeSourceImage(const VirtualFileSystem& filesystem, const VirtualPath& source);

	static Result<TextureImportSettings> Settings(const AssetDatabase& database, const VirtualPath& source);

	static Result<void> SetSettings(AssetDatabase& database, const VirtualPath& source,
	                                const TextureImportSettings& settings);

	static Result<TextureImportResult> Import(AssetDatabase& database, const VirtualPath& source,
	                                          const TextureImportSettings& settings,
	                                          const String& target = "texture-rgba8", bool force = false);
};
} // namespace Ursa::Editor
