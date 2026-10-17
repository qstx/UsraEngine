#include <Scripting/ExternalScriptEditor.hpp>
#include <Application/EditorApplication.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#endif

namespace
{
using namespace Ursa;
using namespace Ursa::Editor;

class IdleEditor final : public EditorApplication
{
public:
	using EditorApplication::OpenCSharpProject;
	using EditorApplication::ScriptDebugInfo;
};

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
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
	if (argumentCount == 2 && std::wstring(arguments[1]) == L"--sleep")
	{
		Sleep(4000);
		return 0;
	}
	if (argumentCount > 2 && std::wstring(arguments[1]) == L"--echo")
	{
		nlohmann::json array = nlohmann::json::array();
		for (Int32 index = 3; index < argumentCount; ++index)
		{
			array.push_back(ToStdString(PathToUrsaString(std::filesystem::path(arguments[index]))));
		}
		std::ofstream output(std::filesystem::path(arguments[2]), std::ios::binary);
		output << array.dump();
		return output ? 0 : 1;
	}
	const auto root = std::filesystem::temp_directory_path() /
	                  ("UrsaExternalEditor-" + ToStdString(ResourceId::Generate().ToString()));
	try
	{
		IdleEditor idleEditor;
		const String info = idleEditor.ScriptDebugInfo();
		Expect(info.find("Project: None") != String::npos && ExternalScriptEditor::ProcessId() != 0,
		       "Idle debug information does not identify the current process");
#if defined(URSA_ENABLE_DOTNET)
		Expect(info.find("CoreCLR: not initialized") != String::npos, "Idle host was reported as initialized");
#else
		Expect(info.find("CoreCLR: disabled") != String::npos && !idleEditor.OpenCSharpProject(),
		       "Native-only Editor exposes a working managed project action");
#endif
		std::filesystem::create_directories(root / "Assets");
		ScriptIdeWorkspace workspace{root / "Library/ScriptIDE/Ursa.Scripts.sln",
		                             root / "Library/ScriptIDE/Ursa.Scripts.code-workspace", root};
		ScriptSourceLocation source{root / std::filesystem::u8path("Assets/脚本 (sample).cs"), 42, 9};
		ScriptEditorConfig custom{ScriptEditorKind::Custom,
		                          "Example",
		                          CurrentExecutablePath(),
		                          {"{solution}"},
		                          {"{solution}", "{file}", "{line}", "{column}"}};
		Expect(static_cast<bool>(ExternalScriptEditor::Save(root / "Preferences", custom)),
		       "Cannot save IDE preference");
		auto loaded = Take(ExternalScriptEditor::Load(root / "Preferences"));
		Expect(loaded.kind == custom.kind && loaded.executable == custom.executable &&
		           loaded.sourceArguments == custom.sourceArguments,
		       "IDE preferences did not round trip");
		std::filesystem::create_directories(root / "Library");
		std::filesystem::remove_all(root / "Library");
		Expect(Take(ExternalScriptEditor::Load(root / "Preferences")).executable == custom.executable,
		       "Library deletion removed the user's IDE selection");
		std::ofstream(root / "Preferences/ExternalTools.json") << "{\"Version\":2,\"Kind\":0}";
		Expect(!ExternalScriptEditor::Load(root / "Preferences"), "Invalid preferences were silently accepted");
		Expect(static_cast<bool>(ExternalScriptEditor::Save(root / "Preferences", custom)),
		       "Preference recovery failed");
		const auto discovered = ExternalScriptEditor::Discover(
		    [&]
		    {
			    return Vector<ScriptEditorConfig>{{ScriptEditorKind::Rider, "Test IDE", CurrentExecutablePath()},
			                                      {ScriptEditorKind::Rider, "Duplicate", CurrentExecutablePath()},
			                                      {ScriptEditorKind::Code, "Uninstalled", root / "missing.exe"}};
		    });
		Expect(discovered.size() == 1 && discovered[0].kind == ScriptEditorKind::Rider,
		       "Discovery retained duplicate or unavailable tools");
		auto command = Take(ExternalScriptEditor::Command(loaded, workspace, source));
		Expect(command.arguments[1] == PathToUrsaString(source.file) && command.arguments[2] == "42" &&
		           command.arguments[3] == "9",
		       "Custom arguments lost source or line/column");
		bool launched = false;
		Expect(static_cast<bool>(ExternalScriptEditor::Open(loaded, workspace, source,
		                                                    [&](const auto& request)
		                                                    {
			                                                    launched = request.arguments == command.arguments;
			                                                    return Result<void>::Success();
		                                                    })) &&
		           launched,
		       "Open bypassed the injected launcher");
		loaded.sourceArguments = {"{unknown}"};
		Expect(!ExternalScriptEditor::Command(loaded, workspace, source) &&
		           !ExternalScriptEditor::Save(root / "Preferences", loaded),
		       "Unknown placeholders were accepted");
		for (const auto kind : {ScriptEditorKind::VisualStudio, ScriptEditorKind::Rider, ScriptEditorKind::Code})
		{
			auto config = custom;
			config.kind = kind;
			const auto request = Take(ExternalScriptEditor::Command(config, workspace, source));
			Expect(request.workingDirectory == root, "IDE opened with a different project context");
			if (kind == ScriptEditorKind::VisualStudio)
			{
				Expect(request.executable.filename() == "UrsaIdeBridge.exe" && request.timeoutMilliseconds == 30000 &&
				           request.arguments[1] == PathToUrsaString(workspace.solution) && request.arguments[3] == "42",
				       "Visual Studio request lost its solution or timeout");
			}
			if (kind == ScriptEditorKind::Rider)
			{
				Expect(request.arguments[1] == "--line" && request.arguments[3] == "--column" &&
				           request.arguments.back() == PathToUrsaString(source.file),
				       "Rider navigation protocol differs");
			}
			if (kind == ScriptEditorKind::Code)
			{
				Expect(request.arguments[1] == PathToUrsaString(workspace.codeWorkspace) &&
				           request.arguments.back() == PathToUrsaString(source.file) + ":42:9",
				       "Code navigation protocol differs");
			}
		}
		Vector<String> values = {"", "with spaces", "a\"b", "C:\\tail\\", "脚本", "$(never); & echo"};
		ScriptEditorCommand echo{CurrentExecutablePath(), root, {"--echo", PathToUrsaString(root / "Echo.json")}, 3000};
		echo.arguments.insert(echo.arguments.end(), values.begin(), values.end());
		Expect(static_cast<bool>(ExternalScriptEditor::Launch(echo)), "Could not run the argument echo process");
		std::ifstream output(root / "Echo.json");
		const auto echoed = nlohmann::json::parse(output);
		output.close();
		for (Size index = 0; index < values.size(); ++index)
		{
			Expect(echoed.at(index).get<std::string>() == ToStdString(values[index]),
			       "Windows argument quoting changed a value");
		}
		ScriptEditorCommand slow{CurrentExecutablePath(), root, {"--sleep"}, 100};
		const auto timeoutStart = std::chrono::steady_clock::now();
		Expect(!ExternalScriptEditor::Launch(slow) &&
		           std::chrono::steady_clock::now() - timeoutStart < std::chrono::seconds(2),
		       "Timed helper did not return promptly");
		Expect(!ExternalScriptEditor::Launch({root / "missing.exe", root, {}}), "Missing executable was launched");
		VirtualFileSystem filesystem;
		Expect(static_cast<bool>(filesystem.Mount(projectScheme, root / "Assets", MountAccess::ReadWrite)),
		       "Cannot mount sources");
		std::ofstream(source.file) << "class Example {}";
		auto location = Take(
		    ExternalScriptEditor::ResolveSource(filesystem, VirtualPath::Parse("prj:脚本 (sample).cs").value, 42, 9));
		Expect(std::filesystem::equivalent(location.file, source.file) && location.line == 42 && location.column == 9,
		       "VFS navigation lost its source");
		Expect(
		    !ExternalScriptEditor::ResolveSource(filesystem, VirtualPath::Parse("prj:missing.cs").value) &&
		        !ExternalScriptEditor::ResolveSource(filesystem, VirtualPath::Parse("ursa:other.cs").value) &&
		        !ExternalScriptEditor::ResolveSource(filesystem, VirtualPath::Parse("prj:脚本 (sample).cs").value, 0),
		    "Invalid navigation source was accepted");
		std::cout << "Discovered " << ExternalScriptEditor::Discover().size() << " installed IDE(s)\n";
		std::filesystem::remove_all(root);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		std::filesystem::remove_all(root);
		return 1;
	}
}
