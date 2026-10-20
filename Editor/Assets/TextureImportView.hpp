#pragma once

#include <Assets/TextureImporter.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <functional>

namespace Ursa::Editor
{
class TextureImportView final
{
public:
	static Result<std::unique_ptr<TextureImportView>> Open(VirtualPath source, TextureImportSettings settings);

	Result<void> Reimport(AssetDatabase& database, Rhi::DevicePtr device, ImGuiBridge& gui, bool cachedOnly = false);

	Result<void> Draw(AssetDatabase& database, Rhi::DevicePtr device, ImGuiBridge& gui, bool canReimport = true,
	                  const std::function<void(const VirtualPath&)>& requestImport = {});

	void Close(ImGuiBridge& gui);

	const VirtualPath& SourcePath() const
	{
		return sourcePath;
	}

	const TextureImportSettings& Settings() const
	{
		return settings;
	}

	const String& Diagnostic() const
	{
		return diagnostic;
	}

	bool HasPreview() const
	{
		return sourceTexture && hasImported && imported.texture;
	}

	const MaterialTexturePtr& SourceTexture() const
	{
		return sourceTexture;
	}

	MaterialTexturePtr ImportedTexture() const
	{
		return hasImported ? imported.texture : nullptr;
	}

private:
	TextureImportView(VirtualPath source, TextureImportSettings sourceSettings)
	    : sourcePath(std::move(source)), settings(sourceSettings)
	{
	}

	VirtualPath sourcePath;
	TextureImportSettings settings;
	TextureImportResult imported;
	MaterialTexturePtr sourceTexture;
	String diagnostic;
	bool hasImported = false;
	bool dirty = false;
};
} // namespace Ursa::Editor
