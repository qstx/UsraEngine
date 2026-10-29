#include <Scripting/ScriptIdeProject.hpp>

#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

VirtualPath IdePath(const String& relative)
{
	return VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptIDE/" + relative).value;
}

Result<bool> WriteChanged(VirtualFileSystem& filesystem, const VirtualPath& path, const String& text)
{
	auto previous = filesystem.ReadText(path);
	if (previous && previous.value == text)
	{
		return Result<bool>::Success(false);
	}
	Vector<UInt8> bytes(text.begin(), text.end());
	auto written = filesystem.WriteAtomic(path, bytes);
	return written ? Result<bool>::Success(true) : Result<bool>::Failure(written.error);
}

String Solution()
{
	const String gameId = "{61019740-4D72-4E51-A436-86CE4B2B2B3F}";
	const String editorId = "{9F915667-BBDA-4FDC-93B8-06253CB36992}";
	String result = "Microsoft Visual Studio Solution File, Format Version 12.00\n# Visual Studio Version 18\n";
	for (const auto& project :
	     Vector<std::pair<String, String>>{{"Ursa.Game", gameId}, {"Ursa.EditorScripts", editorId}})
	{
		const String group = project.first == "Ursa.Game" ? "Game" : "Editor";
		result += "Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"" + project.first + "\", \"Projects\\" +
		          group + "\\" + project.first + ".csproj\", \"" + project.second + "\"\nEndProject\n";
	}
	result += "Global\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
	for (const String configuration : {String("Debug"), String("Release")})
	{
		result += "\t\t" + configuration + "|Any CPU = " + configuration + "|Any CPU\n";
	}
	result += "\tEndGlobalSection\n\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
	for (const auto& id : {gameId, editorId})
	{
		for (const String configuration : {String("Debug"), String("Release")})
		{
			result += "\t\t" + id + "." + configuration + "|Any CPU.ActiveCfg = " + configuration + "|Any CPU\n";
			result += "\t\t" + id + "." + configuration + "|Any CPU.Build.0 = " + configuration + "|Any CPU\n";
		}
	}
	return result + "\tEndGlobalSection\nEndGlobal\n";
}

String Project(const ScriptProjectInput& input, ScriptAssemblyGroup group, const std::filesystem::path& directory,
               const std::filesystem::path& frameworkProjects)
{
	const bool editor = group == ScriptAssemblyGroup::Editor;
	String text = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n" + input.Properties();
	text += "    <AssemblyName>" + String(editor ? "Ursa.EditorScripts" : "Ursa.Game") + "</AssemblyName>\n";
	text += "  </PropertyGroup>\n  <ItemGroup>\n" + input.CompileItems(group, directory, true);
	if (frameworkProjects.empty())
	{
		text += input.FrameworkItems(group);
	}
	else
	{
		for (const auto& reference : input.references)
		{
			if (reference.editorOnly && !editor)
			{
				continue;
			}
			const auto path =
			    frameworkProjects / ToStdString(reference.name) / (ToStdString(reference.name) + ".csproj");
			text += "    <ProjectReference Include=\"" + EscapeProjectXml(PathToUrsaString(path)) + "\" />\n";
		}
	}
	if (editor)
	{
		text += "    <ProjectReference Include=\"../Game/Ursa.Game.csproj\" />\n";
	}
	return text + "  </ItemGroup>\n</Project>\n";
}
} // namespace

Result<ScriptIdeWorkspace> ScriptIdeProject::Generate(VirtualFileSystem& filesystem, const ScriptProjectInput& input,
                                                      const std::function<Result<String>()>& currentFingerprint,
                                                      const ScriptIdeOptions& options)
{
	const auto marker = IdePath(".ursa-directory");
	auto directory = filesystem.CreateDirectory(IdePath("Projects"));
	if (!directory)
	{
		return Result<ScriptIdeWorkspace>::Failure(directory.error);
	}
	auto marked = WriteChanged(filesystem, marker, "");
	if (!marked)
	{
		return Result<ScriptIdeWorkspace>::Failure(marked.error);
	}
	auto native = filesystem.ResolveForExternalToolInterop(marker);
	if (!native)
	{
		return Result<ScriptIdeWorkspace>::Failure(native.error);
	}
	const auto root = native.value.parent_path();
	ScriptIdeWorkspace workspace{root / "Ursa.Scripts.sln", root / "Ursa.Scripts.code-workspace",
	                             root.parent_path().parent_path(), input.StructureFingerprint()};
	std::filesystem::path frameworkProjects;
	std::filesystem::path description = options.description;
	String descriptionText;
	bool associated = options.explicitAssociation;
	const auto association = filesystem.ReadText(IdePath("UnifiedWorkspace.json"));
	try
	{
		if (association && !options.explicitAssociation)
		{
			const auto saved = Json::parse(ToStdString(association.value));
			if (saved.at("Version") != 1)
			{
				throw std::runtime_error("Unsupported workspace association version");
			}
			description = std::filesystem::u8path(saved.at("Description").get<std::string>());
			associated = true;
		}
		if (!description.empty())
		{
			std::ifstream stream(description, std::ios::binary);
			if (!stream && associated)
			{
				throw std::runtime_error("Associated workspace description is missing");
			}
			if (stream)
			{
				const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
				const auto document = Json::parse(bytes);
				const auto projectRoot = std::filesystem::u8path(document.at("ProjectRoot").get<std::string>());
				const bool matchingProject = std::filesystem::equivalent(projectRoot, workspace.projectRoot);
				if (associated && !matchingProject)
				{
					throw std::runtime_error("Workspace belongs to another project");
				}
				if (matchingProject)
				{
					const auto buildRoot = std::filesystem::u8path(document.at("BuildRoot").get<std::string>());
					const auto engineRoot = std::filesystem::u8path(document.at("EngineRoot").get<std::string>());
					const auto solution = std::filesystem::u8path(document.at("Solution").get<std::string>());
					const auto managed = std::filesystem::u8path(document.at("ManagedProjects").get<std::string>());
					if (document.at("Version") != 1 || (!options.preparing && !document.at("Ready").get<bool>()) ||
					    document.at("Configurations") != Json::array({"Debug", "Release"}) ||
					    !buildRoot.is_absolute() || !std::filesystem::is_regular_file(engineRoot / "CMakeLists.txt") ||
					    !std::filesystem::equivalent(solution.parent_path(), buildRoot) ||
					    (solution.filename() != "UrsaEngine.sln" && solution.filename() != "UrsaEngine.slnx") ||
					    !std::filesystem::is_regular_file(solution) ||
					    !std::filesystem::equivalent(managed, buildRoot / "Ide/Managed"))
					{
						throw std::runtime_error("Workspace paths, configuration or readiness are invalid");
					}
					for (const auto& name : {"Ursa.Bindings", "Ursa.Runtime", "Ursa.Rendering", "Ursa.Editor"})
					{
						if (!std::filesystem::is_regular_file(managed / name / (std::string(name) + ".csproj")))
						{
							throw std::runtime_error("Workspace framework project is missing");
						}
					}
					frameworkProjects = managed;
					workspace.visualStudioSolution = solution;
					descriptionText = ToUrsaString(bytes);
					workspace.fingerprint += "|" + PathToUrsaString(description) + "|" + descriptionText;
				}
			}
		}
	}
	catch (const std::exception& exception)
	{
		return Result<ScriptIdeWorkspace>::Failure(
		    "Cannot use unified workspace. Run GenerateEngineSolutionWithTracy.bat to regenerate or reassociate it. " +
		    ToUrsaString(exception.what()));
	}
	const Json codeWorkspace = {
	    {"folders", Json::array({{{"name", "Project"}, {"path", "../.."}}, {{"name", "C# Workspace"}, {"path", "."}}})},
	    {"settings", {{"dotnet.defaultSolution", ToStdString(PathToUrsaString(workspace.solution))}}}};
	const String props =
	    "<Project>\n  <PropertyGroup>\n"
	    "    "
	    "<BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)Build/$(MSBuildProjectName)/$(Configuration)/obj/</"
	    "BaseIntermediateOutputPath>\n"
	    "    <OutputPath>$(MSBuildThisFileDirectory)Build/$(MSBuildProjectName)/bin/$(Configuration)/</OutputPath>\n"
	    "  </PropertyGroup>\n</Project>\n";
	Vector<std::pair<String, String>> files = {
	    {"Directory.Build.props", props},
	    {"Projects/Game/Ursa.Game.csproj",
	     Project(input, ScriptAssemblyGroup::Game, root / "Projects/Game", frameworkProjects)},
	    {"Projects/Editor/Ursa.EditorScripts.csproj",
	     Project(input, ScriptAssemblyGroup::Editor, root / "Projects/Editor", frameworkProjects)},
	    {"Ursa.Scripts.sln", Solution()},
	    {"Ursa.Scripts.code-workspace", ToUrsaString(codeWorkspace.dump(2))}};
	if (!frameworkProjects.empty() && !options.preparing)
	{
		const Json link = {{"Version", 1}, {"Description", ToStdString(PathToUrsaString(description))}};
		files.push_back({"UnifiedWorkspace.json", ToUrsaString(link.dump(2))});
	}
	const auto checkFresh = [&]() -> Result<void>
	{
		if (currentFingerprint)
		{
			auto current = currentFingerprint();
			if (!current)
			{
				return Result<void>::Failure(current.error);
			}
			if (current.value != input.StructureFingerprint())
			{
				return Result<void>::Failure("Script IDE inputs changed during generation; retry the request");
			}
		}
		if (!descriptionText.empty())
		{
			std::ifstream stream(description, std::ios::binary);
			const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			if (!stream || ToUrsaString(bytes) != descriptionText)
			{
				return Result<void>::Failure("Unified workspace changed during generation; retry the request");
			}
		}
		return Result<void>::Success();
	};
	auto fresh = checkFresh();
	if (!fresh)
	{
		return Result<ScriptIdeWorkspace>::Failure(fresh.error);
	}
	const auto statePath = IdePath("GenerationState.json");
	bool invalidated = false;
	for (const auto& file : files)
	{
		auto previous = filesystem.ReadText(IdePath(file.first));
		if (previous && previous.value == file.second)
		{
			continue;
		}
		if (!invalidated)
		{
			auto exists = filesystem.Exists(statePath);
			if (!exists)
			{
				return Result<ScriptIdeWorkspace>::Failure(exists.error);
			}
			if (exists.value)
			{
				auto removed = filesystem.Remove(statePath);
				if (!removed)
				{
					return Result<ScriptIdeWorkspace>::Failure(removed.error);
				}
			}
			invalidated = true;
		}
		auto written = WriteChanged(filesystem, IdePath(file.first), file.second);
		if (!written)
		{
			return Result<ScriptIdeWorkspace>::Failure(written.error);
		}
		workspace.filesChanged += written.value ? 1u : 0u;
	}
	fresh = checkFresh();
	if (!fresh)
	{
		return Result<ScriptIdeWorkspace>::Failure(fresh.error);
	}
	const Json state = {{"Version", 1}, {"Fingerprint", ToStdString(workspace.fingerprint)}};
	auto saved = WriteChanged(filesystem, statePath, ToUrsaString(state.dump(2)));
	return saved ? Result<ScriptIdeWorkspace>::Success(std::move(workspace))
	             : Result<ScriptIdeWorkspace>::Failure(saved.error);
}
} // namespace Ursa::Editor
