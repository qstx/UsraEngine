#include <Shader/ShaderCompiler.hpp>

#include <Core/StringInterop.hpp>
#include <Vfs/VirtualFileSystem.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Ursa::Shader
{
namespace
{
struct ProcessResult
{
	Int32 exitCode = -1;
	String output;
};

#ifdef _WIN32
std::wstring QuoteArgument(const std::wstring& argument)
{
	std::wstring result = L"\"";
	Ursa::Size slashes = 0;
	for (wchar_t character : argument)
	{
		if (character == L'\\')
		{
			++slashes;
			continue;
		}
		if (character == L'"')
		{
			result.append(slashes * 2 + 1, L'\\');
			result.push_back(L'"');
			slashes = 0;
			continue;
		}
		result.append(slashes, L'\\');
		slashes = 0;
		result.push_back(character);
	}
	result.append(slashes * 2, L'\\');
	result.push_back(L'"');
	return result;
}

Result<ProcessResult> RunProcess(const std::filesystem::path& executable, const Vector<std::wstring>& arguments,
                                 const std::filesystem::path& workingDirectory)
{
	SECURITY_ATTRIBUTES security{};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &security, 0))
	{
		return Result<ProcessResult>::Failure("Cannot create compiler output pipe");
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = writePipe;
	startup.hStdError = writePipe;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	PROCESS_INFORMATION process{};
	std::wstring commandLine = QuoteArgument(executable.wstring());
	for (const std::wstring& argument : arguments)
	{
		commandLine.push_back(L' ');
		commandLine += QuoteArgument(argument);
	}
	std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
	mutableCommandLine.push_back(L'\0');
	const std::wstring executableName = executable.wstring();
	const std::wstring directory = workingDirectory.empty() ? std::wstring{} : workingDirectory.wstring();
	const BOOL created =
	    CreateProcessW(executableName.c_str(), mutableCommandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
	                   nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &process);
	CloseHandle(writePipe);
	if (!created)
	{
		CloseHandle(readPipe);
		return Result<ProcessResult>::Failure("Cannot launch shader tool " + PathToUrsaString(executable));
	}
	std::string output;
	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
	{
		output.append(buffer, bytesRead);
	}
	CloseHandle(readPipe);
	WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exitCode = 0;
	GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return Result<ProcessResult>::Success({static_cast<Int32>(exitCode), String(output.data(), output.size())});
}
#else
Result<ProcessResult> RunProcess(const std::filesystem::path&, const Vector<std::wstring>&,
                                 const std::filesystem::path&)
{
	return Result<ProcessResult>::Failure("The DXC shader importer is currently supported on Windows only");
}
#endif

std::wstring Wide(const String& value)
{
	return std::filesystem::u8path(value.c_str()).wstring();
}

std::wstring Wide(const std::filesystem::path& value)
{
	return value.wstring();
}

String Number(UInt32 value)
{
	String result;
	if (value == 0)
	{
		return "0";
	}
	while (value > 0)
	{
		result.insert(result.begin(), static_cast<char>('0' + (value % 10)));
		value /= 10;
	}
	return result;
}

String StageName(ProgramStage stage)
{
	switch (stage)
	{
		case ProgramStage::Vertex:
			return "vertex";
		case ProgramStage::Fragment:
			return "fragment";
		case ProgramStage::Compute:
			return "compute";
	}
	return "unknown";
}

String StageProfile(ProgramStage stage)
{
	switch (stage)
	{
		case ProgramStage::Vertex:
			return "vs_6_0";
		case ProgramStage::Fragment:
			return "ps_6_0";
		case ProgramStage::Compute:
			return "cs_6_0";
	}
	return {};
}

bool ValidIdentifier(const String& identifier)
{
	if (identifier.empty() ||
	    !(std::isalpha(static_cast<unsigned char>(identifier.front())) != 0 || identifier.front() == '_'))
	{
		return false;
	}
	for (char character : identifier)
	{
		if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_')
		{
			return false;
		}
	}
	return true;
}

String RemoveKeywordPragmas(const String& input)
{
	const std::string source = ToStdString(input);
	std::string result;
	for (Ursa::Size lineStart = 0; lineStart <= source.size();)
	{
		const Ursa::Size newline = source.find('\n', lineStart);
		const Ursa::Size lineEnd = newline == std::string::npos ? source.size() : newline;
		std::string_view line(source.data() + lineStart, lineEnd - lineStart);
		Ursa::Size cursor = 0;
		while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t' || line[cursor] == '\r'))
		{
			++cursor;
		}
		bool removeLine = false;
		if (line.substr(cursor, 7) == "#pragma")
		{
			cursor += 7;
			while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])))
			{
				++cursor;
			}
			const Ursa::Size directiveStart = cursor;
			while (cursor < line.size() &&
			       (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
			{
				++cursor;
			}
			const std::string_view directive = line.substr(directiveStart, cursor - directiveStart);
			removeLine = directive == "shader_feature" || directive == "shader_feature_local" ||
			             directive == "multi_compile" || directive == "multi_compile_local";
		}
		if (!removeLine)
		{
			result.append(line.data(), line.size());
		}
		if (newline == std::string::npos)
		{
			break;
		}
		result.push_back('\n');
		lineStart = newline + 1;
	}
	return ToUrsaString(result);
}

Result<void> WriteText(VirtualFileSystem& filesystem, const VirtualPath& path, const String& text)
{
	Vector<UInt8> bytes;
	bytes.reserve(text.size());
	for (char character : text)
	{
		bytes.push_back(static_cast<UInt8>(static_cast<unsigned char>(character)));
	}
	return filesystem.WriteAtomic(path, bytes, true);
}

Result<std::filesystem::path> HostPath(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto roots = filesystem.Roots(path.scheme);
	if (!roots || roots.value.empty())
	{
		return Result<std::filesystem::path>::Failure(roots ? "Temporary compiler mount has no directory root"
		                                                    : roots.error);
	}
	return Result<std::filesystem::path>::Success(roots.value.front() / PathFromUrsaString(path.relative));
}

Result<String> ReadText(const VirtualFileSystem& filesystem, const VirtualPath& path)
{
	auto text = filesystem.ReadText(path);
	if (!text)
	{
		return Result<String>::Failure("Cannot read compiler reflection " + path.ToString() + ": " + text.error);
	}
	return text;
}

class TemporaryDirectory final
{
public:
	explicit TemporaryDirectory(std::filesystem::path directory) : path(std::move(directory))
	{
	}

	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}

	const std::filesystem::path& Path() const
	{
		return path;
	}

private:
	std::filesystem::path path;
};

String Diagnostic(const ShaderCompileRequest& request, const String& message)
{
	return request.sourcePath.ToString() + ":" + Number(request.entryPoint.location.line) + " (" +
	       StageName(request.entryPoint.stage) + " " + request.entryPoint.name + "): " + message;
}
} // namespace

DxcShaderCompiler::DxcShaderCompiler(ShaderCompilerTools selectedTools) : tools(std::move(selectedTools))
{
}

Result<String> DxcShaderCompiler::Version() const
{
	for (const auto& tool : {tools.dxc, tools.spirvValidator, tools.spirvCross})
	{
		if (tool.empty() || !std::filesystem::is_regular_file(tool))
		{
			return Result<String>::Failure("A required shader compiler or validation tool is missing");
		}
	}
	const auto dxcVersion = RunProcess(tools.dxc, {L"--version"}, {});
	const auto validatorVersion = RunProcess(tools.spirvValidator, {L"--version"}, {});
	const auto crossVersion = RunProcess(tools.spirvCross, {L"--revision"}, {});
	if (!dxcVersion || dxcVersion.value.exitCode != 0 || !validatorVersion || validatorVersion.value.exitCode != 0 ||
	    !crossVersion || crossVersion.value.exitCode != 0)
	{
		return Result<String>::Failure(!dxcVersion         ? dxcVersion.error
		                               : !validatorVersion ? validatorVersion.error
		                               : !crossVersion     ? crossVersion.error
		                                                   : "Cannot query shader tool versions");
	}
	return Result<String>::Success("UrsaBindingMapV2\nDXC " + dxcVersion.value.output + "SPIRV-Tools " +
	                               validatorVersion.value.output + "SPIRV-Cross " + crossVersion.value.output);
}

Result<CompiledShaderStage> DxcShaderCompiler::Compile(const ShaderCompileRequest& request) const
{
	if (request.sourcePath.relative.empty() || !ValidIdentifier(request.entryPoint.name) ||
	    StageProfile(request.entryPoint.stage).empty() || request.source.empty())
	{
		return Result<CompiledShaderStage>::Failure(Diagnostic(request, "Invalid shader compilation request"));
	}
	for (const auto& tool : {tools.dxc, tools.spirvValidator, tools.spirvCross})
	{
		if (tool.empty() || !std::filesystem::is_regular_file(tool))
		{
			return Result<CompiledShaderStage>::Failure(Diagnostic(request, "A required shader tool is missing"));
		}
	}
	auto compilerVersion = Version();
	if (!compilerVersion)
	{
		return Result<CompiledShaderStage>::Failure(Diagnostic(request, compilerVersion.error));
	}
	static std::atomic<Ursa::UInt64> uniqueIndex{0};
	const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
	const String folderName =
	    "UrsaShaderCompiler-" + ToUrsaString(std::to_string(timestamp) + "-" + std::to_string(uniqueIndex++));
	std::error_code error;
	const std::filesystem::path temporaryPath =
	    std::filesystem::temp_directory_path(error) / std::filesystem::u8path(folderName.c_str());
	if (error || !std::filesystem::create_directories(temporaryPath, error) || error)
	{
		return Result<CompiledShaderStage>::Failure(Diagnostic(request, "Cannot create a temporary shader directory"));
	}
	TemporaryDirectory temporaryDirectory(temporaryPath);
	VirtualFileSystem temporaryFiles;
	auto mountedTemporary = temporaryFiles.Mount("temp", temporaryPath, MountAccess::ReadWrite);
	if (!mountedTemporary)
	{
		return Result<CompiledShaderStage>::Failure(Diagnostic(request, mountedTemporary.error));
	}
	std::filesystem::path sourceRelative = PathFromUrsaString(request.sourcePath.relative);
	sourceRelative.replace_extension(".hlsl");
	auto sourceVirtualPath =
	    VirtualPath::Parse("temp:" + request.sourcePath.scheme + "/" + PathToUrsaString(sourceRelative));
	auto includeRootVirtualPath = VirtualPath::Parse("temp:" + request.sourcePath.scheme);
	auto bytecodeVirtualPath = VirtualPath::Parse("temp:compiled.spv");
	auto reflectionVirtualPath = VirtualPath::Parse("temp:reflection.json");
	if (!sourceVirtualPath || !includeRootVirtualPath || !bytecodeVirtualPath || !reflectionVirtualPath)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, "Cannot form temporary VFS paths for shader compiler inputs"));
	}
	String source;
	for (const ProgramBlock& shared : request.sharedBlocks)
	{
		if (shared.kind == ProgramBlockKind::Include)
		{
			source += RemoveKeywordPragmas(shared.source);
			source += "\n";
		}
	}
	for (const String& keyword : request.keywords)
	{
		if (!ValidIdentifier(keyword))
		{
			return Result<CompiledShaderStage>::Failure(Diagnostic(request, "Invalid shader keyword " + keyword));
		}
		source += "#define " + keyword + " 1\n";
	}
	source += "#line " + Number(request.entryPoint.location.line) + " \"" + request.sourcePath.ToString() + "\"\n";
	source += RemoveKeywordPragmas(request.source);
	auto sourceWritten = WriteText(temporaryFiles, sourceVirtualPath.value, source);
	if (!sourceWritten)
	{
		return Result<CompiledShaderStage>::Failure(Diagnostic(request, sourceWritten.error));
	}
	for (const ShaderIncludeSource& include : request.includes)
	{
		if (include.path.scheme != request.sourcePath.scheme)
		{
			return Result<CompiledShaderStage>::Failure(
			    Diagnostic(request, "HLSL includes must use the same VFS scheme as their Shader"));
		}
		auto includeVirtualPath = VirtualPath::Parse("temp:" + include.path.scheme + "/" + include.path.relative);
		if (!includeVirtualPath)
		{
			return Result<CompiledShaderStage>::Failure(Diagnostic(request, includeVirtualPath.error));
		}
		auto written = WriteText(temporaryFiles, includeVirtualPath.value, include.source);
		if (!written)
		{
			return Result<CompiledShaderStage>::Failure(Diagnostic(request, written.error));
		}
	}
	auto sourceHostPath = HostPath(temporaryFiles, sourceVirtualPath.value);
	auto includeRootHostPath = HostPath(temporaryFiles, includeRootVirtualPath.value);
	auto bytecodeHostPath = HostPath(temporaryFiles, bytecodeVirtualPath.value);
	auto reflectionHostPath = HostPath(temporaryFiles, reflectionVirtualPath.value);
	if (!sourceHostPath || !includeRootHostPath || !bytecodeHostPath || !reflectionHostPath)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, !sourceHostPath        ? sourceHostPath.error
		                        : !includeRootHostPath ? includeRootHostPath.error
		                        : !bytecodeHostPath    ? bytecodeHostPath.error
		                                               : reflectionHostPath.error));
	}
	const Vector<std::wstring> compileArguments = {L"-spirv",
	                                               L"-fspv-target-env=vulkan1.2",
	                                               L"-fvk-t-shift",
	                                               L"16",
	                                               L"0",
	                                               L"-fvk-s-shift",
	                                               L"32",
	                                               L"0",
	                                               L"-fvk-u-shift",
	                                               L"48",
	                                               L"0",
	                                               L"-T",
	                                               Wide(StageProfile(request.entryPoint.stage)),
	                                               L"-E",
	                                               Wide(request.entryPoint.name),
	                                               L"-I",
	                                               Wide(includeRootHostPath.value),
	                                               L"-Fo",
	                                               Wide(bytecodeHostPath.value),
	                                               Wide(sourceHostPath.value)};
	auto compiled = RunProcess(tools.dxc, compileArguments, temporaryPath);
	if (!compiled || compiled.value.exitCode != 0)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, compiled ? compiled.value.output : compiled.error));
	}
	const Vector<std::wstring> validationArguments = {L"--target-env", L"vulkan1.2", Wide(bytecodeHostPath.value)};
	auto validated = RunProcess(tools.spirvValidator, validationArguments, temporaryPath);
	if (!validated || validated.value.exitCode != 0)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, "SPIR-V validation failed: " + (validated ? validated.value.output : validated.error)));
	}
	const Vector<std::wstring> reflectionArguments = {Wide(bytecodeHostPath.value), L"--reflect", L"--output",
	                                                  Wide(reflectionHostPath.value)};
	auto reflected = RunProcess(tools.spirvCross, reflectionArguments, temporaryPath);
	if (!reflected || reflected.value.exitCode != 0)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, "SPIR-V reflection failed: " + (reflected ? reflected.value.output : reflected.error)));
	}
	auto bytecode = temporaryFiles.Read(bytecodeVirtualPath.value);
	if (!bytecode || bytecode.value.empty() || bytecode.value.size() % sizeof(UInt32) != 0 ||
	    bytecode.value[0] != 0x03 || bytecode.value[1] != 0x02 || bytecode.value[2] != 0x23 ||
	    bytecode.value[3] != 0x07)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, bytecode ? "Compiler output is not valid SPIR-V bytecode" : bytecode.error));
	}
	auto reflection = ReadText(temporaryFiles, reflectionVirtualPath.value);
	if (!reflection)
	{
		return Result<CompiledShaderStage>::Failure(Diagnostic(request, reflection.error));
	}
	try
	{
		nlohmann::json::parse(reflection.value.begin(), reflection.value.end());
	}
	catch (const nlohmann::json::exception& exception)
	{
		return Result<CompiledShaderStage>::Failure(
		    Diagnostic(request, "Invalid SPIR-V reflection JSON: " + ToUrsaString(exception.what())));
	}
	return Result<CompiledShaderStage>::Success({request.entryPoint.stage, request.entryPoint.name,
	                                             std::move(bytecode.value), std::move(reflection.value),
	                                             std::move(compilerVersion.value), std::move(compiled.value.output)});
}
} // namespace Ursa::Shader
