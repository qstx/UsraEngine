#include <Assets/ScriptBuildService.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <Workspace/ProjectWorkspace.hpp>
#include <iostream>

int main(int argumentCount, char** arguments)
{
	Ursa::Log::ConfigureConsoleEncoding();
	using namespace Ursa;
	using namespace Ursa::Editor;
	if (argumentCount < 2)
	{
		std::cerr << "Usage: UrsaScriptCompile <project-directory> [Debug|Release|--ide [--workspace "
		             "<description.json>] [--prepare-workspace]]\n";
		return 2;
	}
	const bool ideOnly = argumentCount >= 3 && String(arguments[2]) == "--ide";
	const String configuration = argumentCount >= 3 && !ideOnly ? arguments[2] : "Debug";
	std::filesystem::path description;
	bool preparing = false;
	for (Int32 index = 3; index < argumentCount; ++index)
	{
		const String option = arguments[index];
		if (ideOnly && option == "--workspace" && index + 1 < argumentCount)
		{
			description = std::filesystem::absolute(std::filesystem::u8path(arguments[++index]));
		}
		else if (ideOnly && option == "--prepare-workspace")
		{
			preparing = true;
		}
		else
		{
			std::cerr << "Unknown or incomplete script compiler option\n";
			return 2;
		}
	}
	if (configuration != "Debug" && configuration != "Release")
	{
		std::cerr << "Script configuration must be Debug or Release\n";
		return 2;
	}
	auto workspace = ProjectWorkspace::Open(std::filesystem::absolute(std::filesystem::u8path(arguments[1])));
	if (!workspace)
	{
		std::cerr << ToStdString(workspace.error) << '\n';
		return 1;
	}
	auto& filesystem = workspace.value->Index().Filesystem();
	auto database = AssetDatabase::Open(filesystem);
	if (!database)
	{
		std::cerr << ToStdString(database.error) << '\n';
		return 1;
	}
	auto service =
	    ScriptBuildService::Open(filesystem, *database.value, std::filesystem::u8path(URSA_DOTNET_EXECUTABLE_PATH),
	                             std::filesystem::u8path(URSA_DOTNET_RUNTIME_ASSEMBLY_PATH),
	                             std::filesystem::u8path(URSA_DOTNET_EDITOR_ASSEMBLY_PATH),
	                             std::filesystem::u8path(URSA_DOTNET_CATALOG_GENERATOR_PATH),
	                             std::filesystem::u8path(URSA_DOTNET_REFERENCE_PACK_PATH));
	if (!service)
	{
		std::cerr << ToStdString(service.error) << '\n';
		return 1;
	}
	if (ideOnly)
	{
		auto generated = service.value->GenerateIdeProjects(description, preparing);
		if (!generated)
		{
			std::cerr << ToStdString(generated.error) << '\n';
			return 1;
		}
		std::cout << ToStdString(PathToUrsaString(generated.value.solution)) << '\n';
		return 0;
	}
	auto compiled = service.value->Build(ScriptAssemblyGroup::Editor, configuration);
	if (!compiled)
	{
		std::cerr << ToStdString(compiled.error) << '\n';
		return 1;
	}
	std::cout << ToStdString(compiled.value.diagnostics) << '\n';
	return compiled.value.succeeded ? 0 : 1;
}
