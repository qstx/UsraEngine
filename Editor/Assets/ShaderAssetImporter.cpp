#include <Assets/ShaderAssetImporter.hpp>
#include <Shader/ShaderImporter.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/RuntimeContent.hpp>
#include <Material/Material.hpp>
#include <Serialization/MaterialResource.hpp>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Ursa::Editor
{
namespace
{
Result<void> ConfigureStaging(Shader::ShaderCompilerTools& tools, VirtualFileSystem& files)
{
	auto roots = files.Roots(editorLibraryScheme);
	if (!roots || roots.value.empty())
	{
		return {};
	}
	auto marker = VirtualPath::Parse("lib:Temp/ShaderCompiler/Root.json");
	if (!marker)
	{
		return Result<void>::Failure(marker.error);
	}
	auto written = files.WriteAtomic(marker.value, {123, 125});
	if (!written)
	{
		return written;
	}
	auto path = files.ResolveForExternalToolInterop(marker.value);
	if (!path)
	{
		return Result<void>::Failure(path.error);
	}
	tools.stagingRoot = path.value.parent_path();
	return {};
}

std::filesystem::path ToolPath(const char* configured, const char* name)
{
	std::error_code error;
	auto path = std::filesystem::u8path(configured);
	if (!path.empty() && std::filesystem::is_regular_file(path, error))
	{
		return path;
	}
#ifdef _WIN32
	wchar_t sdk[32768]{};
	const auto length = GetEnvironmentVariableW(L"VULKAN_SDK", sdk, 32768);
	if (length > 0 && length < 32768)
	{
		return std::filesystem::path(sdk) / "Bin" / name;
	}
#endif
	return std::filesystem::path(name);
}

nlohmann::json ToolIdentity(const Shader::ShaderCompilerTools& tools)
{
	// SDK executable inspection is part of the external tool boundary. No tool
	// is launched on a cache hit, and unavailable tools do not invalidate usable bytecode.
	nlohmann::json result = nlohmann::json::array();
	for (const auto& path : {tools.dxc, tools.spirvValidator, tools.spirvCross})
	{
		std::error_code error;
		if (path.empty() || !std::filesystem::is_regular_file(path, error))
		{
			return nlohmann::json::array();
		}
		const auto size = std::filesystem::file_size(path, error);
		if (error)
		{
			return nlohmann::json::array();
		}
		const auto time = std::filesystem::last_write_time(path, error);
		if (error)
		{
			return nlohmann::json::array();
		}
		result.push_back({{"path", path.u8string()}, {"size", size}, {"timestamp", time.time_since_epoch().count()}});
	}
	return result;
}

Result<ShaderAssetImportResult> Compile(VirtualFileSystem& files, const VirtualPath& source,
                                        const Shader::DxcShaderCompiler& compiler, const String& version,
                                        const VirtualPath& cacheRoot)
{
	Vector<RuntimeContentInput> capturedInputs;
	auto originalFiles = files.List(source.scheme);
	if (!originalFiles)
	{
		return Result<ShaderAssetImportResult>::Failure(originalFiles.error);
	}
	for (const auto& entry : originalFiles.value)
	{
		auto captured = CaptureRuntimeContentInput(files, entry.path);
		if (!captured)
		{
			return Result<ShaderAssetImportResult>::Failure(captured.error);
		}
		capturedInputs.push_back(std::move(captured.value));
	}
	auto library = Shader::ShaderSourceLibrary::Load(files, source.scheme, source);
	if (!library)
	{
		return Result<ShaderAssetImportResult>::Failure(library.error);
	}
	const auto sources = library.value.Sources();
	const auto selected =
	    std::find_if(sources.begin(), sources.end(), [&](const auto* item) { return item->path == source; });
	if (selected == sources.end())
	{
		return Result<ShaderAssetImportResult>::Failure("Shader source not found: " + source.ToString());
	}
	auto report = library.value.ResolveDependencies(files, (*selected)->source.name);
	if (!report)
	{
		return Result<ShaderAssetImportResult>::Failure(report.error);
	}
	ShaderAssetImportResult result;
	result.dependencies = report.value.includes;
	for (const auto& reference : report.value.shaderReferences)
	{
		const auto* dependency = library.value.Find(reference);
		if (!dependency)
		{
			return Result<ShaderAssetImportResult>::Failure("Missing Shader dependency: " + reference);
		}
		result.dependencies.push_back(dependency->path);
	}
	auto listed = files.List(source.scheme);
	if (!listed)
	{
		return Result<ShaderAssetImportResult>::Failure(listed.error);
	}
	for (const auto& entry : listed.value)
	{
		if (PathFromUrsaString(entry.path.relative).extension() == ".ursamat")
		{
			auto material = LoadMaterialResource(files, entry.path,
			                                     [](ResourceId)
			                                     {
				                                     return Result<MaterialTexturePtr>::Success(
				                                         DefaultMaterialTexture(MaterialTextureSlot::BaseColor));
			                                     });
			if (!material)
			{
				return Result<ShaderAssetImportResult>::Failure(material.error);
			}
			if (material.value.material->ShaderId() == (*selected)->source.id)
			{
				result.dependencies.push_back(entry.path);
			}
		}
	}
	const String outputScheme = "shaderstage";
	auto mounted = files.MountProvider(outputScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
	if (!mounted)
	{
		return Result<ShaderAssetImportResult>::Failure(mounted.error);
	}
	auto imported = Shader::ShaderImporter::ImportAll(files, compiler, version, source.scheme, outputScheme, cacheRoot,
	                                                  256, {}, source);
	if (!imported || imported.value.size() != 1)
	{
		return Result<ShaderAssetImportResult>::Failure(imported ? "Shader importer did not produce selected asset"
		                                                         : imported.error);
	}
	result.asset = std::move(imported.value.front());
	auto relative = PathFromUrsaString(source.relative);
	relative.replace_extension(".ursashaderasset");
	auto output = VirtualPath::Parse(outputScheme + ":" + PathToUrsaString(relative));
	if (!output)
	{
		return Result<ShaderAssetImportResult>::Failure(output.error);
	}
	result.artifactPath = output.value;
	for (const auto& input : capturedInputs)
	{
		auto current = CaptureRuntimeContentInput(files, input.path);
		if (!current || current.value.fingerprint != input.fingerprint)
		{
			return Result<ShaderAssetImportResult>::Failure("Shader inputs changed during compilation: " +
			                                                input.path.ToString());
		}
	}
	return Result<ShaderAssetImportResult>::Success(std::move(result));
}
} // namespace

Shader::ShaderCompilerTools DefaultShaderCompilerTools()
{
	return {ToolPath(URSA_DXC_EXECUTABLE, "dxc.exe"), ToolPath(URSA_SPIRV_VAL_EXECUTABLE, "spirv-val.exe"),
	        ToolPath(URSA_SPIRV_CROSS_EXECUTABLE, "spirv-cross.exe")};
}

Result<ShaderAssetImportResult> ShaderAssetImporter::Import(AssetDatabase& database, const VirtualPath& source,
                                                            Shader::ShaderCompilerTools tools, bool force)
{
	auto library = Shader::ShaderSourceLibrary::Load(database.Filesystem(), source.scheme, source);
	if (!library)
	{
		return Result<ShaderAssetImportResult>::Failure(library.error);
	}
	const auto sourceAssets = library.value.Sources();
	const auto selected = std::find_if(sourceAssets.begin(), sourceAssets.end(),
	                                   [&](const auto* entry) { return entry->path == source; });
	if (selected == sourceAssets.end())
	{
		return Result<ShaderAssetImportResult>::Failure("Shader source not found: " + source.ToString());
	}
	auto report = library.value.ResolveDependencies(database.Filesystem(), (*selected)->source.name);
	if (!report)
	{
		return Result<ShaderAssetImportResult>::Failure(report.error);
	}
	Vector<VirtualPath> dependencies = report.value.includes;
	for (const auto& reference : report.value.shaderReferences)
	{
		const auto* dependency = library.value.Find(reference);
		if (dependency)
		{
			dependencies.push_back(dependency->path);
		}
	}
	auto files = database.Filesystem().List(source.scheme);
	if (!files)
	{
		return Result<ShaderAssetImportResult>::Failure(files.error);
	}
	for (const auto& entry : files.value)
	{
		if (PathFromUrsaString(entry.path.relative).extension() == ".ursamat")
		{
			auto material = LoadMaterialResource(database.Filesystem(), entry.path,
			                                     [](ResourceId)
			                                     {
				                                     return Result<MaterialTexturePtr>::Success(
				                                         DefaultMaterialTexture(MaterialTextureSlot::BaseColor));
			                                     });
			if (!material)
			{
				return Result<ShaderAssetImportResult>::Failure(material.error);
			}
			if (material.value.material->ShaderId() == (*selected)->source.id)
			{
				dependencies.push_back(entry.path);
			}
		}
	}
	// A validated importer record is sufficient to reuse bytecode without launching SDK tools.
	auto previous = database.CurrentImport(source);
	const auto toolIdentity = ToolIdentity(tools);
	if (!force && previous && previous.value.upToDate && previous.value.dependencies == dependencies &&
	    !previous.value.artifacts.empty() &&
	    (toolIdentity.empty() || previous.value.settings.value("tools", nlohmann::json::array()) == toolIdentity))
	{
		auto asset = Shader::LoadShaderAssetResource(database.Filesystem(), previous.value.artifacts.front().path);
		if (!asset)
		{
			return Result<ShaderAssetImportResult>::Failure(asset.error);
		}
		return Result<ShaderAssetImportResult>::Success(
		    {std::move(asset.value), previous.value.artifacts.front().path, dependencies, true});
	}
	auto staging = ConfigureStaging(tools, database.Filesystem());
	if (!staging)
	{
		return Result<ShaderAssetImportResult>::Failure(staging.error);
	}
	Shader::DxcShaderCompiler compiler(std::move(tools));
	auto version = compiler.Version();
	if (!version)
	{
		return Result<ShaderAssetImportResult>::Failure(source.ToString() + ": " + version.error);
	}
	const nlohmann::json settings{
	    {"compilerVersion", ToStdString(version.value)}, {"target", "VulkanSpirv"}, {"tools", toolIdentity}};
	if (source.scheme == projectScheme)
	{
		auto metadata = database.Metadata(source);
		if (!metadata)
		{
			return Result<ShaderAssetImportResult>::Failure(metadata.error);
		}
		if (metadata.value.importer != "ursa.Shader" || metadata.value.importerVersion != 1 ||
		    metadata.value.importSettings != settings)
		{
			auto saved = database.SetImporterSettings(source, "ursa.Shader", 1, settings);
			if (!saved)
			{
				return Result<ShaderAssetImportResult>::Failure(saved.error);
			}
		}
	}
	auto cached = database.CheckImport(source, "ursa.Shader", 1, "vulkan-spv", settings, dependencies);
	if (!cached)
	{
		return Result<ShaderAssetImportResult>::Failure(cached.error);
	}
	if (!force && cached.value.upToDate)
	{
		auto asset = Shader::LoadShaderAssetResource(database.Filesystem(), cached.value.artifacts.front().path);
		if (!asset)
		{
			return Result<ShaderAssetImportResult>::Failure(asset.error);
		}
		return Result<ShaderAssetImportResult>::Success(
		    {std::move(asset.value), cached.value.artifacts.front().path, dependencies, true});
	}
	VirtualFileSystem staged = database.Filesystem();
	auto cachePath = VirtualPath::Parse("lib:ShaderCompileCache");
	if (!cachePath)
	{
		return Result<ShaderAssetImportResult>::Failure(cachePath.error);
	}
	auto result = Compile(staged, source, compiler, version.value, cachePath.value);
	if (!result)
	{
		return result;
	}
	auto metadata = database.Metadata(source);
	if (!metadata)
	{
		return Result<ShaderAssetImportResult>::Failure(metadata.error);
	}
	result.value.asset.id = metadata.value.guid;
	auto saved = Shader::SaveShaderAssetResource(staged, result.value.artifactPath, result.value.asset);
	if (!saved)
	{
		return Result<ShaderAssetImportResult>::Failure(saved.error);
	}
	auto bytes = staged.Read(result.value.artifactPath);
	if (!bytes)
	{
		return Result<ShaderAssetImportResult>::Failure(bytes.error);
	}
	auto published = database.PublishImport(source, "ursa.Shader", 1, "vulkan-spv", settings, result.value.dependencies,
	                                        {{0, "Shader", std::move(bytes.value)}});
	if (!published)
	{
		return Result<ShaderAssetImportResult>::Failure(published.error);
	}
	auto artifact = database.Resolve(metadata.value.guid);
	if (!artifact)
	{
		return Result<ShaderAssetImportResult>::Failure(artifact.error);
	}
	result.value.artifactPath = artifact.value;
	return result;
}

Result<ShaderAssetImportResult> ShaderAssetImporter::Preview(const VirtualFileSystem& files, const VirtualPath& source,
                                                             Shader::ShaderCompilerTools tools)
{
	VirtualFileSystem staged = files;
	auto staging = ConfigureStaging(tools, staged);
	if (!staging)
	{
		return Result<ShaderAssetImportResult>::Failure(staging.error);
	}
	Shader::DxcShaderCompiler compiler(std::move(tools));
	auto version = compiler.Version();
	if (!version)
	{
		return Result<ShaderAssetImportResult>::Failure(version.error);
	}
	if (!staged.IsMounted(editorLibraryScheme))
	{
		auto mounted =
		    staged.MountProvider(editorLibraryScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
		if (!mounted)
		{
			return Result<ShaderAssetImportResult>::Failure(mounted.error);
		}
	}
	return Compile(staged, source, compiler, version.value, VirtualPath::Parse("lib:ShaderCompileCache").value);
}
} // namespace Ursa::Editor
