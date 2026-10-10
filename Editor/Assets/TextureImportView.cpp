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

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
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

Result<void> TextureImportView::UploadPreview(Rhi::DevicePtr device, ImGuiBridge& gui,
                                              const MaterialTexturePtr& texture, ImTextureID& textureId,
                                              Rhi::TexturePtr& gpuTexture, Rhi::SamplerPtr& gpuSampler)
{
	if (!device || !texture)
	{
		return Result<void>::Failure("Texture preview needs a device and decoded texture");
	}
	Ursa::Size uploadBytes = texture->GetPixels().size();
	for (const TextureMip& mipmap : texture->GetMipmaps())
	{
		uploadBytes += mipmap.pixels.size();
	}
	auto upload = device->CreateBuffer({uploadBytes, Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload});
	if (!upload)
	{
		return Result<void>::Failure(upload.error);
	}
	Ursa::Size offset = 0;
	auto written = upload.value->Write(offset, texture->GetPixels().data(), texture->GetPixels().size());
	if (!written)
	{
		return written;
	}
	offset += texture->GetPixels().size();
	for (const TextureMip& mipmap : texture->GetMipmaps())
	{
		written = upload.value->Write(offset, mipmap.pixels.data(), mipmap.pixels.size());
		if (!written)
		{
			return written;
		}
		offset += mipmap.pixels.size();
	}
	const Rhi::Format format =
	    texture->GetColorSpace() == TextureColorSpace::Srgb ? Rhi::Format::RgbA8Srgb : Rhi::Format::RgbA8Unorm;
	auto createdTexture = device->CreateTexture({texture->GetWidth(), texture->GetHeight(), format,
	                                             Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination,
	                                             texture->GetMipLevels()});
	if (!createdTexture)
	{
		return Result<void>::Failure(createdTexture.error);
	}
	try
	{
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		done(command->Barrier(createdTexture.value, Rhi::ResourceState::Undefined,
		                      Rhi::ResourceState::TransferDestination));
		offset = 0;
		for (Ursa::UInt32 mipLevel = 0; mipLevel < texture->GetMipLevels(); ++mipLevel)
		{
			const Ursa::Size mipBytes =
			    mipLevel == 0 ? texture->GetPixels().size() : texture->GetMipmaps()[mipLevel - 1].pixels.size();
			done(command->CopyBufferToTexture(upload.value, createdTexture.value, mipLevel, offset));
			offset += mipBytes;
		}
		done(command->Barrier(createdTexture.value, Rhi::ResourceState::TransferDestination,
		                      Rhi::ResourceState::ShaderRead));
		done(command->End());
		auto fence = Take(device->Submit(command));
		done(fence->Wait());
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(ToUrsaString(error.what()));
	}
	const MaterialSampler samplerSettings = texture->DefaultSampler();
	auto createdSampler = device->CreateSampler(
	    {samplerSettings.linear, samplerSettings.repeat, static_cast<Ursa::Float32>(texture->GetMipLevels() - 1)});
	if (!createdSampler)
	{
		return Result<void>::Failure(createdSampler.error);
	}
	auto registered = gui.RegisterTexture(createdTexture.value, createdSampler.value);
	if (!registered)
	{
		return Result<void>::Failure(registered.error);
	}
	textureId = registered.value;
	gpuTexture = std::move(createdTexture.value);
	gpuSampler = std::move(createdSampler.value);
	return {};
}

Result<void> TextureImportView::Reimport(AssetDatabase& database, Rhi::DevicePtr device, ImGuiBridge& gui,
                                         bool cachedOnly)
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
	ImTextureID newSourceId = ImTextureID_Invalid;
	Rhi::TexturePtr newSourceTexture;
	Rhi::SamplerPtr newSourceSampler;
	auto sourceUploaded =
	    UploadPreview(device, gui, sourceResult.value, newSourceId, newSourceTexture, newSourceSampler);
	if (!sourceUploaded)
	{
		diagnostic = sourceUploaded.error;
		return sourceUploaded;
	}
	ImTextureID newPreviewId = ImTextureID_Invalid;
	Rhi::TexturePtr newPreviewTexture;
	Rhi::SamplerPtr newPreviewSampler;
	auto previewUploaded =
	    UploadPreview(device, gui, importedResult.value.texture, newPreviewId, newPreviewTexture, newPreviewSampler);
	if (!previewUploaded)
	{
		gui.UnregisterTexture(newSourceId);
		diagnostic = previewUploaded.error;
		return previewUploaded;
	}
	if (sourcePreviewId != ImTextureID_Invalid)
	{
		gui.UnregisterTexture(sourcePreviewId);
	}
	if (previewId != ImTextureID_Invalid)
	{
		gui.UnregisterTexture(previewId);
	}
	sourcePreviewId = newSourceId;
	sourcePreviewTexture = std::move(newSourceTexture);
	sourcePreviewSampler = std::move(newSourceSampler);
	previewId = newPreviewId;
	previewTexture = std::move(newPreviewTexture);
	previewSampler = std::move(newPreviewSampler);
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
	const bool reimport = ImGui::Button("Reimport");
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
		if (HasPreview())
		{
			if (ImGui::BeginTable("TexturePreviews", 2))
			{
				const Ursa::Float32 sourceScale =
				    (std::min)(1.0f, 320.0f / (std::max)(sourceTexture->GetWidth(), sourceTexture->GetHeight()));
				const Ursa::Float32 importedScale =
				    (std::min)(1.0f, 320.0f / (std::max)(imported.texture->GetWidth(), imported.texture->GetHeight()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Source");
				ImGui::Image(sourcePreviewId,
				             ImVec2(sourceTexture->GetWidth() * sourceScale, sourceTexture->GetHeight() * sourceScale));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Imported");
				ImGui::Image(previewId, ImVec2(imported.texture->GetWidth() * importedScale,
				                               imported.texture->GetHeight() * importedScale));
				ImGui::EndTable();
			}
		}
	}
	if (!diagnostic.empty())
	{
		ImGui::TextWrapped("%s", diagnostic.c_str());
	}
	ImGui::End();
	return {};
}

void TextureImportView::Close(ImGuiBridge& gui)
{
	if (sourcePreviewId != ImTextureID_Invalid)
	{
		gui.UnregisterTexture(sourcePreviewId);
	}
	if (previewId != ImTextureID_Invalid)
	{
		gui.UnregisterTexture(previewId);
	}
	sourcePreviewId = ImTextureID_Invalid;
	previewId = ImTextureID_Invalid;
	sourcePreviewTexture.reset();
	sourcePreviewSampler.reset();
	previewTexture.reset();
	previewSampler.reset();
}
} // namespace Ursa::Editor
