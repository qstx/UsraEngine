#include <Vfs/PakBuilder.hpp>
#include <Vfs/PakArchive.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Core/StringInterop.hpp>
#include <Serialization/ResourceFile.hpp>
#include <Serialization/ResourceFile.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

#if defined(URSA_HAS_EDITOR_ASSET_PIPELINE)
#include <Assets/AssetDatabase.hpp>
#endif

namespace
{
using namespace Ursa;
using Json = nlohmann::json;

String Lowered(String value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character - 'A' + 'a');
		}
	}
	return value;
}

bool IsSourceOnlyAsset(const VirtualPath& path)
{
	const auto dot = path.relative.rfind('.');
	if (dot == String::npos)
	{
		return false;
	}
	const String extension = Lowered(path.relative.substr(dot));
	return extension == ".meta" || extension == ".fbx" || extension == ".obj" || extension == ".mtl" ||
	       extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".ursashader" ||
	       extension == ".hlsl" || extension == ".hlsli";
}

bool HasImportedMetadata(const VirtualFileSystem& filesystem)
{
	auto files = filesystem.List(projectScheme);
	if (!files)
	{
		return false;
	}
	for (const VirtualFileEntry& entry : files.value)
	{
		if (entry.path.relative.size() < 5 || entry.path.relative.substr(entry.path.relative.size() - 5) != ".meta")
		{
			continue;
		}
		auto text = filesystem.ReadText(entry.path);
		if (!text)
		{
			continue;
		}
		try
		{
			const Json metadata = Json::parse(ToStdString(text.value));
			if (metadata.is_object() && metadata.contains("importer") && metadata["importer"].is_string() &&
			    !metadata["importer"].get<std::string>().empty())
			{
				return true;
			}
		}
		catch (const std::exception&)
		{
		}
	}
	return false;
}

Result<void> AddRootCatalogEntries(const VirtualFileSystem& filesystem, const String& scheme,
                                   Map<String, Json>& catalogEntries)
{
	auto files = filesystem.List(scheme);
	if (!files)
	{
		return Result<void>::Failure(files.error);
	}
	for (const VirtualFileEntry& entry : files.value)
	{
		if (entry.path.relative.size() < 5 || entry.path.relative.substr(entry.path.relative.size() - 5) != ".meta")
		{
			continue;
		}
		const String assetRelative = entry.path.relative.substr(0, entry.path.relative.size() - 5);
		auto assetPath = VirtualPath::Parse(scheme + ":" + assetRelative);
		if (!assetPath)
		{
			return Result<void>::Failure(assetPath.error);
		}
		if (!ResourceKindForPath(assetPath.value))
		{
			continue;
		}
		auto text = filesystem.ReadText(entry.path);
		if (!text)
		{
			return Result<void>::Failure(text.error);
		}
		try
		{
			const Json metadata = Json::parse(ToStdString(text.value));
			if (!metadata.is_object() || !metadata.contains("guid") || !metadata["guid"].is_string())
			{
				return Result<void>::Failure("Resource metadata has no GUID at " + entry.path.ToString());
			}
			auto guid = ResourceId::Parse(ToUrsaString(metadata["guid"].get<std::string>()));
			if (!guid)
			{
				return Result<void>::Failure(guid.error + " at " + entry.path.ToString());
			}
			const String key = guid.value.ToString() + ":0";
			catalogEntries[key] = {{"guid", ToStdString(guid.value.ToString())},
			                       {"localFileId", 0},
			                       {"kind", ToStdString(assetPath.value.FileName())},
			                       {"path", ToStdString(assetPath.value.ToString())}};
		}
		catch (const std::exception& exception)
		{
			return Result<void>::Failure("Cannot parse resource metadata at " + entry.path.ToString() + ": " +
			                             ToUrsaString(exception.what()));
		}
	}
	return {};
}

Result<Vector<UInt8>> JsonBytes(const Json& document)
{
	try
	{
		const std::string text = document.dump(2);
		Vector<UInt8> bytes;
		bytes.reserve(text.size());
		for (unsigned char value : text)
		{
			bytes.push_back(value);
		}
		return Result<Vector<UInt8>>::Success(std::move(bytes));
	}
	catch (const std::exception& error)
	{
		return Result<Vector<UInt8>>::Failure(ToUrsaString(error.what()));
	}
}

} // namespace

int main(int argc, char** argv)
{
	using namespace Ursa;
	VirtualFileSystem source;
	Vector<std::filesystem::path> engineDirectories;
	std::filesystem::path projectDirectory;
	std::filesystem::path libraryDirectory;
	std::filesystem::path output;
	for (int index = 1; index < argc; ++index)
	{
		const std::string option = argv[index];
		if (index + 1 >= argc)
		{
			std::cerr << "Usage: UrsaPak --engine <assets> [--engine-generated <assets>] [--project <assets>] "
			             "[--library <Library>] --output <Content.pak>\n";
			return 2;
		}
		const auto path = std::filesystem::u8path(argv[++index]);
		if (option == "--output")
		{
			output = path;
		}
		else if (option == "--project")
		{
			projectDirectory = path;
		}
		else if (option == "--library")
		{
			libraryDirectory = path;
		}
		else if (option == "--engine" || option == "--engine-generated")
		{
			engineDirectories.push_back(path);
		}
		else
		{
			std::cerr << "Unknown package option: " << option << '\n';
			return 2;
		}
	}
	if (output.empty() || engineDirectories.empty())
	{
		std::cerr << "Package Engine sources and output are required\n";
		return 2;
	}
	for (const auto& directory : engineDirectories)
	{
		auto mounted = source.Mount(engineScheme, directory, MountAccess::ReadOnly);
		if (!mounted)
		{
			std::cerr << mounted.error.c_str() << '\n';
			return 1;
		}
	}
	if (!projectDirectory.empty())
	{
		auto projectMounted = source.Mount(projectScheme, projectDirectory, MountAccess::ReadOnly);
		if (!projectMounted)
		{
			std::cerr << projectMounted.error.c_str() << '\n';
			return 1;
		}
		auto projectFiles = source.List(projectScheme);
		if (!projectFiles)
		{
			std::cerr << projectFiles.error.c_str() << '\n';
			return 1;
		}
		for (const VirtualFileEntry& entry : projectFiles.value)
		{
			if (entry.path.relative.compare(0, String("__UrsaCooked/").size(), "__UrsaCooked/") == 0)
			{
				std::cerr << "Project Assets uses the reserved package staging directory: "
				          << entry.path.ToString().c_str() << '\n';
				return 1;
			}
		}
	}
	VirtualFileSystem packageSource;
	auto engineCatalogStage =
	    packageSource.MountProvider(engineScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
	if (!engineCatalogStage)
	{
		std::cerr << engineCatalogStage.error.c_str() << '\n';
		return 1;
	}
	for (const auto& directory : engineDirectories)
	{
		auto mounted = packageSource.Mount(engineScheme, directory, MountAccess::ReadOnly);
		if (!mounted)
		{
			std::cerr << mounted.error.c_str() << '\n';
			return 1;
		}
	}
	if (!projectDirectory.empty())
	{
		auto stagedMount =
		    packageSource.MountProvider(projectScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
		if (!stagedMount)
		{
			std::cerr << stagedMount.error.c_str() << '\n';
			return 1;
		}
		auto mounted = packageSource.Mount(projectScheme, projectDirectory, MountAccess::ReadOnly);
		if (!mounted)
		{
			std::cerr << mounted.error.c_str() << '\n';
			return 1;
		}
	}
	Map<String, Json> catalogEntries;
	auto engineMetadata = AddRootCatalogEntries(packageSource, engineScheme, catalogEntries);
	if (!engineMetadata)
	{
		std::cerr << engineMetadata.error.c_str() << '\n';
		return 1;
	}
	if (!projectDirectory.empty())
	{
		auto projectMetadata = AddRootCatalogEntries(packageSource, projectScheme, catalogEntries);
		if (!projectMetadata)
		{
			std::cerr << projectMetadata.error.c_str() << '\n';
			return 1;
		}
	}
	std::error_code error;
	const bool hasLibrary = !projectDirectory.empty() && !libraryDirectory.empty() &&
	                        std::filesystem::is_directory(libraryDirectory, error) && !error;
	if (hasLibrary)
	{
		auto libraryMounted = source.Mount(editorLibraryScheme, libraryDirectory, MountAccess::ReadOnly);
		if (!libraryMounted)
		{
			std::cerr << libraryMounted.error.c_str() << '\n';
			return 1;
		}
	}
#if defined(URSA_HAS_EDITOR_ASSET_PIPELINE)
	else if (!projectDirectory.empty() && HasImportedMetadata(source))
	{
		std::cerr << "Imported assets require their current Library cache; pass --library <Library>\n";
		return 1;
	}
#else
	else if (!projectDirectory.empty() && HasImportedMetadata(source))
	{
		std::cerr << "Imported assets cannot be packaged without Editor asset import support\n";
		return 1;
	}
#endif
	UInt32 catalogEntryCount = 0;
	UInt32 projectAssetCount = 0;
#if defined(URSA_HAS_EDITOR_ASSET_PIPELINE)
	if (hasLibrary)
	{
		auto database = Editor::AssetDatabase::Open(source, projectScheme);
		if (!database)
		{
			std::cerr << database.error.c_str() << '\n';
			return 1;
		}
		auto refreshedDatabase = database.value->Refresh();
		if (!refreshedDatabase)
		{
			std::cerr << refreshedDatabase.error.c_str() << '\n';
			return 1;
		}
		projectAssetCount = static_cast<UInt32>(database.value->Assets().size());
		auto artifacts = database.value->CurrentImportedArtifacts();
		if (!artifacts)
		{
			std::cerr << artifacts.error.c_str() << '\n';
			return 1;
		}
		if (!artifacts.value.empty())
		{
			for (const Editor::ImportedArtifactRecord& artifact : artifacts.value)
			{
				auto bytes = source.Read(artifact.path);
				if (!bytes)
				{
					std::cerr << bytes.error.c_str() << '\n';
					return 1;
				}
				std::filesystem::path extension =
				    std::filesystem::u8path(ToStdString(artifact.path.relative)).extension();
				if (extension.empty())
				{
					extension = ".bin";
				}
				const String relative = String("__UrsaCooked/") + artifact.guid.ToString() + "/" +
				                        ToUrsaString(std::to_string(artifact.localFileId)) +
				                        ToUrsaString(extension.generic_string());
				auto cookedPath = VirtualPath::Parse(String(projectScheme) + ":" + relative);
				if (!cookedPath)
				{
					std::cerr << cookedPath.error.c_str() << '\n';
					return 1;
				}
				auto staged = packageSource.WriteAtomic(cookedPath.value, bytes.value);
				if (!staged)
				{
					std::cerr << staged.error.c_str() << '\n';
					return 1;
				}
				const String catalogKey =
				    artifact.guid.ToString() + ":" + ToUrsaString(std::to_string(artifact.localFileId));
				catalogEntries[catalogKey] = {{"guid", ToStdString(artifact.guid.ToString())},
				                              {"localFileId", artifact.localFileId},
				                              {"kind", ToStdString(artifact.kind)},
				                              {"path", ToStdString(cookedPath.value.ToString())}};
			}
		}
	}
#endif
	Json catalog = {{"format", "UrsaAssetCatalog"}, {"version", 1}, {"assets", Json::array()}};
	for (const auto& entry : catalogEntries)
	{
		catalog["assets"].push_back(entry.second);
	}
	catalogEntryCount = static_cast<UInt32>(catalogEntries.size());
	const String catalogScheme = projectDirectory.empty() ? engineScheme : projectScheme;
	auto catalogPath = VirtualPath::Parse(catalogScheme + ":__UrsaCooked/AssetCatalog.ursacatalog");
	auto catalogBytes = JsonBytes(catalog);
	if (!catalogPath || !catalogBytes)
	{
		std::cerr << (!catalogPath ? catalogPath.error.c_str() : catalogBytes.error.c_str()) << '\n';
		return 1;
	}
	auto stagedCatalog = packageSource.WriteAtomic(catalogPath.value, catalogBytes.value);
	if (!stagedCatalog)
	{
		std::cerr << stagedCatalog.error.c_str() << '\n';
		return 1;
	}
	Vector<PakEntryData> entries;
	for (const String& scheme : packageSource.Schemes())
	{
		if (scheme != engineScheme && scheme != projectScheme)
		{
			continue;
		}
		auto files = packageSource.List(scheme);
		if (!files)
		{
			std::cerr << files.error.c_str() << '\n';
			return 1;
		}
		for (const VirtualFileEntry& file : files.value)
		{
			if (IsSourceOnlyAsset(file.path))
			{
				continue;
			}
			auto bytes = packageSource.Read(file.path);
			if (!bytes)
			{
				std::cerr << bytes.error.c_str() << '\n';
				return 1;
			}
			entries.push_back({file.path, std::move(bytes.value)});
		}
	}
	auto encoded = PakBuilder::Encode(std::move(entries));
	if (!encoded)
	{
		std::cerr << encoded.error.c_str() << '\n';
		return 1;
	}
	output = std::filesystem::absolute(output, error);
	if (error)
	{
		std::cerr << "Cannot resolve package output path\n";
		return 1;
	}
	auto provider = DirectoryFileProvider::Open(output.parent_path(), true);
	if (!provider)
	{
		std::cerr << "Cannot open package output directory: " << provider.error.c_str() << '\n';
		return 1;
	}
	VirtualFileSystem destination;
	auto mounted = destination.MountProvider("out", provider.value, MountAccess::ReadWrite);
	auto packagePath = VirtualPath::Parse("out:" + PathToUrsaString(output.filename()));
	if (!mounted || !packagePath)
	{
		std::cerr << (!mounted ? mounted.error.c_str() : packagePath.error.c_str()) << '\n';
		return 1;
	}
	auto written = destination.WriteAtomic(packagePath.value, encoded.value);
	if (!written)
	{
		std::cerr << written.error.c_str() << '\n';
		return 1;
	}
	auto package = PakArchive::Open(output);
	if (!package)
	{
		std::cerr << package.error.c_str() << '\n';
		return 1;
	}
	std::cout << "Built " << output.string() << " with " << package.value->Schemes().size() << " resource schemes, "
	          << projectAssetCount << " project assets, and " << catalogEntryCount << " imported asset entries\n";
	return 0;
}
