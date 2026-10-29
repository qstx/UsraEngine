#include <Scripting/ExternalScriptEditor.hpp>

#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/RuntimeMountProfile.hpp>
#include <Vfs/DirectoryFileProvider.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

#ifdef _WIN32
String VisualStudioRoots()
{
	wchar_t programFiles[32768]{};
	const auto length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, 32768);
	if (length == 0 || length >= 32768)
	{
		return {};
	}
	const auto executable = std::filesystem::path(programFiles) / "Microsoft Visual Studio/Installer/vswhere.exe";
	SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &security, 0))
	{
		return {};
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = writePipe;
	startup.hStdError = writePipe;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	PROCESS_INFORMATION process{};
	std::wstring command = ExternalScriptEditor::QuoteArgument(executable.wstring()) +
	                       L" -all -products * -utf8 -property installationPath";
	const BOOL started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
	                                    nullptr, nullptr, &startup, &process);
	CloseHandle(writePipe);
	if (!started)
	{
		CloseHandle(readPipe);
		return {};
	}
	CloseHandle(process.hThread);
	const ULONGLONG deadline = GetTickCount64() + 3000;
	String output;
	bool exited = false;
	for (;;)
	{
		DWORD available = 0;
		if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available)
		{
			char buffer[4096];
			DWORD received = 0;
			if (!ReadFile(readPipe, buffer, std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer))), &received,
			              nullptr))
			{
				break;
			}
			output.append(buffer, received);
			if (output.size() > 1024 * 1024)
			{
				break;
			}
			continue;
		}
		if (exited)
		{
			break;
		}
		if (GetTickCount64() >= deadline)
		{
			break;
		}
		exited = WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0;
	}
	DWORD exitCode = 1;
	if (!exited)
	{
		TerminateProcess(process.hProcess, 1);
	}
	else
	{
		GetExitCodeProcess(process.hProcess, &exitCode);
	}
	CloseHandle(readPipe);
	CloseHandle(process.hProcess);
	return exited && exitCode == 0 ? output : String{};
}
#endif

Result<Vector<String>> Expand(const Vector<String>& patterns, const ScriptIdeWorkspace& workspace,
                              const std::optional<ScriptSourceLocation>& source)
{
	const Map<String, String> values = {
	    {"solution", PathToUrsaString(workspace.solution)},
	    {"workspace", PathToUrsaString(workspace.codeWorkspace)},
	    {"projectRoot", PathToUrsaString(workspace.projectRoot)},
	    {"file", source ? PathToUrsaString(source->file) : String{}},
	    {"line", source ? ToUrsaString(std::to_string(source->line)) : String("1")},
	    {"column", source ? ToUrsaString(std::to_string(source->column)) : String("1")}};
	Vector<String> expanded;
	for (const auto& pattern : patterns)
	{
		String argument;
		for (Size position = 0; position < pattern.size();)
		{
			if (pattern[position] != '{')
			{
				argument.push_back(pattern[position++]);
				continue;
			}
			const auto end = pattern.find('}', position + 1);
			const auto key = end == String::npos ? String{} : pattern.substr(position + 1, end - position - 1);
			const auto found = values.find(key);
			if (found == values.end())
			{
				return Result<Vector<String>>::Failure("Unknown external editor argument placeholder: " + pattern);
			}
			argument += found->second;
			position = end + 1;
		}
		if (argument.find('\0') != String::npos)
		{
			return Result<Vector<String>>::Failure("External editor arguments cannot contain NUL");
		}
		expanded.push_back(std::move(argument));
	}
	return Result<Vector<String>>::Success(std::move(expanded));
}

Json ConfigJson(const ScriptEditorConfig& config)
{
	Json document = {{"Version", 1},
	                 {"Kind", static_cast<Int32>(config.kind)},
	                 {"Name", ToStdString(config.name)},
	                 {"Executable", ToStdString(PathToUrsaString(config.executable))},
	                 {"ProjectArguments", Json::array()},
	                 {"SourceArguments", Json::array()}};
	for (const auto& value : config.projectArguments)
	{
		document["ProjectArguments"].push_back(ToStdString(value));
	}
	for (const auto& value : config.sourceArguments)
	{
		document["SourceArguments"].push_back(ToStdString(value));
	}
	return document;
}
} // namespace

Result<std::filesystem::path> ExternalScriptEditor::PreferencesDirectory()
{
	auto root = ResolveUserDataDirectory("Ursa");
	return root ? Result<std::filesystem::path>::Success(root.value / "Editor")
	            : Result<std::filesystem::path>::Failure(root.error);
}

UInt32 ExternalScriptEditor::ProcessId()
{
#ifdef _WIN32
	return GetCurrentProcessId();
#else
	return 0;
#endif
}

Result<ScriptEditorConfig> ExternalScriptEditor::Load(const std::filesystem::path& directory)
{
	std::error_code error;
	if (!std::filesystem::exists(directory / "ExternalTools.json", error) && !error)
	{
		return Result<ScriptEditorConfig>::Success({});
	}
	VirtualFileSystem files;
	auto mounted = files.Mount("preferences", directory, MountAccess::ReadOnly);
	if (!mounted)
	{
		return Result<ScriptEditorConfig>::Failure(mounted.error);
	}
	auto text = files.ReadText(VirtualPath::Parse("preferences:ExternalTools.json").value);
	if (!text)
	{
		return Result<ScriptEditorConfig>::Failure(text.error);
	}
	try
	{
		const auto document = Json::parse(ToStdString(text.value));
		const Int32 kind = document.at("Kind").get<Int32>();
		if (document.at("Version") != 1 || kind < 0 || kind > static_cast<Int32>(ScriptEditorKind::Custom))
		{
			return Result<ScriptEditorConfig>::Failure(
			    "Unsupported External Script Editor preferences version or kind");
		}
		if (!document.at("ProjectArguments").is_array() || !document.at("SourceArguments").is_array())
		{
			return Result<ScriptEditorConfig>::Failure("External editor arguments must be arrays of strings");
		}
		ScriptEditorConfig config;
		config.kind = static_cast<ScriptEditorKind>(kind);
		config.name = ToUrsaString(document.at("Name").get<std::string>());
		config.executable = std::filesystem::u8path(document.at("Executable").get<std::string>());
		config.projectArguments.clear();
		config.sourceArguments.clear();
		for (const auto& value : document.at("ProjectArguments"))
		{
			config.projectArguments.push_back(ToUrsaString(value.get<std::string>()));
		}
		for (const auto& value : document.at("SourceArguments"))
		{
			config.sourceArguments.push_back(ToUrsaString(value.get<std::string>()));
		}
		if (!Expand(config.projectArguments, {}, {}) || !Expand(config.sourceArguments, {}, {}))
		{
			return Result<ScriptEditorConfig>::Failure(
			    "External Script Editor preferences contain invalid placeholders");
		}
		return Result<ScriptEditorConfig>::Success(std::move(config));
	}
	catch (const std::exception& exception)
	{
		return Result<ScriptEditorConfig>::Failure("Cannot read External Script Editor preferences: " +
		                                           ToUrsaString(exception.what()));
	}
}

Result<void> ExternalScriptEditor::Save(const std::filesystem::path& directory, const ScriptEditorConfig& config)
{
	if (!Expand(config.projectArguments, {}, {}) || !Expand(config.sourceArguments, {}, {}))
	{
		return Result<void>::Failure("Unknown External Script Editor argument placeholder");
	}
	auto provider = DirectoryFileProvider::Open(directory, true);
	if (!provider)
	{
		return Result<void>::Failure(provider.error);
	}
	const auto text = ConfigJson(config).dump(2);
	const auto* bytes = reinterpret_cast<const UInt8*>(text.data());
	return provider.value->WriteAtomic("ExternalTools.json", Vector<UInt8>(bytes, bytes + text.size()), true);
}

Vector<ScriptEditorConfig> ExternalScriptEditor::Discover(const Discovery& discovery)
{
	(void)discovery;
	Vector<ScriptEditorConfig> result;
#ifdef _WIN32
	const auto add = [&](ScriptEditorKind kind, const std::filesystem::path& executable)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(executable, error) ||
		    std::any_of(result.begin(), result.end(),
		                [&](const auto& entry) { return entry.executable == executable; }))
		{
			return;
		}
		result.push_back({kind, PathToUrsaString(executable), executable});
	};
	if (discovery)
	{
		for (const auto& candidate : discovery())
		{
			if (candidate.kind == ScriptEditorKind::VisualStudio || candidate.kind == ScriptEditorKind::Rider ||
			    candidate.kind == ScriptEditorKind::Code)
			{
				add(candidate.kind, candidate.executable);
			}
		}
		return result;
	}
	const String roots = VisualStudioRoots();
	for (Size start = 0; start < roots.size();)
	{
		const auto end = roots.find('\n', start);
		String path = roots.substr(start, end == String::npos ? String::npos : end - start);
		if (!path.empty() && path.back() == '\r')
		{
			path.pop_back();
		}
		if (!path.empty())
		{
			add(ScriptEditorKind::VisualStudio, PathFromUrsaString(path) / "Common7/IDE/devenv.exe");
		}
		if (end == String::npos)
		{
			break;
		}
		start = end + 1;
	}
	for (const auto& entry :
	     Vector<std::pair<ScriptEditorKind, std::wstring>>{{ScriptEditorKind::VisualStudio, L"devenv.exe"},
	                                                       {ScriptEditorKind::Code, L"Code.exe"},
	                                                       {ScriptEditorKind::Rider, L"rider64.exe"}})
	{
		for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
		{
			const auto key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + entry.second;
			wchar_t path[32768]{};
			DWORD bytes = sizeof(path);
			if (RegGetValueW(hive, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &bytes) == ERROR_SUCCESS)
			{
				add(entry.first, path);
			}
		}
		wchar_t path[32768]{};
		if (SearchPathW(nullptr, entry.second.c_str(), nullptr, 32768, path, nullptr) > 0)
		{
			add(entry.first, path);
		}
	}
	// VS also registers custom installation roots in its compatibility registry.
	for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY})
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7", 0, KEY_READ | view,
		                  &key) != ERROR_SUCCESS)
		{
			continue;
		}
		for (DWORD index = 0;; ++index)
		{
			wchar_t name[256]{};
			wchar_t path[32768]{};
			DWORD nameSize = 256;
			DWORD bytes = sizeof(path);
			const LSTATUS status =
			    RegEnumValueW(key, index, name, &nameSize, nullptr, nullptr, reinterpret_cast<BYTE*>(path), &bytes);
			if (status == ERROR_NO_MORE_ITEMS)
			{
				break;
			}
			if (status == ERROR_SUCCESS)
			{
				add(ScriptEditorKind::VisualStudio, std::filesystem::path(path) / "Common7/IDE/devenv.exe");
			}
		}
		RegCloseKey(key);
	}
	for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
	{
		for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY})
		{
			HKEY uninstall = nullptr;
			if (RegOpenKeyExW(hive, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view,
			                  &uninstall) != ERROR_SUCCESS)
			{
				continue;
			}
			for (DWORD index = 0;; ++index)
			{
				wchar_t subkey[256]{};
				DWORD length = 256;
				const auto status =
				    RegEnumKeyExW(uninstall, index, subkey, &length, nullptr, nullptr, nullptr, nullptr);
				if (status == ERROR_NO_MORE_ITEMS)
				{
					break;
				}
				if (status != ERROR_SUCCESS)
				{
					continue;
				}
				wchar_t name[1024]{};
				wchar_t location[32768]{};
				DWORD nameBytes = sizeof(name);
				DWORD locationBytes = sizeof(location);
				if (RegGetValueW(uninstall, subkey, L"DisplayName", RRF_RT_REG_SZ, nullptr, name, &nameBytes) !=
				        ERROR_SUCCESS ||
				    RegGetValueW(uninstall, subkey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, location,
				                 &locationBytes) != ERROR_SUCCESS)
				{
					continue;
				}
				if (std::wstring(name).find(L"JetBrains Rider") != std::wstring::npos)
				{
					add(ScriptEditorKind::Rider, std::filesystem::path(location) / "bin/rider64.exe");
				}
				else if (std::wstring(name).find(L"Visual Studio Code") != std::wstring::npos)
				{
					add(ScriptEditorKind::Code, std::filesystem::path(location) / "Code.exe");
				}
			}
			RegCloseKey(uninstall);
		}
	}
#endif
	return result;
}

Result<ScriptSourceLocation> ExternalScriptEditor::ResolveSource(VirtualFileSystem& filesystem, const VirtualPath& path,
                                                                 UInt32 line, UInt32 column)
{
	if (path.scheme != projectScheme || PathFromUrsaString(path.relative).extension() != ".cs" || line == 0 ||
	    column == 0)
	{
		return Result<ScriptSourceLocation>::Failure(
		    "Source navigation requires an existing project .cs file and positive line/column");
	}
	auto native = filesystem.ResolveForExternalToolInterop(path);
	return native ? Result<ScriptSourceLocation>::Success({native.value, line, column})
	              : Result<ScriptSourceLocation>::Failure(native.error);
}

Result<ScriptEditorCommand> ExternalScriptEditor::Command(const ScriptEditorConfig& config,
                                                          const ScriptIdeWorkspace& workspace,
                                                          const std::optional<ScriptSourceLocation>& source)
{
	if (config.kind == ScriptEditorKind::None || config.executable.empty())
	{
		return Result<ScriptEditorCommand>::Failure("Choose an External Script Editor in Edit > External Tools");
	}
	ScriptEditorCommand command{config.executable, workspace.projectRoot};
	const auto& visualStudioSolution =
	    workspace.visualStudioSolution.empty() ? workspace.solution : workspace.visualStudioSolution;
	if (source && (source->line == 0 || source->column == 0))
	{
		return Result<ScriptEditorCommand>::Failure("Source line and column must be positive");
	}
	if (config.kind == ScriptEditorKind::VisualStudio && source)
	{
		command.executable = CurrentExecutablePath().parent_path() / "UrsaIdeBridge.exe";
		command.arguments = {PathToUrsaString(config.executable), PathToUrsaString(visualStudioSolution),
		                     PathToUrsaString(source->file), ToUrsaString(std::to_string(source->line)),
		                     ToUrsaString(std::to_string(source->column))};
		command.timeoutMilliseconds = 30000;
	}
	else if (config.kind == ScriptEditorKind::Rider)
	{
		command.arguments = {PathToUrsaString(workspace.solution)};
		if (source)
		{
			command.arguments.insert(command.arguments.end(),
			                         {"--line", ToUrsaString(std::to_string(source->line)), "--column",
			                          ToUrsaString(std::to_string(source->column)), PathToUrsaString(source->file)});
		}
	}
	else if (config.kind == ScriptEditorKind::Code)
	{
		command.arguments = {"--reuse-window", PathToUrsaString(workspace.codeWorkspace)};
		if (source)
		{
			command.arguments.push_back("--goto");
			command.arguments.push_back(PathToUrsaString(source->file) + ":" +
			                            ToUrsaString(std::to_string(source->line)) + ":" +
			                            ToUrsaString(std::to_string(source->column)));
		}
	}
	else if (config.kind == ScriptEditorKind::VisualStudio)
	{
		command.arguments = {PathToUrsaString(visualStudioSolution)};
	}
	else
	{
		auto arguments = Expand(source ? config.sourceArguments : config.projectArguments, workspace, source);
		if (!arguments)
		{
			return Result<ScriptEditorCommand>::Failure(arguments.error);
		}
		command.arguments = std::move(arguments.value);
	}
	return Result<ScriptEditorCommand>::Success(std::move(command));
}

Result<void> ExternalScriptEditor::Open(const ScriptEditorConfig& config, const ScriptIdeWorkspace& workspace,
                                        const std::optional<ScriptSourceLocation>& source, const Launcher& launcher)
{
	auto command = Command(config, workspace, source);
	if (!command)
	{
		return Result<void>::Failure(command.error);
	}
	auto opened = launcher ? launcher(command.value) : Launch(command.value);
	if (!opened && config.kind == ScriptEditorKind::VisualStudio)
	{
		return Result<void>::Failure("Visual Studio navigation failed; retry after the solution finishes loading. " +
		                             opened.error);
	}
	return opened;
}

std::wstring ExternalScriptEditor::QuoteArgument(const std::wstring& argument)
{
	std::wstring result(1, L'"');
	Size backslashes = 0;
	for (wchar_t character : argument)
	{
		if (character == L'\\')
		{
			++backslashes;
			continue;
		}
		result.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
		backslashes = 0;
		result.push_back(character);
	}
	result.append(backslashes * 2, L'\\');
	return result + L'"';
}

Result<void> ExternalScriptEditor::Launch(const ScriptEditorCommand& command)
{
#ifdef _WIN32
	std::error_code error;
	if (!std::filesystem::is_regular_file(command.executable, error) || command.executable.extension() != L".exe")
	{
		return Result<void>::Failure("External editor executable is missing or is not an .exe: " +
		                             PathToUrsaString(command.executable));
	}
	std::wstring text = QuoteArgument(command.executable.wstring());
	for (const auto& argument : command.arguments)
	{
		text += L" " + QuoteArgument(PathFromUrsaString(argument).wstring());
	}
	Vector<wchar_t> mutableText(text.data(), text.data() + text.size());
	mutableText.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	const DWORD flags = command.timeoutMilliseconds ? CREATE_NO_WINDOW : 0;
	if (!CreateProcessW(command.executable.c_str(), mutableText.data(), nullptr, nullptr, FALSE, flags, nullptr,
	                    command.workingDirectory.c_str(), &startup, &process))
	{
		return Result<void>::Failure("Cannot start external editor (Windows error " +
		                             ToUrsaString(std::to_string(GetLastError())) + ")");
	}
	CloseHandle(process.hThread);
	Result<void> result;
	if (command.timeoutMilliseconds)
	{
		const DWORD wait = WaitForSingleObject(process.hProcess, command.timeoutMilliseconds);
		DWORD exitCode = 1;
		if (wait != WAIT_OBJECT_0)
		{
			TerminateProcess(process.hProcess, 1);
			result = Result<void>::Failure("External tool timed out: " + PathToUrsaString(command.executable));
		}
		else if (!GetExitCodeProcess(process.hProcess, &exitCode) || exitCode != 0)
		{
			result = Result<void>::Failure("External tool failed (exit " + ToUrsaString(std::to_string(exitCode)) +
			                               "): " + PathToUrsaString(command.executable));
		}
	}
	CloseHandle(process.hProcess);
	return result;
#else
	(void)command;
	return Result<void>::Failure("External script editors are currently supported on Windows");
#endif
}
} // namespace Ursa::Editor
