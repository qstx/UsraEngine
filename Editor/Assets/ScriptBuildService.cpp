#include <Assets/ScriptBuildService.hpp>

#include <Core/StringInterop.hpp>
#include <Core/Memory/ContainerAllocator.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Ursa::Editor
{
namespace
{
using Json = nlohmann::json;

String ContentFingerprint(const Vector<UInt8>& bytes)
{
	UInt64 hash = 14695981039346656037ull;
	for (UInt8 value : bytes)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	std::ostringstream result;
	result << std::hex << std::setfill('0') << std::setw(16) << hash;
	return ToUrsaString(result.str());
}

Vector<UInt8> JsonBytes(const Json& value)
{
	const std::string text = value.dump(2);
	const auto* bytes = reinterpret_cast<const UInt8*>(text.data());
	return Vector<UInt8>(bytes, bytes + text.size());
}

struct BuildProcessResult
{
	Int32 exitCode = -1;
	String output;
};

String XmlEscape(const String& value)
{
	String escaped;
	for (char character : value)
	{
		switch (character)
		{
			case '&':
				escaped += "&amp;";
				break;
			case '<':
				escaped += "&lt;";
				break;
			case '>':
				escaped += "&gt;";
				break;
			case '"':
				escaped += "&quot;";
				break;
			case '\'':
				escaped += "&apos;";
				break;
			default:
				escaped.push_back(character);
				break;
		}
	}
	return escaped;
}

#if defined(_WIN32)
std::wstring QuoteArgument(const std::wstring& argument)
{
	std::wstring quoted(1, L'"');
	size_t backslashes = 0;
	for (wchar_t character : argument)
	{
		if (character == L'\\')
		{
			++backslashes;
			continue;
		}
		if (character == L'"')
		{
			quoted.append(backslashes * 2 + 1, L'\\');
			quoted.push_back(L'"');
			backslashes = 0;
			continue;
		}
		quoted.append(backslashes, L'\\');
		backslashes = 0;
		quoted.push_back(character);
	}
	quoted.append(backslashes * 2, L'\\');
	quoted.push_back(L'"');
	return quoted;
}

Result<BuildProcessResult> RunProcess(const std::filesystem::path& executable,
                                      const std::vector<std::wstring>& arguments,
                                      const std::filesystem::path& workingDirectory)
{
	Memory::TemporaryScope processMemory;
	SECURITY_ATTRIBUTES security{};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &security, 0))
	{
		return Result<BuildProcessResult>::Failure("Cannot create the script compiler output pipe");
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = writePipe;
	startup.hStdError = writePipe;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	PROCESS_INFORMATION process{};
	std::wstring command = QuoteArgument(executable.wstring());
	for (const std::wstring& argument : arguments)
	{
		command += L" " + QuoteArgument(argument);
	}
	std::vector<wchar_t, Memory::StlAllocator<wchar_t>> writableCommand(
	    command.begin(), command.end(),
	    Memory::StlAllocator<wchar_t>(processMemory.GetAllocator(), Memory::MemoryTag::Editor));
	writableCommand.push_back(L'\0');
	const BOOL created = CreateProcessW(nullptr, writableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
	                                    nullptr, workingDirectory.c_str(), &startup, &process);
	CloseHandle(writePipe);
	if (!created)
	{
		CloseHandle(readPipe);
		return Result<BuildProcessResult>::Failure("Cannot start the configured .NET compiler executable");
	}

	std::string outputText;
	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead != 0)
	{
		outputText.append(buffer, bytesRead);
	}
	CloseHandle(readPipe);
	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return Result<BuildProcessResult>::Success({static_cast<Int32>(exitCode), ToUrsaString(outputText)});
}

Result<BuildProcessResult> RunDotNet(const std::filesystem::path& executable, const std::filesystem::path& project,
                                     const std::filesystem::path& output, const String& configuration)
{
	return RunProcess(executable,
	                  {L"build", project.wstring(), L"--configuration",
	                   std::filesystem::u8path(ToStdString(configuration)).wstring(), L"--output", output.wstring(),
	                   L"--nologo"},
	                  project.parent_path());
}
#endif

void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
{
	if (from.empty())
	{
		return;
	}
	std::string::size_type position = 0;
	while ((position = text.find(from, position)) != std::string::npos)
	{
		text.replace(position, from.size(), to);
		position += to.size();
	}
}

void HashBytes(UInt64& hash, const void* data, Size size)
{
	const auto* bytes = static_cast<const UInt8*>(data);
	for (Size index = 0; index < size; ++index)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ull;
	}
}

void HashString(UInt64& hash, const String& value)
{
	const std::string text = ToStdString(value);
	HashBytes(hash, text.data(), text.size());
	const UInt8 separator = 0xff;
	HashBytes(hash, &separator, sizeof(separator));
}

Result<Vector<UInt8>> ReadConfiguredFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		return Result<Vector<UInt8>>::Failure("Cannot read configured script build dependency: " +
		                                      PathToUrsaString(path));
	}
	input.seekg(0, std::ios::end);
	const std::streamoff length = input.tellg();
	if (length < 0)
	{
		return Result<Vector<UInt8>>::Failure("Cannot determine script build dependency size");
	}
	input.seekg(0, std::ios::beg);
	Vector<UInt8> bytes(static_cast<Size>(length));
	if (length != 0 && !input.read(reinterpret_cast<char*>(bytes.data()), length))
	{
		return Result<Vector<UInt8>>::Failure("Cannot read configured script build dependency: " +
		                                      PathToUrsaString(path));
	}
	return Result<Vector<UInt8>>::Success(std::move(bytes));
}
} // namespace

ScriptBuildService::ScriptBuildService(VirtualFileSystem& filesystem, AssetDatabase& database,
                                       std::filesystem::path dotnetExecutable, std::filesystem::path runtimeAssembly,
                                       std::filesystem::path editorAssembly,
                                       std::filesystem::path catalogGeneratorAssembly,
                                       std::filesystem::path referenceAssemblyDirectory)
    : filesystem(filesystem), database(database), dotnetExecutable(std::move(dotnetExecutable)),
      runtimeAssembly(std::move(runtimeAssembly)), editorAssembly(std::move(editorAssembly)),
      catalogGeneratorAssembly(std::move(catalogGeneratorAssembly)),
      referenceAssemblyDirectory(std::move(referenceAssemblyDirectory))
{
}

Result<std::unique_ptr<ScriptBuildService>>
ScriptBuildService::Open(VirtualFileSystem& filesystem, AssetDatabase& database, std::filesystem::path dotnetExecutable,
                         std::filesystem::path runtimeAssembly, std::filesystem::path editorAssembly,
                         std::filesystem::path catalogGeneratorAssembly,
                         std::filesystem::path referenceAssemblyDirectory)
{
	if (!filesystem.IsMounted(editorLibraryScheme))
	{
		return Result<std::unique_ptr<ScriptBuildService>>::Failure("ScriptBuild requires a writable lib: mount");
	}
	if (dotnetExecutable.empty() || runtimeAssembly.empty() || editorAssembly.empty() ||
	    catalogGeneratorAssembly.empty() || referenceAssemblyDirectory.empty())
	{
		return Result<std::unique_ptr<ScriptBuildService>>::Failure(
		    "ScriptBuild needs the .NET CLI, Ursa API assemblies, "
		    "catalog generator, and reference pack");
	}
	return Result<std::unique_ptr<ScriptBuildService>>::Success(std::unique_ptr<ScriptBuildService>(
	    new ScriptBuildService(filesystem, database, std::move(dotnetExecutable), std::move(runtimeAssembly),
	                           std::move(editorAssembly), std::move(catalogGeneratorAssembly),
	                           std::move(referenceAssemblyDirectory))));
}

Result<void> ScriptBuildService::GenerateCatalog(const Vector<ScriptSourceAsset>& sources, VirtualPath& catalogPath)
{
	const VirtualPath manifestPath =
	    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Inputs/ScriptCatalog.json").value;
	const VirtualPath catalogDirectory = VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Catalog").value;
	auto catalogOutputDirectory = EnsureToolDirectory(catalogDirectory);
	if (!catalogOutputDirectory)
	{
		return Result<void>::Failure(catalogOutputDirectory.error);
	}
	const std::filesystem::path catalogOutputPath = catalogOutputDirectory.value / "ScriptTypeCatalog.json";
	Json manifest = Json::object();
	manifest["RuntimeAssembly"] = ToStdString(PathToUrsaString(runtimeAssembly));
	manifest["RenderingAssembly"] = ToStdString(PathToUrsaString(runtimeAssembly.parent_path() / "Ursa.Rendering.dll"));
	manifest["EditorAssembly"] = ToStdString(PathToUrsaString(editorAssembly));
	manifest["ReferenceDirectory"] = ToStdString(PathToUrsaString(referenceAssemblyDirectory));
	manifest["OutputPath"] = ToStdString(PathToUrsaString(catalogOutputPath));
	manifest["Sources"] = Json::array();
	for (const ScriptSourceAsset& source : sources)
	{
		auto nativeSource = filesystem.ResolveForExternalToolInterop(source.path);
		if (!nativeSource)
		{
			return Result<void>::Failure("Script source is not available to the compiler: " + source.path.ToString() +
			                             ": " + nativeSource.error);
		}
		Json sourceRecord = Json::object();
		sourceRecord["Path"] = ToStdString(PathToUrsaString(nativeSource.value));
		sourceRecord["VirtualPath"] = ToStdString(source.path.ToString());
		sourceRecord["Guid"] = ToStdString(source.guid.ToString());
		sourceRecord["Group"] = source.group == ScriptAssemblyGroup::Editor ? "Editor" : "Game";
		manifest["Sources"].push_back(std::move(sourceRecord));
	}
	const std::string manifestText = manifest.dump(2);
	Vector<UInt8> manifestBytes;
	manifestBytes.reserve(manifestText.size());
	for (unsigned char character : manifestText)
	{
		manifestBytes.push_back(character);
	}
	auto savedManifest = filesystem.WriteAtomic(manifestPath, manifestBytes);
	if (!savedManifest)
	{
		return Result<void>::Failure("Cannot write the ScriptCatalog manifest: " + savedManifest.error);
	}
	auto nativeManifest = filesystem.ResolveForExternalToolInterop(manifestPath);
	if (!nativeManifest)
	{
		return Result<void>::Failure(nativeManifest.error);
	}
	auto process = RunProcess(dotnetExecutable, {catalogGeneratorAssembly.wstring(), nativeManifest.value.wstring()},
	                          nativeManifest.value.parent_path());
	if (!process)
	{
		return Result<void>::Failure(process.error);
	}
	if (process.value.exitCode != 0)
	{
		std::string diagnostics = ToStdString(process.value.output);
		for (const ScriptSourceAsset& source : sources)
		{
			auto nativeSource = filesystem.ResolveForExternalToolInterop(source.path);
			if (nativeSource)
			{
				ReplaceAll(diagnostics, nativeSource.value.u8string(), ToStdString(source.path.ToString()));
			}
		}
		return Result<void>::Failure("ScriptCatalog generation failed:\n" + ToUrsaString(diagnostics));
	}
	catalogPath = VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Catalog/ScriptTypeCatalog.json").value;
	auto catalogText = filesystem.ReadText(catalogPath);
	if (!catalogText)
	{
		return Result<void>::Failure("ScriptCatalog tool did not write its output: " + catalogText.error);
	}
	try
	{
		const Json document = Json::parse(ToStdString(catalogText.value));
		if (!document.is_object() || document.value("Version", 0u) != 1 || !document.contains("Scripts") ||
		    !document["Scripts"].is_array())
		{
			return Result<void>::Failure("ScriptCatalog output has an invalid versioned structure");
		}
		Vector<ScriptTypeRecord> records;
		Set<String> typeIds;
		Set<Ursa::UInt64> localFileIds;
		for (const Json& item : document["Scripts"])
		{
			if (!item.is_object() || !item.contains("SourceGuid") || !item["SourceGuid"].is_string() ||
			    !item.contains("ScriptType") || !item["ScriptType"].is_string() || !item.contains("Version") ||
			    !item["Version"].is_number_unsigned() || !item.contains("LocalFileId") ||
			    !item["LocalFileId"].is_number_unsigned() || !item.contains("ClrTypeName") ||
			    !item["ClrTypeName"].is_string() || !item.contains("VirtualPath") || !item["VirtualPath"].is_string() ||
			    !item.contains("Group") || !item["Group"].is_string())
			{
				return Result<void>::Failure("ScriptCatalog contains an invalid type record");
			}
			auto guid = ResourceId::Parse(ToUrsaString(item["SourceGuid"].get<std::string>()));
			auto sourcePath = VirtualPath::Parse(ToUrsaString(item["VirtualPath"].get<std::string>()));
			const String scriptType = ToUrsaString(item["ScriptType"].get<std::string>());
			const Ursa::UInt64 localFileId = item["LocalFileId"].get<Ursa::UInt64>();
			const std::string group = item["Group"].get<std::string>();
			const std::string kind = item.value("Kind", std::string{"Behaviour"});
			if (!guid || !sourcePath || scriptType.empty() || localFileId == 0 ||
			    (group != "Game" && group != "Editor") || (kind != "Behaviour" && kind != "ScriptableObject") ||
			    !typeIds.insert(scriptType).second || !localFileIds.insert(localFileId).second)
			{
				return Result<void>::Failure("ScriptCatalog contains a duplicate or invalid stable script identity");
			}
			if (!item.contains("Fields") || !item["Fields"].is_array())
			{
				return Result<void>::Failure("ScriptCatalog contains no serialized field descriptor array");
			}
			Vector<ScriptFieldTypeDescriptor> fields;
			Set<String> fieldNames;
			for (const Json& field : item["Fields"])
			{
				if (!field.is_object() || !field.contains("Name") || !field["Name"].is_string() ||
				    !field.contains("Kind") || !field["Kind"].is_string() || !field.contains("ElementKind") ||
				    !field["ElementKind"].is_string() || !field.contains("TypeName") ||
				    !field["TypeName"].is_string() || !field.contains("DefaultValueJson") ||
				    !field["DefaultValueJson"].is_string() || !field.contains("EnumValues") ||
				    !field["EnumValues"].is_array() || !field.contains("FormerNames") ||
				    !field["FormerNames"].is_array())
				{
					return Result<void>::Failure("ScriptCatalog contains an invalid serialized field descriptor");
				}
				ScriptFieldTypeDescriptor descriptor;
				descriptor.name = ToUrsaString(field["Name"].get<std::string>());
				descriptor.kind = ToUrsaString(field["Kind"].get<std::string>());
				descriptor.elementKind = ToUrsaString(field["ElementKind"].get<std::string>());
				descriptor.typeName = ToUrsaString(field["TypeName"].get<std::string>());
				descriptor.elementTypeName = ToUrsaString(field.value("ElementTypeName", std::string{}));
				descriptor.defaultValueJson = ToUrsaString(field["DefaultValueJson"].get<std::string>());
				try
				{
					(void)Json::parse(ToStdString(descriptor.defaultValueJson));
				}
				catch (const std::exception&)
				{
					return Result<void>::Failure("ScriptCatalog contains an invalid default value for field " +
					                             descriptor.name);
				}
				descriptor.enumUnderlyingType = ToUrsaString(field.value("EnumUnderlyingType", std::string{}));
				for (const Json& value : field.value("EnumNumericValues", Json::array()))
				{
					if (!value.is_number_integer())
					{
						return Result<void>::Failure("Script enum catalog contains a noninteger value");
					}
					descriptor.enumNumericValuesJson.push_back(ToUrsaString(value.dump()));
				}
				for (const Json& value : field["EnumValues"])
				{
					if (!value.is_string())
					{
						return Result<void>::Failure("ScriptCatalog contains an invalid enum value");
					}
					descriptor.enumValues.push_back(ToUrsaString(value.get<std::string>()));
				}
				for (const Json& value : field["FormerNames"])
				{
					if (!value.is_string())
					{
						return Result<void>::Failure("ScriptCatalog contains an invalid former field name");
					}
					descriptor.formerNames.push_back(ToUrsaString(value.get<std::string>()));
				}
				if (descriptor.name.empty() || descriptor.kind.empty() || !fieldNames.insert(descriptor.name).second)
				{
					return Result<void>::Failure("ScriptCatalog contains a duplicate or empty serialized field name");
				}
				fields.push_back(std::move(descriptor));
			}
			records.push_back(
			    {guid.value, scriptType, item["Version"].get<Ursa::UInt32>(), localFileId,
			     ToUrsaString(item["ClrTypeName"].get<std::string>()), sourcePath.value,
			     group == "Editor" ? ScriptAssemblyGroup::Editor : ScriptAssemblyGroup::Game, std::move(fields),
			     kind == "ScriptableObject" ? ScriptTypeKind::ScriptableObject : ScriptTypeKind::Behaviour});
		}
		scriptTypes = std::move(records);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Cannot parse ScriptCatalog output: " + ToUrsaString(error.what()));
	}
}

Result<ScriptProjectPaths> ScriptBuildService::GenerateProjects()
{
	auto refreshed = database.Refresh();
	if (!refreshed)
	{
		return Result<ScriptProjectPaths>::Failure(refreshed.error);
	}
	const String base = String(editorLibraryScheme) + ":ScriptBuild/Generated/";
	const String output = String(editorLibraryScheme) + ":ScriptBuild/Assemblies/";
	auto catalogPath = VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/ScriptTypeCatalog.json");
	if (!catalogPath)
	{
		return Result<ScriptProjectPaths>::Failure(catalogPath.error);
	}
	ScriptProjectPaths projects{
	    VirtualPath::Parse(base + "Ursa.Game.csproj").value, VirtualPath::Parse(base + "Ursa.Editor.csproj").value,
	    VirtualPath::Parse(output + "Game/Ursa.Game.dll").value,
	    VirtualPath::Parse(output + "Editor/Ursa.EditorScripts.dll").value, std::move(catalogPath.value)};
	auto sources = database.ScriptSources();
	auto catalog = GenerateCatalog(sources, projects.typeCatalog);
	if (!catalog)
	{
		return Result<ScriptProjectPaths>::Failure(catalog.error);
	}
	auto game = WriteProject(projects.gameProject, sources, ScriptAssemblyGroup::Game, projects);
	if (!game)
	{
		return Result<ScriptProjectPaths>::Failure(game.error);
	}
	auto editor = WriteProject(projects.editorProject, sources, ScriptAssemblyGroup::Editor, projects);
	if (!editor)
	{
		return Result<ScriptProjectPaths>::Failure(editor.error);
	}
	generatedProjects = projects;
	projectsGenerated = true;
	return Result<ScriptProjectPaths>::Success(std::move(projects));
}

Result<void> ScriptBuildService::WriteProject(const VirtualPath& path, const Vector<ScriptSourceAsset>& sources,
                                              ScriptAssemblyGroup group, const ScriptProjectPaths& projects)
{
	const bool editorProject = group == ScriptAssemblyGroup::Editor;
	const String assemblyName = editorProject ? "Ursa.EditorScripts" : "Ursa.Game";
	String document = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n";
	document += "    <TargetFramework>net10.0</TargetFramework>\n";
	document += "    <OutputType>Library</OutputType>\n";
	document += "    <AssemblyName>" + assemblyName + "</AssemblyName>\n";
	document += "    <Nullable>enable</Nullable>\n";
	document += "    <ImplicitUsings>disable</ImplicitUsings>\n";
	document += "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
	document += "  </PropertyGroup>\n  <ItemGroup>\n";
	for (const ScriptSourceAsset& source : sources)
	{
		if (source.group != group)
		{
			continue;
		}
		auto pathOnDisk = filesystem.ResolveForExternalToolInterop(source.path);
		if (!pathOnDisk)
		{
			return Result<void>::Failure("Script source cannot be passed to the compiler: " + source.path.ToString() +
			                             ": " + pathOnDisk.error);
		}
		document += "    <Compile Include=\"" + XmlEscape(PathToUrsaString(pathOnDisk.value)) + "\" />\n";
	}
	document += "  </ItemGroup>\n  <ItemGroup>\n";
	document += "    <Reference Include=\"Ursa.Runtime\"><HintPath>" + XmlEscape(PathToUrsaString(runtimeAssembly)) +
	            "</HintPath></Reference>\n";
	document += "    <Reference Include=\"Ursa.Rendering\"><HintPath>" +
	            XmlEscape(PathToUrsaString(runtimeAssembly.parent_path() / "Ursa.Rendering.dll")) +
	            "</HintPath></Reference>\n";
	if (editorProject)
	{
		document += "    <Reference Include=\"Ursa.Editor\"><HintPath>" + XmlEscape(PathToUrsaString(editorAssembly)) +
		            "</HintPath></Reference>\n";
		auto gameAssembly = filesystem.ResolveForExternalToolInterop(projects.gameAssembly);
		if (!gameAssembly)
		{
			// The game output may not exist yet. Resolve the project output directory marker instead.
			auto marker =
			    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Assemblies/Game/.output-directory");
			if (!marker)
			{
				return Result<void>::Failure(marker.error);
			}
			auto markerWritten = filesystem.WriteAtomic(marker.value, {});
			if (!markerWritten)
			{
				return markerWritten;
			}
			auto markerPath = filesystem.ResolveForExternalToolInterop(marker.value);
			if (!markerPath)
			{
				return Result<void>::Failure(markerPath.error);
			}
			gameAssembly = Result<std::filesystem::path>::Success(markerPath.value.parent_path() / "Ursa.Game.dll");
		}
		document += "    <Reference Include=\"Ursa.Game\"><HintPath>" +
		            XmlEscape(PathToUrsaString(gameAssembly.value)) + "</HintPath></Reference>\n";
	}
	document += "  </ItemGroup>\n</Project>\n";
	Vector<UInt8> bytes;
	bytes.reserve(document.size());
	for (unsigned char character : document)
	{
		bytes.push_back(character);
	}
	return filesystem.WriteAtomic(path, bytes);
}

Result<std::filesystem::path> ScriptBuildService::EnsureToolDirectory(const VirtualPath& directory)
{
	auto marker = VirtualPath::Parse(directory.ToString() + "/.ursa-directory");
	if (!marker)
	{
		return Result<std::filesystem::path>::Failure(marker.error);
	}
	auto written = filesystem.WriteAtomic(marker.value, {});
	if (!written)
	{
		return Result<std::filesystem::path>::Failure(written.error);
	}
	auto path = filesystem.ResolveForExternalToolInterop(marker.value);
	return path ? Result<std::filesystem::path>::Success(path.value.parent_path())
	            : Result<std::filesystem::path>::Failure(path.error);
}

Result<ScriptBuildReport> ScriptBuildService::Build(ScriptAssemblyGroup group, const String& configuration)
{
	if (configuration.empty())
	{
		return Result<ScriptBuildReport>::Failure("A script build configuration is required");
	}
	auto generated = GenerateProjects();
	if (!generated)
	{
		return Result<ScriptBuildReport>::Failure(generated.error);
	}
	if (group == ScriptAssemblyGroup::Editor)
	{
		auto game = BuildProject(generatedProjects.gameProject, generatedProjects.gameAssembly,
		                         ScriptAssemblyGroup::Game, configuration);
		if (!game || !game.value.succeeded)
		{
			return game;
		}
		return BuildProject(generatedProjects.editorProject, generatedProjects.editorAssembly, group, configuration);
	}
	return BuildProject(generatedProjects.gameProject, generatedProjects.gameAssembly, group, configuration);
}

Result<String> ScriptBuildService::SourceFingerprint()
{
	auto refreshed = database.Refresh();
	if (!refreshed)
	{
		return Result<String>::Failure(refreshed.error);
	}
	UInt64 hash = 14695981039346656037ull;
	for (const ScriptSourceAsset& script : database.ScriptSources())
	{
		HashString(hash, script.path.ToString());
		HashString(hash, script.guid.ToString());
		HashString(hash, script.group == ScriptAssemblyGroup::Game ? "Game" : "Editor");
		auto bytes = filesystem.Read(script.path);
		if (!bytes)
		{
			return Result<String>::Failure(bytes.error);
		}
		HashBytes(hash, bytes.value.data(), bytes.value.size());
	}
	std::ostringstream result;
	result << std::hex << hash;
	return Result<String>::Success(ToUrsaString(result.str()));
}

Result<bool> ScriptBuildService::IsBuildCurrent(ScriptAssemblyGroup group, const String& configuration)
{
	auto refreshed = database.Refresh();
	if (!refreshed)
	{
		return Result<bool>::Failure(refreshed.error);
	}
	if (database.ScriptSources().empty())
	{
		return Result<bool>::Success(true);
	}
	generatedProjects.gameAssembly =
	    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Assemblies/Game/Ursa.Game.dll").value;
	generatedProjects.editorAssembly =
	    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Assemblies/Editor/Ursa.EditorScripts.dll").value;
	auto fingerprint = BuildFingerprint(group, configuration);
	if (!fingerprint)
	{
		return Result<bool>::Failure(fingerprint.error);
	}
	const String groupName = group == ScriptAssemblyGroup::Game ? "Game" : "Editor";
	const VirtualPath statePath = VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/State/" + groupName +
	                                                 "-" + configuration + ".json")
	                                  .value;
	auto state = filesystem.ReadText(statePath);
	const VirtualPath assembly =
	    group == ScriptAssemblyGroup::Game ? generatedProjects.gameAssembly : generatedProjects.editorAssembly;
	auto assemblyExists = filesystem.Exists(assembly);
	if (!state || !assemblyExists || !assemblyExists.value)
	{
		return Result<bool>::Success(false);
	}
	try
	{
		const Json document = Json::parse(ToStdString(state.value));
		auto manifestPath =
		    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Manifests/" + groupName + ".json");
		auto manifest =
		    manifestPath ? filesystem.ReadText(manifestPath.value) : Result<String>::Failure(manifestPath.error);
		if (!manifest)
		{
			return Result<bool>::Success(false);
		}
		const Json content = Json::parse(ToStdString(manifest.value));
		return Result<bool>::Success(document.value("fingerprint", std::string{}) == ToStdString(fingerprint.value) &&
		                             content.value("BuildFingerprint", std::string{}) ==
		                                 ToStdString(fingerprint.value));
	}
	catch (const std::exception&)
	{
		return Result<bool>::Success(false);
	}
}

Result<ScriptBuildReport> ScriptBuildService::BuildProject(const VirtualPath& project, const VirtualPath& assembly,
                                                           ScriptAssemblyGroup group, const String& configuration)
{
#if defined(_WIN32)
	auto fingerprint = BuildFingerprint(group, configuration);
	if (!fingerprint)
	{
		return Result<ScriptBuildReport>::Failure(fingerprint.error);
	}
	const String groupName = group == ScriptAssemblyGroup::Game ? "Game" : "Editor";
	const String stateName = groupName + "-" + configuration + ".json";
	const VirtualPath statePath =
	    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/State/" + stateName).value;
	auto cachedState = filesystem.ReadText(statePath);
	auto assemblyExists = filesystem.Exists(assembly);
	if (cachedState && assemblyExists && assemblyExists.value)
	{
		try
		{
			const Json state = Json::parse(ToStdString(cachedState.value));
			auto manifestPath =
			    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Manifests/" + groupName + ".json").value;
			auto manifest = filesystem.ReadText(manifestPath);
			if (state.value("fingerprint", std::string{}) == ToStdString(fingerprint.value) && manifest &&
			    Json::parse(ToStdString(manifest.value)).value("BuildFingerprint", std::string{}) ==
			        ToStdString(fingerprint.value))
			{
				return Result<ScriptBuildReport>::Success({true, group, assembly, "Script assemblies are up to date."});
			}
		}
		catch (const std::exception&)
		{
			// Invalid cache state is rebuilt below.
		}
	}
	auto projectPath = filesystem.ResolveForExternalToolInterop(project);
	if (!projectPath)
	{
		return Result<ScriptBuildReport>::Failure(projectPath.error);
	}
	const String outputDirectory =
	    String(editorLibraryScheme) + ":ScriptBuild/Staging/" + groupName + "/" + fingerprint.value;
	auto outputPath = VirtualPath::Parse(outputDirectory);
	if (!outputPath)
	{
		return Result<ScriptBuildReport>::Failure(outputPath.error);
	}
	auto output = EnsureToolDirectory(outputPath.value);
	if (!output)
	{
		return Result<ScriptBuildReport>::Failure(output.error);
	}
	auto process = RunDotNet(dotnetExecutable, projectPath.value, output.value, configuration);
	if (!process)
	{
		return Result<ScriptBuildReport>::Failure(process.error);
	}
	if (process.value.exitCode == 0)
	{
		auto latestFingerprint = BuildFingerprint(group, configuration);
		if (!latestFingerprint || latestFingerprint.value != fingerprint.value)
		{
			return Result<ScriptBuildReport>::Success(
			    {false, group, assembly, "Script inputs changed during compilation; staged output was discarded."});
		}
		const String fileName = group == ScriptAssemblyGroup::Game ? "Ursa.Game.dll" : "Ursa.EditorScripts.dll";
		const String stagedPrefix = outputDirectory + "/";
		auto stagedPath = VirtualPath::Parse(stagedPrefix + fileName);
		if (!stagedPath)
		{
			return Result<ScriptBuildReport>::Failure(stagedPath.error);
		}
		auto stagedBytes = filesystem.Read(stagedPath.value);
		if (!stagedBytes)
		{
			return Result<ScriptBuildReport>::Failure("Compiler succeeded but produced no staged assembly: " +
			                                          stagedPath.value.ToString());
		}
		auto published = filesystem.WriteAtomic(assembly, stagedBytes.value);
		if (!published)
		{
			return Result<ScriptBuildReport>::Failure("Cannot publish compiled script assembly: " + published.error);
		}
		const String pdbName = group == ScriptAssemblyGroup::Game ? "Ursa.Game.pdb" : "Ursa.EditorScripts.pdb";
		auto stagedPdb = VirtualPath::Parse(stagedPrefix + pdbName).value;
		auto contentPublished = PublishContent(group, fingerprint.value, stagedPath.value, stagedPdb);
		if (!contentPublished)
		{
			return Result<ScriptBuildReport>::Failure(contentPublished.error);
		}
		auto pdbBytes = filesystem.Read(stagedPdb);
		if (pdbBytes)
		{
			auto pdbPath =
			    VirtualPath::Parse(assembly.ToString().substr(0, assembly.ToString().find_last_of('/')) + "/" + pdbName)
			        .value;
			auto savedPdb = filesystem.WriteAtomic(pdbPath, pdbBytes.value);
			if (!savedPdb)
			{
				return Result<ScriptBuildReport>::Failure(savedPdb.error);
			}
		}
		const std::string stateText =
		    Json{{"fingerprint", ToStdString(fingerprint.value)}, {"assembly", ToStdString(assembly.ToString())}}.dump(
		        2);
		Vector<UInt8> stateBytes;
		stateBytes.reserve(stateText.size());
		for (unsigned char character : stateText)
		{
			stateBytes.push_back(character);
		}
		auto savedState = filesystem.WriteAtomic(statePath, stateBytes);
		if (!savedState)
		{
			return Result<ScriptBuildReport>::Failure("Cannot save script build fingerprint: " + savedState.error);
		}
	}
	String diagnostics = process.value.output;
	for (const ScriptSourceAsset& source : database.ScriptSources())
	{
		auto nativeSource = filesystem.ResolveForExternalToolInterop(source.path);
		if (nativeSource)
		{
			std::string text = ToStdString(diagnostics);
			ReplaceAll(text, nativeSource.value.u8string(), ToStdString(source.path.ToString()));
			diagnostics = ToUrsaString(text);
		}
	}
	return Result<ScriptBuildReport>::Success({process.value.exitCode == 0, group, assembly, std::move(diagnostics)});
#else
	(void)project;
	(void)assembly;
	(void)group;
	(void)configuration;
	return Result<ScriptBuildReport>::Failure("The script compiler driver currently supports Windows only");
#endif
}

Result<void> ScriptBuildService::PublishContent(ScriptAssemblyGroup group, const String& fingerprint,
                                                const VirtualPath& assembly, const VirtualPath& pdb)
{
	const String groupName = group == ScriptAssemblyGroup::Game ? "Game" : "Editor";
	const String contentRoot =
	    String(editorLibraryScheme) + ":ScriptBuild/Content/" + groupName + "/" + fingerprint + "/";
	const VirtualPath manifestPath =
	    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Manifests/" + groupName + ".json").value;
	Json manifest{{"Version", 1u}, {"Group", ToStdString(groupName)}, {"BuildFingerprint", ToStdString(fingerprint)}};
	manifest["Assemblies"] = Json::array();
	if (group == ScriptAssemblyGroup::Editor)
	{
		auto gameText = filesystem.ReadText(
		    VirtualPath::Parse(String(editorLibraryScheme) + ":ScriptBuild/Manifests/Game.json").value);
		if (!gameText)
		{
			return Result<void>::Failure("Editor content requires a successful Game content manifest: " +
			                             gameText.error);
		}
		try
		{
			const Json game = Json::parse(ToStdString(gameText.value));
			manifest["Assemblies"] = game.at("Assemblies");
		}
		catch (const std::exception& error)
		{
			return Result<void>::Failure("Invalid Game content manifest: " + ToUrsaString(error.what()));
		}
	}
	auto bytes = filesystem.Read(assembly);
	if (!bytes)
	{
		return Result<void>::Failure(bytes.error);
	}
	const String fileName = group == ScriptAssemblyGroup::Game ? "Ursa.Game.dll" : "Ursa.EditorScripts.dll";
	const VirtualPath imagePath = VirtualPath::Parse(contentRoot + fileName).value;
	auto saved = filesystem.WriteAtomic(imagePath, bytes.value);
	if (!saved)
	{
		return saved;
	}
	Json record{{"Name", group == ScriptAssemblyGroup::Game ? "Ursa.Game" : "Ursa.EditorScripts"},
	            {"Path", ToStdString(imagePath.ToString())},
	            {"Fingerprint", ToStdString(ContentFingerprint(bytes.value))}};
	auto symbols = filesystem.Read(pdb);
	if (symbols)
	{
		const VirtualPath symbolsPath = VirtualPath::Parse(contentRoot + pdb.FileName()).value;
		saved = filesystem.WriteAtomic(symbolsPath, symbols.value);
		if (!saved)
		{
			return saved;
		}
		record["PdbPath"] = ToStdString(symbolsPath.ToString());
		record["PdbFingerprint"] = ToStdString(ContentFingerprint(symbols.value));
	}
	manifest["Assemblies"].push_back(record);
	auto catalogText = filesystem.ReadText(generatedProjects.typeCatalog);
	if (!catalogText)
	{
		return Result<void>::Failure(catalogText.error);
	}
	Json catalog;
	try
	{
		catalog = Json::parse(ToStdString(catalogText.value));
		if (group == ScriptAssemblyGroup::Game)
		{
			Json scripts = Json::array();
			for (const Json& script : catalog.at("Scripts"))
			{
				if (script.value("Group", std::string{}) == "Game")
				{
					scripts.push_back(script);
				}
			}
			catalog["Scripts"] = std::move(scripts);
		}
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure("Cannot publish script catalog: " + ToUrsaString(error.what()));
	}
	const Vector<UInt8> catalogBytes = JsonBytes(catalog);
	const VirtualPath catalogPath = VirtualPath::Parse(contentRoot + "ScriptTypeCatalog.json").value;
	saved = filesystem.WriteAtomic(catalogPath, catalogBytes);
	if (!saved)
	{
		return saved;
	}
	manifest["TypeCatalog"] = ToStdString(catalogPath.ToString());
	manifest["CatalogFingerprint"] = ToStdString(ContentFingerprint(catalogBytes));
	// All referenced immutable blobs exist before publishing the manifest pointer.
	return filesystem.WriteAtomic(manifestPath, JsonBytes(manifest));
}

Result<String> ScriptBuildService::BuildFingerprint(ScriptAssemblyGroup group, const String& configuration) const
{
	UInt64 hash = 1469598103934665603ull;
	HashString(hash, group == ScriptAssemblyGroup::Game ? "Game" : "Editor");
	HashString(hash, configuration);
	HashString(hash, PathToUrsaString(dotnetExecutable));
	HashString(hash, PathToUrsaString(referenceAssemblyDirectory));
	auto sdkVersion = RunProcess(dotnetExecutable, {L"--version"}, referenceAssemblyDirectory);
	if (!sdkVersion || sdkVersion.value.exitCode != 0)
	{
		return Result<String>::Failure("Cannot determine the active .NET SDK version for script build fingerprinting");
	}
	HashString(hash, sdkVersion.value.output);
	for (const ScriptSourceAsset& source : database.ScriptSources())
	{
		if (source.group != group)
		{
			continue;
		}
		HashString(hash, source.path.ToString());
		HashString(hash, source.guid.ToString());
		auto bytes = filesystem.Read(source.path);
		if (!bytes)
		{
			return Result<String>::Failure("Cannot fingerprint script source " + source.path.ToString() + ": " +
			                               bytes.error);
		}
		HashBytes(hash, bytes.value.data(), bytes.value.size());
	}
	Vector<std::filesystem::path> dependencies{runtimeAssembly, runtimeAssembly.parent_path() / "Ursa.Rendering.dll",
	                                           catalogGeneratorAssembly};
	if (group == ScriptAssemblyGroup::Editor)
	{
		dependencies.push_back(editorAssembly);
		if (!generatedProjects.gameAssembly.scheme.empty())
		{
			auto gameBytes = filesystem.Read(generatedProjects.gameAssembly);
			if (gameBytes)
			{
				HashBytes(hash, gameBytes.value.data(), gameBytes.value.size());
			}
		}
	}
	for (const std::filesystem::path& dependency : dependencies)
	{
		auto bytes = ReadConfiguredFile(dependency);
		if (!bytes)
		{
			return Result<String>::Failure(bytes.error);
		}
		HashString(hash, PathToUrsaString(dependency));
		HashBytes(hash, bytes.value.data(), bytes.value.size());
	}
	std::error_code directoryError;
	for (const auto& entry : std::filesystem::directory_iterator(referenceAssemblyDirectory, directoryError))
	{
		if (directoryError)
		{
			return Result<String>::Failure("Cannot enumerate .NET reference assemblies for script fingerprinting");
		}
		if (!entry.is_regular_file() || entry.path().extension() != ".dll")
		{
			continue;
		}
		auto bytes = ReadConfiguredFile(entry.path());
		if (!bytes)
		{
			return Result<String>::Failure(bytes.error);
		}
		HashString(hash, PathToUrsaString(entry.path().filename()));
		HashBytes(hash, bytes.value.data(), bytes.value.size());
	}
	if (directoryError)
	{
		return Result<String>::Failure("Cannot enumerate .NET reference assemblies for script fingerprinting");
	}
	std::ostringstream encoded;
	encoded << std::hex << std::setfill('0') << std::setw(16) << hash;
	return Result<String>::Success(ToUrsaString(encoded.str()));
}
} // namespace Ursa::Editor
