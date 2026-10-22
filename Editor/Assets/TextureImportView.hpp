#pragma once

#include <Assets/TextureImporter.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <functional>

namespace Ursa::Editor
{
class ProjectAssetImporter;
class AssetImporterInspector;

class TextureImportView final
{
public:
	static Result<std::unique_ptr<TextureImportView>> Open(VirtualPath source, TextureImportSettings settings);

	Result<void> Reimport(AssetDatabase& database, ProjectAssetImporter& importer, Rhi::DevicePtr device,
	                      ImGuiBridge& gui, bool cachedOnly = false);

	Result<void> Draw(AssetDatabase& database, ProjectAssetImporter& importer, Rhi::DevicePtr device, ImGuiBridge& gui,
	                  bool canReimport = true, const std::function<void(const VirtualPath&)>& requestImport = {},
	                  ProjectWorkspace* workspace = nullptr, const AssetImporterInspector::CustomDraw& custom = {},
	                  bool embedded = false);

	void SetSettingsInspector(std::shared_ptr<AssetImporterInspector> value)
	{
		settingsInspector = std::move(value);
	}

	void Close(ImGuiBridge& gui);
	Result<std::shared_ptr<AssetImporterInspector>> SettingsInspector(AssetDatabase& database);
	bool HasModifiedSettings() const;

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
	std::shared_ptr<AssetImporterInspector> settingsInspector;
};
} // namespace Ursa::Editor
