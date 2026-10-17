#include <Scripting/ScriptIdeProject.hpp>
#include <Scripting/ScriptDiagnostic.hpp>
#include <Core/StringInterop.hpp>
#include <algorithm>
#include <fstream>
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

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

VirtualPath Path(const String& text)
{
	return Take(VirtualPath::Parse(text));
}

void Write(const std::filesystem::path& path, const char* text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream stream(path, std::ios::binary);
	stream << text;
	Expect(static_cast<bool>(stream), "Cannot write source fixture");
}
} // namespace

int main()
{
	const auto root =
	    std::filesystem::temp_directory_path() / ("UrsaScriptIde-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		const auto assets = root / "Assets";
		const auto library = root / "Library";
		std::filesystem::create_directories(assets);
		std::filesystem::create_directories(library);
		VirtualFileSystem filesystem;
		Done(filesystem.Mount(projectScheme, assets, MountAccess::ReadWrite));
		Done(filesystem.Mount(editorLibraryScheme, library, MountAccess::ReadWrite));
		auto database = Take(AssetDatabase::Open(filesystem));
		const auto collect = [&]
		{
			return Take(CollectScriptProjectInput(filesystem, *database, root / "Framework/Ursa.Runtime.dll",
			                                      root / "Framework/Ursa.Editor.dll"));
		};
		auto empty = collect();
		Expect(empty.sources.empty(), "An empty project contains source items");
		auto workspace = Take(ScriptIdeProject::Generate(filesystem, empty));
		Expect(std::filesystem::exists(workspace.solution) && workspace.filesChanged == 5,
		       "Empty project did not produce a complete IDE workspace");
		const auto solutionTime = std::filesystem::last_write_time(workspace.solution);
		auto again = Take(ScriptIdeProject::Generate(filesystem, collect()));
		Expect(again.filesChanged == 0 && std::filesystem::last_write_time(workspace.solution) == solutionTime,
		       "Unchanged IDE generation rewrote project files");
		Write(assets / "Scripts/Z.cs", "class Z { this is invalid; }");
		Write(assets / "Scripts/A.cs", "class A : MissingType { }");
		Write(assets / "Scripts/Editor/Tool.cs", "class Tool { }");
		auto input = collect();
		Expect(input.sources.size() == 3 && input.sources[0].asset.path.relative == "Scripts/A.cs" &&
		           input.sources[1].asset.group == ScriptAssemblyGroup::Editor,
		       "Source collection requires semantic success or has unstable source groups/order");
		Expect(input.FrameworkItems(ScriptAssemblyGroup::Game).find("Ursa.Editor") == String::npos &&
		           input.FrameworkItems(ScriptAssemblyGroup::Editor).find("Ursa.Editor") != String::npos,
		       "Framework references violate group dependencies");
		workspace = Take(ScriptIdeProject::Generate(filesystem, input));
		auto editorProject = Take(filesystem.ReadText(Path("lib:ScriptIDE/Projects/Editor/Ursa.EditorScripts.csproj")));
		auto gameProject = Take(filesystem.ReadText(Path("lib:ScriptIDE/Projects/Game/Ursa.Game.csproj")));
		Expect(editorProject.find("ProjectReference Include=\"../Game/Ursa.Game.csproj\"") != String::npos &&
		           editorProject.find("Assemblies/Game") == String::npos &&
		           gameProject.find("Link=\"Assets/Scripts/A.cs\"") != String::npos &&
		           gameProject.find("Scripts/Editor/Tool.cs") == String::npos,
		       "IDE project copied sources or depended on a published Game DLL");
		const String fingerprint = input.StructureFingerprint();
		Write(assets / "Scripts/Z.cs", "class Z { int value = 42; }");
		input = collect();
		Expect(input.StructureFingerprint() == fingerprint &&
		           Take(ScriptIdeProject::Generate(filesystem, input)).filesChanged == 0,
		       "Method-body edits changed IDE structure or rewrote its files");
		const auto guid = Take(database->Metadata(Path("prj:Scripts/A.cs"))).guid;
		Done(database->Move(Path("prj:Scripts/A.cs"), Path("prj:Scripts/Editor/Renamed.cs")));
		input = collect();
		Expect(Take(database->Metadata(Path("prj:Scripts/Editor/Renamed.cs"))).guid == guid &&
		           input.StructureFingerprint() != fingerprint,
		       "Cross-group move lost GUID or did not invalidate the IDE structure");
		Take(ScriptIdeProject::Generate(filesystem, input));
		editorProject = Take(filesystem.ReadText(Path("lib:ScriptIDE/Projects/Editor/Ursa.EditorScripts.csproj")));
		gameProject = Take(filesystem.ReadText(Path("lib:ScriptIDE/Projects/Game/Ursa.Game.csproj")));
		Expect(editorProject.find("Scripts/Editor/Renamed.cs") != String::npos &&
		           gameProject.find("Scripts/A.cs") == String::npos,
		       "Cross-group move left stale IDE source items");
		Write(root / "Framework/Ursa.Runtime.dll", "framework-v1");
		const auto beforeApi = input.StructureFingerprint();
		Expect(collect().StructureFingerprint() != beforeApi,
		       "Framework identity changes did not invalidate the model");
		input = collect();
		auto stale = ScriptIdeProject::Generate(filesystem, input, [] { return Result<String>::Success("stale"); });
		Expect(!stale && stale.error.find("changed during generation") != String::npos,
		       "Stale input was reported as a current IDE workspace");
		input.nullable = "disable";
		UInt32 checks = 0;
		auto lateStale = ScriptIdeProject::Generate(
		    filesystem, input,
		    [&] { return Result<String>::Success(++checks == 1 ? input.StructureFingerprint() : "stale"); });
		Expect(!lateStale && !Take(filesystem.Exists(Path("lib:ScriptIDE/GenerationState.json"))),
		       "Changed inputs committed a partial generation as current");
		Take(ScriptIdeProject::Generate(filesystem, input));
		// A directory in place of a target file simulates a failed atomic replacement.
		const auto gamePath = Path("lib:ScriptIDE/Projects/Game/Ursa.Game.csproj");
		Done(filesystem.Remove(gamePath));
		Done(filesystem.CreateDirectory(gamePath));
		auto failed = ScriptIdeProject::Generate(filesystem, input);
		Expect(!failed && !Take(filesystem.Exists(Path("lib:ScriptIDE/GenerationState.json"))),
		       "Failed writes retained a current generation marker");
		Done(filesystem.RemoveDirectory(gamePath));
		Take(ScriptIdeProject::Generate(filesystem, input));
		const auto state = Take(filesystem.ReadText(Path("lib:ScriptIDE/GenerationState.json")));
		Expect(state.find(input.StructureFingerprint()) != String::npos, "Retry did not recover IDE generation");
		Done(database->Remove(Path("prj:Scripts/Z.cs")));
		input = collect();
		Take(ScriptIdeProject::Generate(filesystem, input));
		Expect(Take(filesystem.ReadText(gamePath)).find("Scripts/Z.cs") == String::npos,
		       "Removed scripts remained in the generated project");
		Write(assets / std::filesystem::u8path("Scripts/例子 (sample).cs"), "class Sample { }");
		input = collect();
		const auto unicode = std::find_if(input.sources.begin(), input.sources.end(), [](const auto& item)
		                                  { return item.asset.path.relative.find("sample") != String::npos; });
		Expect(unicode != input.sources.end(), "Unicode fixture is missing from the source model");
		std::string uri = "file:///" + unicode->nativePath.generic_u8string();
		for (auto at = uri.find(' '); at != std::string::npos; at = uri.find(' ', at + 3))
		{
			uri.replace(at, 1, "%20");
		}
		using Json = nlohmann::json;
		const auto logPath = Path("lib:ScriptIDE/Diagnostics.json");
		const auto readLog = [&](const Json& document)
		{
			const auto text = document.dump();
			Done(filesystem.WriteAtomic(logPath, Vector<UInt8>(text.data(), text.data() + text.size())));
			return Take(ReadScriptDiagnostics(filesystem, logPath, input.sources, "Compiler"));
		};
		Json location = {{"resultFile", {{"uri", uri}, {"region", {{"startLine", 4}, {"startColumn", 7}}}}}};
		Json result = {{"ruleId", "CS0169"},
		               {"level", "warning"},
		               {"message", "Unused field"},
		               {"locations", Json::array({location})}};
		auto records = readLog({{"runs", Json::array({{{"results", Json::array({result})}}})}});
		Expect(records.size() == 1 && records[0].source == unicode->asset.path && records[0].line == 4 &&
		           records[0].column == 7 && records[0].code == "CS0169",
		       "SARIF v1 lost encoded source locations");
		result["message"] = {{"text", "Unused field"}};
		result["locations"] = Json::array(
		    {{{"physicalLocation",
		       {{"artifactLocation", {{"uri", uri}}}, {"region", {{"startLine", 4}, {"startColumn", 7}}}}}}});
		records = readLog({{"runs", Json::array({{{"results", Json::array({result})}}})}});
		Expect(records[0].source == unicode->asset.path && records[0].severity == "warning",
		       "SARIF v2 lost source or severity");
		records = readLog({{"Version", 1},
		                   {"Diagnostics", Json::array({{{"Path", "prj:outside.cs"},
		                                                 {"Line", -1},
		                                                 {"Column", 0},
		                                                 {"Code", "TOOL"},
		                                                 {"Message", "Compiler unavailable"}}})}});
		Expect(records[0].source.scheme.empty() && records[0].line == 0 && records[0].column == 0 &&
		           records[0].message == "Compiler unavailable",
		       "Unmapped tool diagnostics became navigable source");
		std::filesystem::remove_all(library);
		std::filesystem::create_directories(library);
		VirtualFileSystem rebuilt;
		Done(rebuilt.Mount(projectScheme, assets, MountAccess::ReadWrite));
		Done(rebuilt.Mount(editorLibraryScheme, library, MountAccess::ReadWrite));
		auto rebuiltDatabase = Take(AssetDatabase::Open(rebuilt));
		auto rebuiltInput = Take(CollectScriptProjectInput(
		    rebuilt, *rebuiltDatabase, root / "Framework/Ursa.Runtime.dll", root / "Framework/Ursa.Editor.dll"));
		Take(ScriptIdeProject::Generate(rebuilt, rebuiltInput));
		Expect(Take(rebuiltDatabase->Metadata(Path("prj:Scripts/Editor/Renamed.cs"))).guid == guid,
		       "Rebuilding Library changed existing source metadata");
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << " [" << root.string() << "]\n";
		std::filesystem::remove_all(root);
		return 1;
	}
}
