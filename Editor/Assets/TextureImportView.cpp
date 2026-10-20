#include <Ui/EditorIcons.hpp>
#include <Assets/TextureImportView.hpp>
#include <Assets/ProjectAssetImporter.hpp>
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

Result<void> TextureImportView::Reimport(AssetDatabase& database, Rhi::DevicePtr, ImGuiBridge&, bool cachedOnly)
{
	auto sourceResult = TextureImporter::DecodeSourceImage(database.Filesystem(), sourcePath);
	if (!sourceResult)
	{
		diagnostic = sourceResult.error;
		return Result<void>::Failure(diagnostic);
	}
	if (!cachedOnly)
	{
		auto savedSettings = TextureImporter::SetSettings(database, sourcePath, settings);
		if (!savedSettings)
		{
			diagnostic = savedSettings.error;
			return savedSettings;
		}
	}
	auto importedAsset = cachedOnly ? ProjectAssetImporter::LoadCurrent(database, sourcePath)
	                                : ProjectAssetImporter{}.Import(database, sourcePath, true);
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
	dirty = false;
	diagnostic.clear();
	return {};
}

Result<void> TextureImportView::Draw(AssetDatabase& database, Rhi::DevicePtr device, ImGuiBridge& gui, bool canReimport,
                                     const std::function<void(const VirtualPath&)>& requestImport)
{
	ImGui::Begin("Texture Import");
	ImGui::TextUnformatted(AssetDisplayPath(sourcePath).c_str());
	ImGui::TextDisabled("Source remains unchanged; imported data is cached in Library.");
	static constexpr const char* typeNames[] = {"Default", "Normal Map", "Linear Data"};
	int type = static_cast<int>(settings.type);
	if (ImGui::Combo("Texture Type", &type, typeNames, 3))
	{
		settings.type = static_cast<TextureImportType>(type);
		if (settings.type != TextureImportType::Default)
		{
			settings.sRgb = false;
		}
		dirty = true;
	}
	ImGui::BeginDisabled(settings.type != TextureImportType::Default);
	if (ImGui::Checkbox("sRGB (Color Texture)", &settings.sRgb))
	{
		dirty = true;
	}
	ImGui::EndDisabled();
	if (ImGui::Checkbox("Alpha", &settings.alpha))
	{
		dirty = true;
	}
	if (ImGui::Checkbox("Generate Mipmaps", &settings.generateMipmaps))
	{
		dirty = true;
	}
	static constexpr const char* filterNames[] = {"Point", "Linear"};
	int filter = settings.filter == TextureFilterMode::Point ? 0 : 1;
	if (ImGui::Combo("Filter Mode", &filter, filterNames, 2))
	{
		settings.filter = filter == 0 ? TextureFilterMode::Point : TextureFilterMode::Linear;
		dirty = true;
	}
	static constexpr const char* wrapNames[] = {"Repeat", "Clamp"};
	int wrap = settings.wrap == TextureWrapMode::Repeat ? 0 : 1;
	if (ImGui::Combo("Wrap Mode", &wrap, wrapNames, 2))
	{
		settings.wrap = wrap == 0 ? TextureWrapMode::Repeat : TextureWrapMode::Clamp;
		dirty = true;
	}
	int maxDimension = static_cast<int>(settings.maxDimension);
	if (ImGui::InputInt("Max Size", &maxDimension))
	{
		settings.maxDimension = maxDimension > 0 ? static_cast<Ursa::UInt32>(maxDimension) : 0;
		dirty = true;
	}
	ImGui::BeginDisabled(!canReimport || (!dirty && hasImported));
	const bool reimport = Icons::Button("Reimport");
	ImGui::EndDisabled();
	if (reimport)
	{
		auto result = requestImport ? TextureImporter::SetSettings(database, sourcePath, settings)
		                            : Reimport(database, std::move(device), gui);
		if (result && requestImport)
		{
			requestImport(sourcePath);
			dirty = false;
		}

		if (!result)
		{
			diagnostic = result.error;
		}
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
	ImGui::End();
	return {};
}

void TextureImportView::Close(ImGuiBridge&)
{
	sourceTexture.reset();
	imported = {};
	hasImported = false;
}
} // namespace Ursa::Editor
