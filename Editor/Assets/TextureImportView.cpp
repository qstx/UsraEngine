#include <Ui/EditorIcons.hpp>
#include <Assets/TextureImportView.hpp>
#include <Assets/ProjectAssetImporter.hpp>
#include <Assets/AssetImporterInspector.hpp>
#include <Serialization/TextureResource.hpp>

#include <Core/StringInterop.hpp>
#include <algorithm>
#include <imgui.h>

namespace Ursa::Editor
{
namespace
{
String AssetDisplayPath(const VirtualPath& path)
{
	return path.scheme == projectScheme ? String("Assets/") + path.relative : path.ToString();
}

} // namespace

Result<std::unique_ptr<TextureImportView>> TextureImportView::Open(VirtualPath source,
                                                                   TextureImportSettings sourceSettings)
{
	if (source.scheme != projectScheme)
	{
		return Result<std::unique_ptr<TextureImportView>>::Failure(
		    "Texture import settings can only be edited for a project asset: " + source.ToString());
	}
	return Result<std::unique_ptr<TextureImportView>>::Success(
	    std::unique_ptr<TextureImportView>(new TextureImportView(std::move(source), sourceSettings)));
}

Result<void> TextureImportView::Reimport(AssetDatabase& database, ProjectAssetImporter& importer, Rhi::DevicePtr,
                                         ImGuiBridge&, bool cachedOnly)
{
	auto sourceResult = TextureImporter::DecodeSourceImage(database.Filesystem(), sourcePath);
	if (!sourceResult)
	{
		diagnostic = sourceResult.error;
		return Result<void>::Failure(diagnostic);
	}
	if (!cachedOnly && !settingsInspector)
	{
		auto savedSettings = TextureImporter::SetSettings(database, sourcePath, settings);
		if (!savedSettings)
		{
			diagnostic = savedSettings.error;
			return savedSettings;
		}
	}
	auto importedAsset = cachedOnly ? ProjectAssetImporter::LoadCurrent(database, sourcePath)
	                                : importer.Import(database, sourcePath, true);
	if (!importedAsset)
	{
		diagnostic = importedAsset.error;
		return Result<void>::Failure(diagnostic);
	}
	const auto& artifact = importedAsset.value.artifacts.front();
	auto loaded = LoadTextureResource(database.Filesystem(), artifact.path);
	if (!loaded)
	{
		diagnostic = loaded.error;
		return Result<void>::Failure(diagnostic);
	}
	auto importedResult = Result<TextureImportResult>::Success(
	    {importedAsset.value.guid, artifact.path, loaded.value.texture, importedAsset.value.cacheHit});
	if (!importedResult)
	{
		diagnostic = importedResult.error;
		return Result<void>::Failure(diagnostic);
	}
	sourceTexture = std::move(sourceResult.value);
	imported = std::move(importedResult.value);
	hasImported = true;
	diagnostic.clear();
	return {};
}

Result<std::shared_ptr<AssetImporterInspector>> TextureImportView::SettingsInspector(AssetDatabase& database)
{
	if (!settingsInspector)
	{
		auto opened = AssetImporterInspector::Open(database, sourcePath);
		if (!opened)
		{
			return opened;
		}
		settingsInspector = std::move(opened.value);
	}
	return Result<std::shared_ptr<AssetImporterInspector>>::Success(settingsInspector);
}

bool TextureImportView::HasModifiedSettings() const
{
	return settingsInspector && settingsInspector->HasModified();
}

Result<void> TextureImportView::Draw(AssetDatabase& database, ProjectAssetImporter& importer, Rhi::DevicePtr device,
                                     ImGuiBridge& gui, bool canReimport,
                                     const std::function<void(const VirtualPath&)>& requestImport,
                                     ProjectWorkspace* workspace, const AssetImporterInspector::CustomDraw& custom,
                                     bool embedded)
{
	if (!embedded)
	{
		ImGui::Begin("Texture Import");
	}
	ImGui::TextUnformatted(AssetDisplayPath(sourcePath).c_str());
	ImGui::TextDisabled("Source remains unchanged; imported data is cached in Library.");
	auto target = SettingsInspector(database);
	if (!target)
	{
		if (!embedded)
		{
			ImGui::End();
		}
		return Result<void>::Failure(target.error);
	}
	auto drawn = target.value->DrawDefault(
	    [&](const VirtualPath& path)
	    {
		    const auto saved = TextureImporter::Settings(database, sourcePath);
		    if (!saved)
		    {
			    diagnostic = saved.error;
			    return;
		    }
		    settings = saved.value;
		    if (requestImport)
		    {
			    requestImport(path);
		    }
		    else
		    {
			    Reimport(database, importer, device, gui);
		    }
	    },
	    canReimport, workspace, custom);
	if (!drawn)
	{
		diagnostic = drawn.error;
	}
	if (hasImported)
	{
		ImGui::Text("Imported: %u x %u, %u mip levels", imported.texture->GetWidth(), imported.texture->GetHeight(),
		            imported.texture->GetMipLevels());
		ImGui::TextDisabled("GUID: %s", imported.guid.ToString().c_str());
		ImGui::TextDisabled("View Source, Imported, channels and mips in Inspector Preview.");
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	if (!embedded)
	{
		ImGui::End();
	}
	return {};
}

void TextureImportView::Close(ImGuiBridge&)
{
	sourceTexture.reset();
	imported = {};
	hasImported = false;
}
} // namespace Ursa::Editor
