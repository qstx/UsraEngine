#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <Assets/AssetDatabase.hpp>
#include <Assets/TextureImporter.hpp>
#include <Assets/TextureImportView.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Serialization/TextureResource.hpp>
#include <Ui/ImGuiBridge.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

VirtualPath Path(const char* value)
{
	auto path = VirtualPath::Parse(value);
	if (!path)
	{
		throw std::runtime_error(ToStdString(path.error));
	}
	return std::move(path.value);
}

void AppendEncodedBytes(void* context, void* data, int size)
{
	auto& bytes = *static_cast<Vector<UInt8>*>(context);
	auto* begin = static_cast<const UInt8*>(data);
	for (int index = 0; index < size; ++index)
	{
		bytes.push_back(begin[index]);
	}
}

Vector<UInt8> EncodePng(const Vector<UInt8>& pixels, int width, int height)
{
	Vector<UInt8> bytes;
	if (!stbi_write_png_to_func(AppendEncodedBytes, &bytes, width, height, 4, pixels.data(), width * 4))
	{
		throw std::runtime_error("Cannot encode PNG test fixture");
	}
	return bytes;
}

Vector<UInt8> EncodeJpeg(const Vector<UInt8>& pixels, int width, int height)
{
	Vector<UInt8> bytes;
	if (!stbi_write_jpg_to_func(AppendEncodedBytes, &bytes, width, height, 4, pixels.data(), 95))
	{
		throw std::runtime_error("Cannot encode JPEG test fixture");
	}
	return bytes;
}

void TestImportSettingsCacheAndFailure(const std::filesystem::path& root)
{
	std::filesystem::create_directories(root / "Assets");
	std::filesystem::create_directories(root / "Library");
	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite)),
	       "Cannot mount texture importer project fixture");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, root / "Library", MountAccess::ReadWrite)),
	       "Cannot mount texture importer Library fixture");
	const Vector<UInt8> sourcePixels{255, 0,  0,  255, 0,  255, 0,  128, 0,  0,  255, 255, 255, 255, 255, 0,
	                                 10,  20, 30, 255, 40, 50,  60, 255, 70, 80, 90,  255, 100, 110, 120, 255};
	Expect(static_cast<bool>(filesystem.WriteAtomic(Path("prj:Logo.png"), EncodePng(sourcePixels, 4, 2))) &&
	           static_cast<bool>(filesystem.WriteAtomic(Path("prj:Photo.jpg"), EncodeJpeg(sourcePixels, 4, 2))),
	       "Cannot write VFS texture fixtures");
	auto database = AssetDatabase::Open(filesystem);
	Expect(database && static_cast<bool>(database.value->Refresh()),
	       "Cannot initialize texture importer AssetDatabase");

	TextureImportSettings settings;
	settings.maxDimension = 2;
	settings.generateMipmaps = true;
	auto imported = TextureImporter::Import(*database.value, Path("prj:Logo.png"), settings);
	Expect(imported && !imported.value.cacheHit && imported.value.texture->GetWidth() == 2 &&
	           imported.value.texture->GetHeight() == 1 &&
	           imported.value.texture->GetColorSpace() == TextureColorSpace::Srgb &&
	           imported.value.texture->GetMipLevels() == 2 && imported.value.texture->DefaultSampler().linear &&
	           imported.value.texture->DefaultSampler().repeat,
	       "PNG import did not apply the persisted size, color-space, mip, and sampling settings");
	const ResourceId imageGuid = imported.value.guid;
	auto sourceAfterImport = filesystem.Read(Path("prj:Logo.png"));
	Expect(sourceAfterImport && sourceAfterImport.value == EncodePng(sourcePixels, 4, 2),
	       "Texture import modified the source image");
	auto loadedArtifact = LoadTextureResource(filesystem, imported.value.artifactPath);
	Expect(loadedArtifact && loadedArtifact.value.id == imageGuid && loadedArtifact.value.texture->GetMipLevels() == 2,
	       "Imported texture artifact did not round-trip through the runtime resource loader");
	auto cached = TextureImporter::Import(*database.value, Path("prj:Logo.png"), settings);
	Expect(cached && cached.value.cacheHit && cached.value.artifactPath == imported.value.artifactPath,
	       "Unchanged source did not reuse its imported artifact");

	TextureImportSettings changed = settings;
	changed.type = TextureImportType::LinearData;
	changed.sRgb = false;
	changed.generateMipmaps = false;
	changed.filter = TextureFilterMode::Point;
	changed.wrap = TextureWrapMode::Clamp;
	changed.maxDimension = 4;
	auto reimported = TextureImporter::Import(*database.value, Path("prj:Logo.png"), changed);
	Expect(reimported && !reimported.value.cacheHit && reimported.value.artifactPath != imported.value.artifactPath &&
	           reimported.value.texture->GetWidth() == 4 && reimported.value.texture->GetHeight() == 2 &&
	           reimported.value.texture->GetColorSpace() == TextureColorSpace::Linear &&
	           reimported.value.texture->GetMipLevels() == 1 && !reimported.value.texture->DefaultSampler().linear &&
	           !reimported.value.texture->DefaultSampler().repeat,
	       "Changing texture import settings did not produce a new effective artifact");
	Rhi::DeviceDesc deviceDescription;
	auto device = Rhi::CreateDevice(deviceDescription);
	Expect(static_cast<bool>(device), "Cannot create a device for the texture importer Inspector test");
	auto gui = ImGuiBridge::Create(device.value);
	Expect(static_cast<bool>(gui), "Cannot create the texture importer ImGui bridge");
	auto view = TextureImportView::Open(Path("prj:Logo.png"), changed);
	Expect(static_cast<bool>(view), "Cannot open the texture import Inspector view");
	Expect(static_cast<bool>(view.value->Reimport(*database.value, device.value, *gui.value)) &&
	           view.value->HasPreview(),
	       "Texture import Inspector did not upload a preview");
	Expect(static_cast<bool>(gui.value->BeginFrame(1.0f / 60.0f, 640, 480)),
	       "Cannot begin an ImGui frame for texture import settings");
	Expect(static_cast<bool>(view.value->Draw(*database.value, device.value, *gui.value, false)),
	       "Texture import Inspector did not draw its settings and preview");
	Expect(static_cast<bool>(gui.value->EndFrame()), "Cannot end the texture import Inspector frame");
	AssetDatabase* assetDatabase = database.value.get();
	auto session = ResourceSession::Open(filesystem, [assetDatabase](ResourceId guid, UInt64 localFileId)
	                                     { return assetDatabase->Resolve(guid, localFileId); });
	Expect(static_cast<bool>(session), "ResourceSession could not open with the AssetDatabase resolver");
	auto runtimeTexture = session.value->LoadTexture(imageGuid);
	Expect(runtimeTexture && runtimeTexture.value->GetWidth() == 4 && runtimeTexture.value->GetHeight() == 2 &&
	           runtimeTexture.value->GetMipLevels() == 1,
	       "Runtime resource loading did not resolve the imported texture artifact");
	auto persistedSettings = TextureImporter::Settings(*database.value, Path("prj:Logo.png"));
	Expect(persistedSettings && persistedSettings.value.type == TextureImportType::LinearData &&
	           persistedSettings.value.filter == TextureFilterMode::Point &&
	           persistedSettings.value.wrap == TextureWrapMode::Clamp,
	       "Texture settings did not persist in the asset metadata");

	TextureImportSettings jpegSettings;
	jpegSettings.generateMipmaps = false;
	auto jpeg = TextureImporter::Import(*database.value, Path("prj:Photo.jpg"), jpegSettings);
	Expect(jpeg && jpeg.value.texture->GetWidth() == 4 && jpeg.value.texture->GetHeight() == 2,
	       "JPEG source was not imported");

	const VirtualPath source = Path("prj:Logo.png");
	Expect(static_cast<bool>(filesystem.WriteAtomic(source, {1, 2, 3, 4})), "Cannot corrupt the PNG fixture");
	Expect(!TextureImporter::Import(*database.value, source, changed), "Invalid image source was accepted");
	Expect(!view.value->Reimport(*database.value, device.value, *gui.value) && view.value->HasPreview(),
	       "Failed Inspector reimport discarded the last successful preview");
	auto retained = database.value->Resolve(imageGuid, 0);
	Expect(retained && retained.value == reimported.value.artifactPath &&
	           static_cast<bool>(LoadTextureResource(filesystem, retained.value)),
	       "Failed reimport replaced the last valid texture artifact");

	TextureImportSettings invalid = changed;
	invalid.maxDimension = 0;
	Expect(!TextureImporter::SetSettings(*database.value, source, invalid),
	       "Invalid maximum texture dimension was accepted");
	view.value->Close(*gui.value);
	Expect(static_cast<bool>(device.value->WaitIdle()), "Texture importer Inspector left GPU work in flight");
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::temp_directory_path() /
		                  ("UrsaTextureImporterTests-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root);
		TestImportSettingsCacheAndFailure(root);
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
