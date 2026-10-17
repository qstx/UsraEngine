#include <Application/EditorApplication.hpp>

#include <Core/StringInterop.hpp>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

template <Size count> void SetInput(Array<char, count>& buffer, const String& text)
{
	const Size length = std::min(text.size(), count - 1);
	std::memcpy(buffer.data(), text.data(), length);
	buffer[length] = '\0';
}

String ArgumentsJson(const Vector<String>& arguments)
{
	Json array = Json::array();
	for (const auto& argument : arguments)
	{
		array.push_back(ToStdString(argument));
	}
	return ToUrsaString(array.dump());
}

Vector<String> ParseArguments(const char* text)
{
	const auto array = Json::parse(text);
	if (!array.is_array())
	{
		throw std::runtime_error("Arguments must be a JSON array of strings");
	}
	Vector<String> result;
	for (const auto& argument : array)
	{
		result.push_back(ToUrsaString(argument.get<std::string>()));
	}
	return result;
}
} // namespace

Result<void> EditorApplication::OpenCSharpProject(const VirtualPath& source, UInt32 line, UInt32 column,
                                                  bool openEditor)
{
#if defined(URSA_ENABLE_DOTNET)
	if (!scriptBuildService)
	{
		return Result<void>::Failure("Open a loose project with .NET scripting enabled first");
	}
	if (!source.scheme.empty() && (source.scheme != projectScheme || line == 0 || column == 0))
	{
		return Result<void>::Failure("Source navigation requires a project file and positive line/column");
	}
	if (scriptIdeJob.valid() || scriptEditorJob.valid() || ideGenerationRequested)
	{
		return Result<void>::Failure("A script IDE request is pending; retry when it finishes");
	}
	ideGenerationRequested = true;
	ideOpenRequested = openEditor;
	ideSource = source;
	ideSourceLine = line;
	ideSourceColumn = column;
	scriptIdeStatus = "等待同步 C# 工程……";
	return {};
#else
	(void)source;
	(void)line;
	(void)column;
	(void)openEditor;
	return Result<void>::Failure("This build does not enable .NET scripting");
#endif
}

Result<void> EditorApplication::OpenScriptDiagnostic(Size index)
{
	if (index >= scriptDiagnostics.size())
	{
		return Result<void>::Failure("Script diagnostic is no longer available");
	}
	const auto& diagnostic = scriptDiagnostics[index];
	if (diagnostic.source.scheme.empty() || diagnostic.line == 0 || diagnostic.column == 0)
	{
		return Result<void>::Failure("This diagnostic has no navigable project source location");
	}
	return OpenCSharpProject(diagnostic.source, diagnostic.line, diagnostic.column);
}

void EditorApplication::SetScriptEditorInput(const ScriptEditorConfig& config)
{
	scriptEditorConfig = config;
	SetInput(scriptEditorExecutableInput, PathToUrsaString(config.executable));
	SetInput(scriptEditorProjectArgumentsInput, ArgumentsJson(config.projectArguments));
	SetInput(scriptEditorSourceArgumentsInput, ArgumentsJson(config.sourceArguments));
}

void EditorApplication::PumpScriptIde()
{
	if (!scriptEditorPreferencesLoaded)
	{
		scriptEditorPreferencesLoaded = true;
		auto directory = ExternalScriptEditor::PreferencesDirectory();
		if (directory)
		{
			scriptEditorPreferencesDirectory = std::move(directory.value);
			auto config = ExternalScriptEditor::Load(scriptEditorPreferencesDirectory);
			if (config)
			{
				SetScriptEditorInput(config.value);
			}
			else
			{
				scriptIdeStatus = config.error;
			}
		}
		else
		{
			scriptIdeStatus = directory.error;
		}
	}
	if (scriptEditorDiscoveryJob.valid() &&
	    scriptEditorDiscoveryJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		discoveredScriptEditors = scriptEditorDiscoveryJob.get();
	}
	if (scriptEditorJob.valid() && scriptEditorJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto opened = scriptEditorJob.get();
		scriptIdeStatus = opened ? "已提交 IDE 打开请求。" : opened.error;
		if (!opened)
		{
			showExternalTools = true;
		}
	}
	if (scriptIdeJob.valid() && scriptIdeJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto generated = scriptIdeJob.get();
		if (!generated)
		{
			scriptIdeStatus = generated.error;
		}
		else
		{
			scriptIdeStatus = "C# 工程已同步：" + PathToUrsaString(generated.value.solution);
			if (ideOpenRequested)
			{
				std::optional<ScriptSourceLocation> source;
				Result<void> ready;
				if (!ideSource.scheme.empty())
				{
					auto location = ExternalScriptEditor::ResolveSource(workspace->Index().Filesystem(), ideSource,
					                                                    ideSourceLine, ideSourceColumn);
					if (location)
					{
						source = std::move(location.value);
					}
					else
					{
						ready = Result<void>::Failure(location.error);
					}
				}
				if (ready && scriptEditorConfig.kind != ScriptEditorKind::None)
				{
					const auto config = scriptEditorConfig;
					const auto generatedWorkspace = generated.value;
					scriptEditorJob =
					    std::async(std::launch::async, [config, generatedWorkspace, source]
					               { return ExternalScriptEditor::Open(config, generatedWorkspace, source); });
				}
				else
				{
					scriptIdeStatus =
					    ready ? String("请在 Edit > External Tools 选择 External Script Editor。") : ready.error;
					showExternalTools = true;
				}
			}
		}
		ideOpenRequested = false;
		ideSource = {};
	}
#if defined(URSA_ENABLE_DOTNET)
	if (ideGenerationRequested && scriptBuildService && !scriptBuildJob.valid() && !scriptCheckJob.valid() &&
	    !scriptIdeJob.valid() && !assetScanJob.valid() &&
	    (!assetImportJob.valid() || assetImportJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready))
	{
		ideGenerationRequested = false;
		scriptIdeStatus = "正在同步 C# 工程……";
		ScriptBuildService* service = scriptBuildService.get();
		scriptIdeJob = std::async(std::launch::async, [service] { return service->GenerateIdeProjects(); });
	}
#endif
}

void EditorApplication::DrawExternalTools()
{
	if (!showExternalTools)
	{
		return;
	}
	if (!scriptEditorDiscoveryRequested)
	{
		scriptEditorDiscoveryRequested = true;
		scriptEditorDiscoveryJob = std::async(std::launch::async, [] { return ExternalScriptEditor::Discover(); });
	}
	ImGui::SetNextWindowSize({760.0f, 540.0f}, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("External Tools", &showExternalTools))
	{
		const char* selected = scriptEditorConfig.kind == ScriptEditorKind::None     ? "Select an editor"
		                       : scriptEditorConfig.kind == ScriptEditorKind::Custom ? "Custom executable"
		                                                                             : scriptEditorConfig.name.c_str();
		if (ImGui::BeginCombo("External Script Editor", selected))
		{
			if (ImGui::Selectable("None", scriptEditorConfig.kind == ScriptEditorKind::None))
			{
				SetScriptEditorInput({});
			}
			for (const auto& config : discoveredScriptEditors)
			{
				if (ImGui::Selectable(config.name.c_str(), config.executable == scriptEditorConfig.executable))
				{
					SetScriptEditorInput(config);
				}
			}
			if (ImGui::Selectable("Custom executable", scriptEditorConfig.kind == ScriptEditorKind::Custom))
			{
				auto custom = scriptEditorConfig;
				custom.kind = ScriptEditorKind::Custom;
				custom.name = "Custom";
				SetScriptEditorInput(custom);
			}
			ImGui::EndCombo();
		}
		ImGui::InputText("Executable", scriptEditorExecutableInput.data(), scriptEditorExecutableInput.size());
		if (scriptEditorConfig.kind == ScriptEditorKind::Custom)
		{
			ImGui::TextWrapped("Arguments are JSON string arrays. Placeholders: {solution}, {workspace}, "
			                   "{projectRoot}, {file}, {line}, {column}.");
			ImGui::InputTextMultiline("Project arguments", scriptEditorProjectArgumentsInput.data(),
			                          scriptEditorProjectArgumentsInput.size(), {-1, 50});
			ImGui::InputTextMultiline("Source arguments", scriptEditorSourceArgumentsInput.data(),
			                          scriptEditorSourceArgumentsInput.size(), {-1, 50});
			ImGui::TextUnformatted("Source position support depends on the configured arguments.");
		}
		if (ImGui::Button("Save preferences"))
		{
			try
			{
				auto config = scriptEditorConfig;
				config.executable = PathFromUrsaString(scriptEditorExecutableInput.data());
				if (config.kind == ScriptEditorKind::Custom)
				{
					config.projectArguments = ParseArguments(scriptEditorProjectArgumentsInput.data());
					config.sourceArguments = ParseArguments(scriptEditorSourceArgumentsInput.data());
				}
				auto saved = scriptEditorPreferencesDirectory.empty()
				                 ? Result<void>::Failure("User preferences directory is unavailable")
				                 : ExternalScriptEditor::Save(scriptEditorPreferencesDirectory, config);
				scriptIdeStatus = saved ? String("外部编辑器设置已保存。") : saved.error;
				if (saved)
				{
					SetScriptEditorInput(config);
				}
			}
			catch (const std::exception& error)
			{
				scriptIdeStatus = ToUrsaString(error.what());
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!scriptBuildService || scriptIdeJob.valid() || scriptEditorJob.valid());
		if (ImGui::Button("Open C# Project"))
		{
			auto opened = OpenCSharpProject();
			if (!opened)
			{
				scriptIdeStatus = opened.error;
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(scriptEditorDiscoveryJob.valid());
		if (ImGui::Button("Refresh installed editors"))
		{
			scriptEditorDiscoveryRequested = false;
		}
		ImGui::EndDisabled();
		ImGui::Separator();
		ImGui::TextWrapped("%s", scriptIdeStatus.c_str());
		ImGui::Separator();
		const String debugInfo = ScriptDebugInfo();
		ImGui::TextWrapped("%s", debugInfo.c_str());
		if (ImGui::Button("Copy debug information"))
		{
			ImGui::SetClipboardText(debugInfo.c_str());
		}
	}
	ImGui::End();
}

String EditorApplication::ScriptDebugInfo() const
{
	String info = "Editor PID: " + ToUrsaString(std::to_string(ExternalScriptEditor::ProcessId())) + "\n";
	info += "Project: " + (workspace ? PathToUrsaString(workspace->ProjectRoot()) : String("None")) + "\n";
#if defined(URSA_ENABLE_DOTNET)
	info += managedHost ? "CoreCLR: initialized\n" : "CoreCLR: not initialized; compile valid scripts first\n";
	info += "Editor script build: Debug, Portable PDB\n";
	if (workspace)
	{
		for (const auto& group : {String("Game/Ursa.Game"), String("Editor/Ursa.EditorScripts")})
		{
			auto path = VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Assemblies/" + group + ".pdb");
			auto exists = workspace->Index().Filesystem().Exists(path.value);
			info += group + (exists && exists.value ? ": PDB available\n" : ": no published PDB\n");
		}
	}
	info += "Attach a compatible CoreCLR managed debugger to this PID. Set a script breakpoint, then Run or invoke an "
	        "editor extension.\n"
	        "After Stop and recompilation, use current symbols. If the debugger retains an old session, detach and "
	        "follow the reload diagnostic before reattaching.";
#else
	info += "CoreCLR: disabled in this build; enable URSA_ENABLE_DOTNET for script editing and debugging.";
#endif
	return info;
}
} // namespace Ursa::Editor
