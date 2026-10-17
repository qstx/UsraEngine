#include <Scripting/ScriptIdeProject.hpp>

#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>

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
	String result = "Microsoft Visual Studio Solution File, Format Version 12.00\n# Visual Studio Version 17\n";
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

String Project(const ScriptProjectInput& input, ScriptAssemblyGroup group, const std::filesystem::path& directory)
{
	const bool editor = group == ScriptAssemblyGroup::Editor;
	String text = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n" + input.Properties();
	text += "    <AssemblyName>" + String(editor ? "Ursa.EditorScripts" : "Ursa.Game") + "</AssemblyName>\n";
	text += "  </PropertyGroup>\n  <ItemGroup>\n" + input.CompileItems(group, directory, true);
	text += input.FrameworkItems(group);
	if (editor)
	{
		text += "    <ProjectReference Include=\"../Game/Ursa.Game.csproj\" />\n";
	}
	return text + "  </ItemGroup>\n</Project>\n";
}
} // namespace

Result<ScriptIdeWorkspace> ScriptIdeProject::Generate(VirtualFileSystem& filesystem, const ScriptProjectInput& input,
                                                      const std::function<Result<String>()>& currentFingerprint)
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
	const Json codeWorkspace = {
	    {"folders", Json::array({{{"name", "Project"}, {"path", "../.."}}, {{"name", "C# Workspace"}, {"path", "."}}})},
	    {"settings", {{"dotnet.defaultSolution", ToStdString(PathToUrsaString(workspace.solution))}}}};
	const String props =
	    "<Project>\n  <PropertyGroup>\n"
	    "    "
	    "<BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)Build/$(MSBuildProjectName)/obj/</"
	    "BaseIntermediateOutputPath>\n"
	    "    <OutputPath>$(MSBuildThisFileDirectory)Build/$(MSBuildProjectName)/bin/$(Configuration)/</OutputPath>\n"
	    "  </PropertyGroup>\n</Project>\n";
	const Vector<std::pair<String, String>> files = {
	    {"Directory.Build.props", props},
	    {"Projects/Game/Ursa.Game.csproj", Project(input, ScriptAssemblyGroup::Game, root / "Projects/Game")},
	    {"Projects/Editor/Ursa.EditorScripts.csproj",
	     Project(input, ScriptAssemblyGroup::Editor, root / "Projects/Editor")},
	    {"Ursa.Scripts.sln", Solution()},
	    {"Ursa.Scripts.code-workspace", ToUrsaString(codeWorkspace.dump(2))}};
	const auto checkFresh = [&]() -> Result<void>
	{
		if (currentFingerprint)
		{
			auto current = currentFingerprint();
			if (!current)
			{
				return Result<void>::Failure(current.error);
			}
			if (current.value != workspace.fingerprint)
			{
				return Result<void>::Failure("Script IDE inputs changed during generation; retry the request");
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
