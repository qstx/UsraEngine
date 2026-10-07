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
	const bool databaseImport = !SourceReadOnly() && workspace.Database();
	if (databaseImport)
	{
		auto refreshed = workspace.Database()->Refresh();
		if (!refreshed)
		{
			diagnostic = refreshed.error;
			return refreshed;
		}
	}
	const String outputScheme = "shaderoutput";
	auto outputProvider = std::make_shared<MemoryFileProvider>();
	auto outputMount = filesystem.MountProvider(outputScheme, outputProvider, MountAccess::ReadWrite);
	if (!outputMount)
	{
		diagnostic = outputMount.error;
		return outputMount;
	}
	VirtualPath cacheRoot;
	if (filesystem.IsMounted(editorLibraryScheme))
	{
		auto parsed = VirtualPath::Parse(String(editorLibraryScheme) + ":ShaderCompileCache");
		if (!parsed)
		{
			diagnostic = parsed.error;
			return Result<void>::Failure(diagnostic);
		}
		cacheRoot = std::move(parsed.value);
	}
	else
	{
		const String cacheScheme = "shadercache";
		auto cacheMount =
		    filesystem.MountProvider(cacheScheme, std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite);
		if (!cacheMount)
		{
			diagnostic = cacheMount.error;
			return cacheMount;
		}
		auto parsed = VirtualPath::Parse(cacheScheme + ":Shader");
		if (!parsed)
		{
			diagnostic = parsed.error;
			return Result<void>::Failure(diagnostic);
		}
		cacheRoot = std::move(parsed.value);
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
	for (const String& reference : resolved.value.shaderReferences)
	{
		const Shader::ShaderSourceAsset* referenced = library.value.Find(reference);
		if (!referenced)
		{
			diagnostic = "Shader dependency is missing from the source library: " + reference;
			return Result<void>::Failure(diagnostic);
		}
		dependencies.push_back(referenced->path.ToString());
	}
	std::sort(dependencies.begin(), dependencies.end());
	dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
	auto imported = Shader::ShaderImporter::ImportAll(filesystem, compiler, version.value, sourcePath.scheme,
	                                                  outputScheme, cacheRoot);
	if (!imported)
	{
		diagnostic = imported.error;
		return Result<void>::Failure(diagnostic);
	}
	const auto selectedAsset =
	    std::find_if(imported.value.begin(), imported.value.end(),
	                 [&](const Shader::ShaderAsset& value) { return value.sourcePath == sourcePath.ToString(); });
	if (selectedAsset == imported.value.end())
	{
		diagnostic = "Shader importer completed without producing the selected asset";
		return Result<void>::Failure(diagnostic);
	}
	if (databaseImport)
	{
		const nlohmann::json settings = {{"compilerVersion", ToStdString(version.value)}, {"target", "VulkanSpirv"}};
		for (const Shader::ShaderAsset& asset : imported.value)
		{
			auto assetSource = VirtualPath::Parse(asset.sourcePath);
			if (!assetSource || assetSource.value.scheme != projectResourceScheme)
			{
				continue;
			}
			auto metadata = workspace.Database()->Metadata(assetSource.value);
			if (!metadata)
			{
				diagnostic = metadata.error;
				return Result<void>::Failure(diagnostic);
			}
			const Shader::ShaderSourceAsset* shaderSource = library.value.Find(asset.name);
			if (!shaderSource)
			{
				diagnostic = "Shader source is missing from the loaded source library: " + assetSource.value.ToString();
				return Result<void>::Failure(diagnostic);
			}
			auto dependencyReport = library.value.ResolveDependencies(filesystem, shaderSource->source.name);
			if (!dependencyReport)
			{
				diagnostic = dependencyReport.error;
				return Result<void>::Failure(diagnostic);
			}
			Vector<VirtualPath> assetDependencies = dependencyReport.value.includes;
			for (const String& reference : dependencyReport.value.shaderReferences)
			{
				const Shader::ShaderSourceAsset* referenced = library.value.Find(reference);
				if (referenced)
				{
					assetDependencies.push_back(referenced->path);
				}
			}
			std::sort(assetDependencies.begin(), assetDependencies.end(),
			          [](const VirtualPath& left, const VirtualPath& right)
			          { return left.ToString() < right.ToString(); });
			assetDependencies.erase(std::unique(assetDependencies.begin(), assetDependencies.end()),
			                        assetDependencies.end());
			auto settingsSaved =
			    workspace.Database()->SetImporterSettings(assetSource.value, "ursa.Shader", 1, settings);
			if (!settingsSaved)
			{
				diagnostic = settingsSaved.error;
				return settingsSaved;
			}
			VirtualPath outputPath = assetSource.value;
			outputPath.scheme = outputScheme;
			std::filesystem::path relative = PathFromUrsaString(outputPath.relative);
			relative.replace_extension(".ursashaderasset");
			auto shaderArtifactPath = VirtualPath::Parse(outputScheme + ":" + PathToUrsaString(relative));
			if (!shaderArtifactPath)
			{
				diagnostic = shaderArtifactPath.error;
				return Result<void>::Failure(diagnostic);
			}
			auto bytes = filesystem.Read(shaderArtifactPath.value);
			if (!bytes)
			{
				diagnostic = bytes.error;
				return Result<void>::Failure(diagnostic);
			}
			Vector<ImportedArtifact> artifacts;
			artifacts.push_back({0, "Shader", std::move(bytes.value)});
			auto published = workspace.Database()->PublishImport(assetSource.value, "ursa.Shader", 1, "vulkan-spv",
			                                                     settings, assetDependencies, artifacts);
			if (!published)
			{
				diagnostic = published.error;
				return published;
			}
		}
		auto refreshed = workspace.Refresh();
		if (!refreshed)
		{
			diagnostic = refreshed.error;
			importedAsset.reset();
			return refreshed;
		}
	}
	importedAsset = *selectedAsset;
	if (sourcePath.scheme == projectResourceScheme)
	{
		dependencies.clear();
		for (const VirtualPath& include : resolved.value.includes)
		{
			dependencies.push_back(include.ToString());
		}
		for (const String& reference : resolved.value.shaderReferences)
		{
			dependencies.push_back(reference);
		}
	}
	return {};
}
} // namespace Ursa::Editor
