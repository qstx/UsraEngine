#include <Assets/MeshComponentMigration.hpp>
#include <Assets/ModelImporter.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>

int main(int count, char** values)
{
	using namespace Ursa;
	using namespace Ursa::Editor;
	Log::ConfigureConsoleEncoding();
	std::filesystem::path project;
	std::filesystem::path engine = std::filesystem::u8path(URSA_MESH_MIGRATION_ENGINE_ASSETS);
	bool dryRun = false;
	for (int index = 1; index < count; ++index)
	{
		const String option = values[index];
		if (option == "--project" && index + 1 < count)
		{
			project = std::filesystem::u8path(values[++index]);
		}
		else if (option == "--engine" && index + 1 < count)
		{
			engine = std::filesystem::u8path(values[++index]);
		}
		else if (option == "--dry-run")
		{
			dryRun = true;
		}
		else
		{
			std::cerr << "Usage: UrsaMeshMigrate --project <project> [--engine <Engine/Assets>] [--dry-run]\n";
			return 2;
		}
	}
	if (project.empty())
	{
		std::cerr << "--project is required\n";
		return 2;
	}
	auto workspace = ProjectWorkspace::Open(
	    std::filesystem::absolute(project),
	    Vector<ResourceMount>{{engineScheme, std::filesystem::absolute(engine), MountAccess::ReadOnly}});
	if (!workspace)
	{
		std::cerr << ToStdString(workspace.error) << '\n';
		return 1;
	}
	auto* database = workspace.value->Database();
	if (!database)
	{
		std::cerr << "Project AssetDatabase is unavailable\n";
		return 1;
	}
	for (const auto& asset : database->Assets())
	{
		const auto extension = PathFromUrsaString(asset.path.relative).extension();
		if (extension == ".fbx" || extension == ".obj")
		{
			auto settings = ModelImporter::Settings(*database, asset.path);
			if (!settings)
			{
				std::cerr << ToStdString(settings.error) << '\n';
				return 1;
			}
			auto imported = ModelImporter::Import(*database, asset.path, settings.value);
			if (!imported)
			{
				std::cerr << ToStdString(imported.error) << '\n';
				return 1;
			}
		}
	}
	auto& files = workspace.value->Index().Filesystem();
	auto resources = ResourceSession::Open(files, workspace.value->AssetResolver());
	if (!resources)
	{
		std::cerr << ToStdString(resources.error) << '\n';
		return 1;
	}
	auto migrated = MeshComponentMigration::Migrate(files, *resources.value, dryRun);
	if (!migrated)
	{
		std::cerr << ToStdString(migrated.error) << '\n';
		return 1;
	}
	std::cout << (dryRun ? "Validated " : "Migrated ") << migrated.value.documents << " documents\n";
	if (!migrated.value.backupRoot.empty())
	{
		std::cout << "Backup: " << ToStdString(migrated.value.backupRoot) << '\n';
	}
	return 0;
}
