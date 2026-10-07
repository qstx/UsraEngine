#include <Assets/ShaderImportView.hpp>

#include <Core/StringInterop.hpp>
#include <Shader/ShaderCompiler.hpp>
#include <Shader/ShaderImporter.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <algorithm>
#include <filesystem>

namespace Ursa::Editor
{
Result<std::unique_ptr<ShaderImportView>> ShaderImportView::Open(const VirtualFileSystem& filesystem,
                                                                 VirtualPath sourcePath)
{
	if (std::filesystem::u8path(ToStdString(sourcePath.relative)).extension() != ".ursashader")
	{
		return Result<std::unique_ptr<ShaderImportView>>::Failure("Shader source path must end with .ursashader");
	}
	auto source = filesystem.ReadText(sourcePath);
	if (!source)
	{
		return Result<std::unique_ptr<ShaderImportView>>::Failure(source.error);
	}
	const auto writable = filesystem.CheckWritable(sourcePath);
	const bool readOnly = sourcePath.scheme != projectResourceScheme || !writable;
	return Result<std::unique_ptr<ShaderImportView>>::Success(std::unique_ptr<ShaderImportView>(
	    new ShaderImportView(std::move(sourcePath), std::move(source.value), readOnly)));
}

Result<void> ShaderImportView::Reimport(ProjectWorkspace& workspace)
{
	diagnostic.clear();
	dependencies.clear();
	importedAsset.reset();
	VirtualFileSystem filesystem = workspace.Index().Filesystem();
	auto cacheProvider = std::make_shared<MemoryFileProvider>();
	auto cacheMount = filesystem.MountProvider("shadercache", cacheProvider, MountAccess::ReadWrite);
	if (!cacheMount)
	{
		diagnostic = cacheMount.error;
		return cacheMount;
	}
	String outputScheme = sourcePath.scheme;
	if (SourceReadOnly())
	{
		outputScheme = "editorimport";
		auto outputMount =
		    filesystem.MountProvider(outputScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
		if (!outputMount)
		{
			diagnostic = outputMount.error;
			return outputMount;
		}
	}
	Shader::DxcShaderCompiler compiler({std::filesystem::u8path(URSA_DXC_EXECUTABLE),
	                                    std::filesystem::u8path(URSA_SPIRV_VAL_EXECUTABLE),
	                                    std::filesystem::u8path(URSA_SPIRV_CROSS_EXECUTABLE)});
	auto version = compiler.Version();
	if (!version)
	{
		diagnostic = version.error;
		return Result<void>::Failure(diagnostic);
	}
	auto library = Shader::ShaderSourceLibrary::Load(filesystem, sourcePath.scheme);
	if (!library)
	{
		diagnostic = library.error;
		return Result<void>::Failure(diagnostic);
	}
	const auto sources = library.value.Sources();
	const auto selected = std::find_if(sources.begin(), sources.end(), [&](const Shader::ShaderSourceAsset* source)
	                                   { return source->path == sourcePath; });
	if (selected == sources.end())
	{
		diagnostic = "Shader source is not present in the mounted VFS: " + sourcePath.ToString();
		return Result<void>::Failure(diagnostic);
	}
	auto resolved = library.value.ResolveDependencies(filesystem, (*selected)->source.name);
	if (!resolved)
	{
		diagnostic = resolved.error;
		return Result<void>::Failure(diagnostic);
	}
	for (const VirtualPath& include : resolved.value.includes)
	{
		dependencies.push_back(include.ToString());
	}
	auto cacheRoot = VirtualPath::Parse("shadercache:Shader");
	if (!cacheRoot)
	{
		diagnostic = cacheRoot.error;
		return Result<void>::Failure(diagnostic);
	}
	auto imported = Shader::ShaderImporter::ImportAll(filesystem, compiler, version.value, sourcePath.scheme,
	                                                  outputScheme, cacheRoot.value);
	if (!imported)
	{
		diagnostic = imported.error;
		return Result<void>::Failure(diagnostic);
	}
	const auto asset = std::find_if(imported.value.begin(), imported.value.end(), [&](const Shader::ShaderAsset& value)
	                                { return value.sourcePath == sourcePath.ToString(); });
	if (asset == imported.value.end())
	{
		diagnostic = "Shader importer completed without producing the selected asset";
		return Result<void>::Failure(diagnostic);
	}
	importedAsset = *asset;
	if (!SourceReadOnly())
	{
		auto refreshed = workspace.Refresh();
		if (!refreshed)
		{
			diagnostic = refreshed.error;
			importedAsset.reset();
			return refreshed;
		}
	}
	return {};
}
} // namespace Ursa::Editor
