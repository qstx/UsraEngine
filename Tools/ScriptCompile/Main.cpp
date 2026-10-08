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
	if (argumentCount < 2 || argumentCount > 3)
	{
		std::cerr << "Usage: UrsaScriptCompile <project-directory> [Debug|Release]\n";
		return 2;
	}
	const String configuration = argumentCount == 3 ? arguments[2] : "Debug";
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
	auto compiled = service.value->Build(ScriptAssemblyGroup::Editor, configuration);
	if (!compiled)
	{
		std::cerr << ToStdString(compiled.error) << '\n';
		return 1;
	}
	std::cout << ToStdString(compiled.value.diagnostics) << '\n';
	return compiled.value.succeeded ? 0 : 1;
}
