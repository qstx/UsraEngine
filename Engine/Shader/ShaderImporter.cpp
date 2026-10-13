#include <Shader/ShaderImporter.hpp>

#include <Core/StringInterop.hpp>
#include <Material/Material.hpp>
#include <Serialization/MaterialResource.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Ursa::Shader
{
namespace
{
void AddSharedBlocks(const Vector<ProgramBlock>& blocks, Vector<ProgramBlock>& output)
{
	for (const ProgramBlock& block : blocks)
	{
		if (block.kind == ProgramBlockKind::Include)
		{
			output.push_back(block);
		}
	}
}

String DirectiveKey(const ShaderKeywordDirective& directive)
{
	String key = directive.kind == ShaderKeywordDirectiveKind::MultiCompile ? "multi:" : "feature:";
	for (const String& option : directive.options)
	{
		key += option;
		key += ';';
	}
	return key;
}

String SourceFingerprint(const VirtualFileSystem& filesystem, const ShaderSourceAsset& source,
                         const ShaderDependencyReport& dependencies, const String& compilerVersion,
                         const ShaderSourceLibrary& library)
{
	const auto hashBytes = [](Ursa::UInt64& hash, const void* data, Ursa::Size size)
	{
		const auto* bytes = static_cast<const Ursa::UInt8*>(data);
		for (Ursa::Size index = 0; index < size; ++index)
		{
			hash ^= bytes[index];
			hash *= 1099511628211ull;
		}
	};
	Ursa::UInt64 hash = 14695981039346656037ull;
	constexpr char importerVersion[] = "UrsaShaderImportV2";
	hashBytes(hash, importerVersion, sizeof(importerVersion) - 1);
	hashBytes(hash, &source.source.formatVersion, sizeof(source.source.formatVersion));
	hashBytes(hash, &shaderReflectionFormatVersion, sizeof(shaderReflectionFormatVersion));
	const auto sourceBytes = filesystem.Read(source.path);
	if (!sourceBytes)
	{
		throw std::runtime_error(ToStdString(sourceBytes.error));
	}
	hashBytes(hash, sourceBytes.value.data(), sourceBytes.value.size());
	hashBytes(hash, source.source.id.bytes.data(), source.source.id.bytes.size());
	hashBytes(hash, compilerVersion.data(), compilerVersion.size());
	for (const auto& reference : dependencies.shaderReferences)
	{
		const auto* dependency = library.Find(reference);
		if (!dependency)
		{
			throw std::runtime_error("Shader dependency identity is missing");
		}
		hashBytes(hash, dependency->source.id.bytes.data(), dependency->source.id.bytes.size());
		const String dependencyPath = dependency->path.ToString();
		hashBytes(hash, dependencyPath.data(), dependencyPath.size());
		hashBytes(hash, &dependency->source.formatVersion, sizeof(dependency->source.formatVersion));
		const auto dependencyBytes = filesystem.Read(dependency->path);
		if (!dependencyBytes)
		{
			throw std::runtime_error(ToStdString(dependencyBytes.error));
		}
		hashBytes(hash, dependencyBytes.value.data(), dependencyBytes.value.size());
	}
	for (const VirtualPath& includePath : dependencies.includes)
	{
		const auto include = filesystem.Read(includePath);
		if (!include)
		{
			throw std::runtime_error(ToStdString(include.error));
		}
		const String includeName = includePath.ToString();
		hashBytes(hash, includeName.data(), includeName.size());
		hashBytes(hash, include.value.data(), include.value.size());
	}
	std::ostringstream text;
	text << "fnv1a64-" << std::hex << std::setfill('0') << std::setw(16) << hash;
	return ToUrsaString(text.str());
}
} // namespace

Result<Vector<ShaderAsset>> ShaderImporter::ImportAll(VirtualFileSystem& filesystem, const DxcShaderCompiler& compiler,
                                                      const String& compilerVersion, const String& sourceScheme,
                                                      const String& outputScheme, const VirtualPath& cacheRoot,
                                                      UInt32 maxVariantCount,
                                                      const UsedShaderKeywordSets& usedKeywordSets,
                                                      const VirtualPath& requestedSource)
{
	auto library = ShaderSourceLibrary::Load(filesystem, sourceScheme, requestedSource);
	if (!library)
	{
		return Result<Vector<ShaderAsset>>::Failure(library.error);
	}
	UsedShaderKeywordSets projectKeywordSets = usedKeywordSets;
	const auto sources = library.value.Sources();
	const auto defaultTexture = DefaultMaterialTexture(MaterialTextureSlot::BaseColor);
	auto files = filesystem.List(sourceScheme);
	if (!files)
	{
		return Result<Vector<ShaderAsset>>::Failure(files.error);
	}
	for (const VirtualFileEntry& file : files.value)
	{
		if (std::filesystem::u8path(ToStdString(file.path.relative)).extension() != ".ursamat")
		{
			continue;
		}
		auto material = LoadMaterialResource(filesystem, file.path, [&](ResourceId)
		                                     { return Result<MaterialTexturePtr>::Success(defaultTexture); });
		if (!material)
		{
			return Result<Vector<ShaderAsset>>::Failure(material.error);
		}
		if (!material.value.material->IsShaderBacked())
		{
			continue;
		}
		const auto shader = std::find_if(sources.begin(), sources.end(), [&](const ShaderSourceAsset* candidate)
		                                 { return candidate->source.id == material.value.material->ShaderId(); });
		if (shader != sources.end())
		{
			projectKeywordSets[(*shader)->source.name].push_back(material.value.material->Keywords());
		}
	}
	Vector<ShaderAsset> imported;
	for (const ShaderSourceAsset* source : sources)
	{
		if (!requestedSource.relative.empty() && source->path != requestedSource)
		{
			continue;
		}
		auto dependencies = library.value.ResolveDependencies(filesystem, source->source.name);
		if (!dependencies)
		{
			return Result<Vector<ShaderAsset>>::Failure(dependencies.error);
		}
		Vector<ShaderIncludeSource> includes;
		for (const VirtualPath& includePath : dependencies.value.includes)
		{
			auto content = filesystem.ReadText(includePath);
			if (!content)
			{
				return Result<Vector<ShaderAsset>>::Failure(content.error);
			}
			includes.push_back({includePath, std::move(content.value)});
		}
		Vector<ProgramBlock> rootSharedBlocks;
		AddSharedBlocks(source->source.includes, rootSharedBlocks);
		Vector<ShaderPassCompilation> compilations;
		for (Ursa::Size subShaderIndex = 0; subShaderIndex < source->source.subShaders.size(); ++subShaderIndex)
		{
			const SubShaderDeclaration& subShader = source->source.subShaders[subShaderIndex];
			Vector<ProgramBlock> subShaderSharedBlocks = rootSharedBlocks;
			AddSharedBlocks(subShader.includes, subShaderSharedBlocks);
			for (Ursa::Size passIndex = 0; passIndex < subShader.passes.size(); ++passIndex)
			{
				const PassDeclaration& pass = subShader.passes[passIndex];
				Vector<ProgramBlock> passSharedBlocks = subShaderSharedBlocks;
				AddSharedBlocks(pass.programs, passSharedBlocks);
				Vector<const ProgramBlock*> programs;
				Vector<ShaderKeywordDirective> directives;
				Set<String> seenDirectives;
				for (const ProgramBlock& block : pass.programs)
				{
					if (block.kind != ProgramBlockKind::Program)
					{
						continue;
					}
					programs.push_back(&block);
					for (const ShaderKeywordDirective& directive : block.keywordDirectives)
					{
						if (seenDirectives.insert(DirectiveKey(directive)).second)
						{
							directives.push_back(directive);
						}
					}
				}
				if (programs.empty())
				{
					continue;
				}
				Vector<Vector<String>> used;
				auto usedForShader = projectKeywordSets.find(source->source.name);
				if (usedForShader != projectKeywordSets.end())
				{
					used = usedForShader->second;
				}
				auto variants = ExpandShaderKeywordVariants(directives, used, maxVariantCount);
				if (!variants)
				{
					return Result<Vector<ShaderAsset>>::Failure(source->path.ToString() + ": " + variants.error);
				}
				for (const ShaderKeywordVariant& variant : variants.value)
				{
					ShaderPassCompilation compilation;
					compilation.subShaderIndex = static_cast<UInt32>(subShaderIndex);
					compilation.passIndex = static_cast<UInt32>(passIndex);
					compilation.keywords = variant.keywords;
					compilation.required = variant.required;
					for (const ProgramBlock* program : programs)
					{
						for (const ProgramEntryPoint& entryPoint : program->entryPoints)
						{
							ShaderCompileRequest request;
							request.sourcePath = source->path;
							request.entryPoint = entryPoint;
							request.source = program->source;
							request.sharedBlocks = passSharedBlocks;
							request.includes = includes;
							request.keywords = variant.keywords;
							auto compiled =
							    ShaderCompileCache::CompileCached(filesystem, cacheRoot, compiler, compilerVersion,
							                                      request, ShaderTarget::VulkanSpirv, variant.keywords);
							if (!compiled)
							{
								return Result<Vector<ShaderAsset>>::Failure(compiled.error);
							}
							compilation.stages.push_back(std::move(compiled.value));
						}
					}
					compilations.push_back(std::move(compilation));
				}
			}
		}
		String fingerprint;
		try
		{
			fingerprint = SourceFingerprint(filesystem, *source, dependencies.value, compilerVersion, library.value);
		}
		catch (const std::exception& error)
		{
			return Result<Vector<ShaderAsset>>::Failure(source->path.ToString() + ": " + ToUrsaString(error.what()));
		}
		auto shader =
		    BuildShaderAsset(*source, library.value, dependencies.value, fingerprint, compilerVersion, compilations);
		if (!shader)
		{
			return Result<Vector<ShaderAsset>>::Failure(shader.error);
		}
		VirtualPath outputPath = source->path;
		outputPath.scheme = outputScheme;
		std::filesystem::path relative = PathFromUrsaString(outputPath.relative);
		relative.replace_extension(".ursashaderasset");
		auto virtualOutput = VirtualPath::Parse(outputScheme + ":" + PathToUrsaString(relative));
		if (!virtualOutput)
		{
			return Result<Vector<ShaderAsset>>::Failure(virtualOutput.error);
		}
		auto saved = SaveShaderAssetResource(filesystem, virtualOutput.value, shader.value);
		if (!saved)
		{
			return Result<Vector<ShaderAsset>>::Failure(saved.error);
		}
		imported.push_back(std::move(shader.value));
	}
	return Result<Vector<ShaderAsset>>::Success(std::move(imported));
}
} // namespace Ursa::Shader
