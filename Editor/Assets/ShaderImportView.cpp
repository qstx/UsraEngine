#include <Assets/ShaderImportView.hpp>
#include <Assets/ShaderAssetImporter.hpp>

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

Result<void> ShaderImportView::Reimport(ProjectWorkspace& workspace, bool force)
{
	diagnostic.clear();
	auto* database = sourcePath.scheme == engineResourceScheme ? workspace.BuiltinDatabase() : workspace.Database();
	auto imported = database ? ShaderAssetImporter::Import(*database, sourcePath, DefaultShaderCompilerTools(), force)
	                         : ShaderAssetImporter::Preview(workspace.Index().Filesystem(), sourcePath);
	if (!imported)
	{
		diagnostic = imported.error;
		return Result<void>::Failure(diagnostic);
	}
	importedAsset = std::move(imported.value.asset);
	dependencies.clear();
	for (const auto& dependency : imported.value.dependencies)
	{
		dependencies.push_back(dependency.ToString());
	}
	auto currentText = workspace.Index().Filesystem().ReadText(sourcePath);
	if (currentText)
	{
		sourceText = std::move(currentText.value);
	}
	return {};
}
} // namespace Ursa::Editor