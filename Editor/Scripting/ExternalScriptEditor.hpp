#pragma once

#include <Scripting/ScriptIdeProject.hpp>
#include <functional>
#include <optional>

namespace Ursa::Editor
{
enum class ScriptEditorKind
{
	None,
	VisualStudio,
	Rider,
	Code,
	Custom
};

struct ScriptEditorConfig
{
	ScriptEditorKind kind = ScriptEditorKind::None;
	String name;
	std::filesystem::path executable;
	Vector<String> projectArguments = {"{solution}"};
	Vector<String> sourceArguments = {"{file}"};
};

struct ScriptSourceLocation
{
	std::filesystem::path file;
	UInt32 line = 1;
	UInt32 column = 1;
};

struct ScriptEditorCommand
{
	std::filesystem::path executable;
	std::filesystem::path workingDirectory;
	Vector<String> arguments;
	UInt32 timeoutMilliseconds = 0;
};

class ExternalScriptEditor final
{
public:
	using Launcher = std::function<Result<void>(const ScriptEditorCommand&)>;
	using Discovery = std::function<Vector<ScriptEditorConfig>()>;

	static Result<std::filesystem::path> PreferencesDirectory();

	static UInt32 ProcessId();

	static Result<ScriptEditorConfig> Load(const std::filesystem::path& directory);

	static Result<void> Save(const std::filesystem::path& directory, const ScriptEditorConfig& config);

	static Vector<ScriptEditorConfig> Discover(const Discovery& discovery = {});

	static Result<ScriptSourceLocation> ResolveSource(VirtualFileSystem& filesystem, const VirtualPath& path,
	                                                  UInt32 line = 1, UInt32 column = 1);

	static Result<ScriptEditorCommand> Command(const ScriptEditorConfig& config, const ScriptIdeWorkspace& workspace,
	                                           const std::optional<ScriptSourceLocation>& source = {});

	static Result<void> Open(const ScriptEditorConfig& config, const ScriptIdeWorkspace& workspace,
	                         const std::optional<ScriptSourceLocation>& source = {}, const Launcher& launcher = {});

	static Result<void> Launch(const ScriptEditorCommand& command);

	static std::wstring QuoteArgument(const std::wstring& argument);
};
} // namespace Ursa::Editor
