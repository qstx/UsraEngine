#include <Assets/AssetDatabase.hpp>
#include <Assets/TextureImporter.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceSession.hpp>
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

void TestProvidedImage(const std::filesystem::path& root)
{
	const auto assetsRoot = root / "Assets";
	const auto libraryRoot = root / "Library";
	const auto source = assetsRoot / "Textures" / "UrsaEngine.png";
	std::filesystem::create_directories(source.parent_path());
	std::filesystem::create_directories(libraryRoot);
	std::filesystem::copy_file(std::filesystem::u8path(URSA_DEMO_TEXTURE_PATH), source);
	const auto sourceSize = std::filesystem::file_size(source);

	VirtualFileSystem filesystem;
	Expect(static_cast<bool>(filesystem.Mount(projectScheme, assetsRoot, MountAccess::ReadWrite)),
	       "Cannot mount the copied Demo assets");
	Expect(static_cast<bool>(filesystem.Mount(editorLibraryScheme, libraryRoot, MountAccess::ReadWrite)),
	       "Cannot mount the copied Demo Library");
	auto database = AssetDatabase::Open(filesystem);
	if (!database)
	{
		throw std::runtime_error("Cannot open the sample AssetDatabase: " + ToStdString(database.error));
	}
	auto refreshed = database.value->Refresh();
	if (!refreshed)
	{
		throw std::runtime_error("Cannot refresh the sample AssetDatabase: " + ToStdString(refreshed.error));
	}
	auto imported = TextureImporter::Import(*database.value, VirtualPath::Parse("prj:Textures/UrsaEngine.png").value,
	                                        TextureImportSettings{});
	Expect(imported && imported.value.texture->GetWidth() == 1448 && imported.value.texture->GetHeight() == 1086 &&
	           imported.value.texture->GetColorSpace() == TextureColorSpace::Srgb &&
	           imported.value.texture->GetMipLevels() == 11,
	       "UrsaEngine.png did not import with its source dimensions and complete mip chain");
	Expect(std::filesystem::file_size(source) == sourceSize,
	       "Importing UrsaEngine.png modified the copied source file");

	AssetDatabase* assetDatabase = database.value.get();
	auto session = ResourceSession::Open(filesystem, [assetDatabase](ResourceId guid, UInt64 localFileId)
	                                     { return assetDatabase->Resolve(guid, localFileId); });
	Expect(static_cast<bool>(session), "Cannot open a texture ResourceSession with AssetDatabase resolution");
	auto loaded = session.value->LoadTexture(imported.value.guid);
	Expect(loaded && loaded.value->GetWidth() == 1448 && loaded.value->GetHeight() == 1086 &&
	           loaded.value->GetMipLevels() == 11,
	       "Runtime texture loading did not resolve the imported Demo sample");
}
} // namespace

int main()
{
	try
	{
		const auto root = std::filesystem::current_path() / "temp" /
		                  ("UrsaDemoTextureImportTests-" + ToStdString(ResourceId::Generate().ToString()));
		std::filesystem::create_directories(root);
		TestProvidedImage(root);
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
