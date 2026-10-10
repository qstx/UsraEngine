#include <Assets/AssetImportPipeline.hpp>
#include <Core/Log.hpp>
#include <Core/StringInterop.hpp>
#include <iostream>

int main(int argc, char** argv)
{
	using namespace Ursa;
	using namespace Ursa::Editor;
	Log::ConfigureConsoleEncoding();
	std::filesystem::path project;
	std::filesystem::path engine;
	AssetImportOptions options;
	String profile = "Both";
	for (int index = 1; index < argc; ++index)
	{
		const String argument = argv[index];
		if (argument == "--project" && index + 1 < argc)
		{
			project = std::filesystem::absolute(std::filesystem::u8path(argv[++index]));
		}
		else if (argument == "--engine" && index + 1 < argc)
		{
			engine = std::filesystem::absolute(std::filesystem::u8path(argv[++index]));
		}
		else if (argument == "--configuration" && index + 1 < argc)
		{
			options.configuration = argv[++index];
		}
		else if (argument == "--target" && index + 1 < argc)
		{
			options.target = argv[++index];
		}
		else if (argument == "--profile" && index + 1 < argc)
		{
			profile = argv[++index];
		}
		else if (argument == "--scope" && index + 1 < argc)
		{
			auto scope = VirtualPath::Parse(argv[++index]);
			if (!scope)
			{
				std::cerr << ToStdString(scope.error) << '\n';
				return 2;
			}
			options.scope = scope.value;
		}
		else if (argument == "--force")
		{
			options.force = true;
		}
		else if (argument == "--no-scripts")
		{
			options.buildScripts = false;
		}
		else if (argument == "--bootstrap")
		{
			options.bootstrapOnly = true;
		}
		else
		{
			std::cerr << "Unknown import option: " << ToStdString(argument) << '\n';
			return 2;
		}
	}
	if (project.empty() || (profile != "Both" && profile != "Editor" && profile != "Player"))
	{
		std::cerr
		    << "Usage: UrsaAssetImport --project <project> [--engine <Assets>] [--configuration Debug|Release] "
		       "[--profile Both|Editor|Player] [--target WindowsVulkan] [--scope prj:Path] [--force] [--no-scripts]\n";
		return 2;
	}
	auto workspace = AssetImportPipeline::OpenProject(project, engine);
	if (!workspace)
	{
		std::cerr << ToStdString(workspace.error) << '\n';
		return 1;
	}
	for (const String selected : {String("Editor"), String("Player")})
	{
		if (profile != "Both" && profile != selected)
		{
			continue;
		}
		options.profile = selected;
		auto report = AssetImportPipeline::Prepare(
		    *workspace.value, options, [](UInt64 current, UInt64 total, const VirtualPath& asset)
		    { std::cout << "[" << current << '/' << total << "] " << ToStdString(asset.ToString()) << '\n'; });
		if (!report)
		{
			std::cerr << ToStdString(report.error) << '\n';
			return 1;
		}
		std::cout << ToStdString(selected) << ": imported=" << report.value.importedAssets
		          << ", cacheHits=" << report.value.cacheHits << '\n';
	}
	return 0;
}
