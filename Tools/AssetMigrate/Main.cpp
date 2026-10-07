#include <Assets/AssetDatabase.hpp>
#include <Assets/AssetProjectMigrator.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <iostream>

namespace
{
struct Options
{
	std::filesystem::path assetsDirectory;
	std::filesystem::path libraryDirectory;
	Ursa::String restoreBackup;
};

Ursa::Result<Options> ParseOptions(int count, char** values)
{
	Options options;
	for (int index = 1; index < count; ++index)
	{
		const std::string argument = values[index];
		if ((argument == "--assets" || argument == "--library" || argument == "--restore") && index + 1 >= count)
		{
			return Ursa::Result<Options>::Failure("Missing value after " + Ursa::ToUrsaString(argument));
		}
		if (argument == "--assets")
		{
			options.assetsDirectory = std::filesystem::u8path(values[++index]);
		}
		else if (argument == "--library")
		{
			options.libraryDirectory = std::filesystem::u8path(values[++index]);
		}
		else if (argument == "--restore")
		{
			options.restoreBackup = Ursa::ToUrsaString(values[++index]);
		}
		else
		{
			return Ursa::Result<Options>::Failure("Unknown option: " + Ursa::ToUrsaString(argument));
		}
	}
	if (options.assetsDirectory.empty() || options.libraryDirectory.empty())
	{
		return Ursa::Result<Options>::Failure(
		    "Usage: UrsaAssetMigrate --assets <Assets directory> --library <Library directory> [--restore lib:Migrations/<id>]");
	}
	return Ursa::Result<Options>::Success(std::move(options));
}
} // namespace

int main(int count, char** values)
{
	auto options = ParseOptions(count, values);
	if (!options)
	{
		std::cerr << Ursa::ToStdString(options.error) << std::endl;
		return 2;
	}
	Ursa::VirtualFileSystem filesystem;
	auto mountedAssets = filesystem.Mount(Ursa::projectScheme, options.value.assetsDirectory, Ursa::MountAccess::ReadWrite);
	if (!mountedAssets)
	{
		std::cerr << Ursa::ToStdString(mountedAssets.error) << std::endl;
		return 1;
	}
	auto libraryProvider = Ursa::DirectoryFileProvider::Open(options.value.libraryDirectory, true);
	if (!libraryProvider)
	{
		std::cerr << Ursa::ToStdString(libraryProvider.error) << std::endl;
		return 1;
	}
	auto mountedLibrary = filesystem.MountProvider(Ursa::editorLibraryScheme, std::move(libraryProvider.value),
	                                               Ursa::MountAccess::ReadWrite);
	if (!mountedLibrary)
	{
		std::cerr << Ursa::ToStdString(mountedLibrary.error) << std::endl;
		return 1;
	}
	auto database = Ursa::Editor::AssetDatabase::Open(filesystem);
	if (!database)
	{
		std::cerr << Ursa::ToStdString(database.error) << std::endl;
		return 1;
	}
	auto refreshed = database.value->Refresh();
	if (!refreshed)
	{
		std::cerr << Ursa::ToStdString(refreshed.error) << std::endl;
		return 1;
	}
	if (!options.value.restoreBackup.empty())
	{
		auto restored = Ursa::Editor::AssetProjectMigrator::Restore(filesystem, options.value.restoreBackup);
		if (!restored)
		{
			std::cerr << Ursa::ToStdString(restored.error) << std::endl;
			return 1;
		}
		refreshed = database.value->Refresh();
		if (!refreshed)
		{
			std::cerr << Ursa::ToStdString(refreshed.error) << std::endl;
			return 1;
		}
		std::cout << "Restored asset migration from " << Ursa::ToStdString(options.value.restoreBackup) << std::endl;
		return 0;
	}
	auto migrated = Ursa::Editor::AssetProjectMigrator::Migrate(*database.value);
	if (!migrated)
	{
		std::cerr << Ursa::ToStdString(migrated.error) << std::endl;
		return 1;
	}
	std::cout << "Seeded " << migrated.value.seededGuids << " GUIDs and converted "
	          << migrated.value.convertedReferences << " references. Backup: "
	          << Ursa::ToStdString(migrated.value.backupRoot) << std::endl;
	return 0;
}
