#include <Shader/ShaderCompileCache.hpp>

#include <Core/StringInterop.hpp>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Ursa::Shader
{
namespace
{
class StableHash final
{
public:
	void AddByte(UInt8 value)
	{
		first ^= value;
		first *= 1099511628211ull;
		second ^= static_cast<UInt64>(value) + 0x9e3779b97f4a7c15ull + (second << 6) + (second >> 2);
		second *= 0x100000001b3ull;
	}

	void Add(const String& value)
	{
		AddUInt64(value.size());
		for (char character : value)
		{
			AddByte(static_cast<UInt8>(static_cast<unsigned char>(character)));
		}
	}

	void AddUInt64(UInt64 value)
	{
		for (Ursa::Size index = 0; index < sizeof(value); ++index)
		{
			AddByte(static_cast<UInt8>((value >> (index * 8)) & 0xff));
		}
	}

	String Finish() const
	{
		String result;
		AppendHex(result, first);
		AppendHex(result, second);
		return result;
	}

private:
	static void AppendHex(String& result, UInt64 value)
	{
		static constexpr char digits[] = "0123456789abcdef";
		for (Int32 index = 15; index >= 0; --index)
		{
			result.push_back(digits[(value >> (index * 4)) & 0xf]);
		}
	}

	UInt64 first = 14695981039346656037ull;
	UInt64 second = 0x6a09e667f3bcc909ull;
};

String TargetName(ShaderTarget target)
{
	switch (target)
	{
		case ShaderTarget::VulkanSpirv:
			return "VulkanSpirv";
		case ShaderTarget::OpenGlGlsl:
			return "OpenGlGlsl";
		case ShaderTarget::D3D12Dxil:
			return "D3D12Dxil";
	}
	return "Unknown";
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

Vector<String> NormalizeKeywords(const Vector<String>& keywords)
{
	Vector<String> normalized = keywords;
	std::sort(normalized.begin(), normalized.end());
	normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
	return normalized;
}

struct CachePaths
{
	VirtualPath bytecode;
	VirtualPath reflection;
	VirtualPath metadata;
};

Result<CachePaths> Paths(const VirtualPath& root, const String& key)
{
	auto bytecode = VirtualPath::Parse(root.ToString() + "/" + key + ".spv");
	auto reflection = VirtualPath::Parse(root.ToString() + "/" + key + ".reflection.json");
	auto metadata = VirtualPath::Parse(root.ToString() + "/" + key + ".cache.json");
	if (!bytecode || !reflection || !metadata)
	{
		return Result<CachePaths>::Failure(!bytecode     ? bytecode.error
		                                   : !reflection ? reflection.error
		                                                 : metadata.error);
	}
	return Result<CachePaths>::Success(
	    {std::move(bytecode.value), std::move(reflection.value), std::move(metadata.value)});
}

Result<Vector<UInt8>> Bytes(const String& text)
{
	Vector<UInt8> result;
	result.reserve(text.size());
	for (char character : text)
	{
		result.push_back(static_cast<UInt8>(static_cast<unsigned char>(character)));
	}
	return Result<Vector<UInt8>>::Success(std::move(result));
}

bool ValidSpirv(const Vector<UInt8>& bytes)
{
	return bytes.size() >= sizeof(UInt32) && bytes.size() % sizeof(UInt32) == 0 && bytes[0] == 0x03 &&
	       bytes[1] == 0x02 && bytes[2] == 0x23 && bytes[3] == 0x07;
}

Result<CompiledShaderStage> ReadCache(const VirtualFileSystem& filesystem, const CachePaths& paths, const String& key,
                                      const String& compilerVersion, const ShaderCompileRequest& request,
                                      ShaderTarget target, const Vector<String>& keywords)
{
	auto metadataText = filesystem.ReadText(paths.metadata);
	if (!metadataText)
	{
		return Result<CompiledShaderStage>::Failure(metadataText.error);
	}
	auto reflection = filesystem.ReadText(paths.reflection);
	if (!reflection)
	{
		return Result<CompiledShaderStage>::Failure(reflection.error);
	}
	auto bytecode = filesystem.Read(paths.bytecode);
	if (!bytecode)
	{
		return Result<CompiledShaderStage>::Failure(bytecode.error);
	}
	try
	{
		const auto metadata = nlohmann::json::parse(metadataText.value.begin(), metadataText.value.end());
		if (metadata.value("key", std::string{}) != ToStdString(key) ||
		    metadata.value("target", std::string{}) != ToStdString(TargetName(target)) ||
		    metadata.value("stage", std::string{}) != ToStdString(StageName(request.entryPoint.stage)) ||
		    metadata.value("entryPoint", std::string{}) != ToStdString(request.entryPoint.name) ||
		    metadata.value("compilerVersion", std::string{}) != ToStdString(compilerVersion) ||
		    !ValidSpirv(bytecode.value))
		{
			return Result<CompiledShaderStage>::Failure("Shader compile cache metadata is stale or invalid");
		}
		const Vector<String> expectedKeywords = NormalizeKeywords(keywords);
		if (!metadata.contains("keywords") || !metadata["keywords"].is_array() ||
		    metadata["keywords"].size() != expectedKeywords.size())
		{
			return Result<CompiledShaderStage>::Failure("Shader compile cache keyword metadata is invalid");
		}
		for (Ursa::Size index = 0; index < expectedKeywords.size(); ++index)
		{
			if (!metadata["keywords"][index].is_string() ||
			    metadata["keywords"][index].get<std::string>() != ToStdString(expectedKeywords[index]))
			{
				return Result<CompiledShaderStage>::Failure("Shader compile cache keyword metadata is stale");
			}
		}
		if (!nlohmann::json::parse(reflection.value.begin(), reflection.value.end()).is_object())
		{
			return Result<CompiledShaderStage>::Failure("Shader compile cache reflection must be a JSON object");
		}
		return Result<CompiledShaderStage>::Success({request.entryPoint.stage,
		                                             request.entryPoint.name,
		                                             std::move(bytecode.value),
		                                             std::move(reflection.value),
		                                             compilerVersion,
		                                             {}});
	}
	catch (const nlohmann::json::exception& exception)
	{
		return Result<CompiledShaderStage>::Failure("Invalid shader compile cache metadata: " +
		                                            ToUrsaString(exception.what()));
	}
}

Result<void> RemoveCache(VirtualFileSystem& filesystem, const CachePaths& paths)
{
	for (const VirtualPath* path : {&paths.bytecode, &paths.reflection, &paths.metadata})
	{
		auto exists = filesystem.Exists(*path);
		if (!exists)
		{
			return Result<void>::Failure(exists.error);
		}
		if (exists.value)
		{
			auto removed = filesystem.Remove(*path);
			if (!removed)
			{
				return removed;
			}
		}
	}
	return {};
}
} // namespace

String ShaderCompileCache::ComputeKey(const ShaderCompileRequest& request, const String& compilerVersion,
                                      ShaderTarget target, const Vector<String>& keywords)
{
	StableHash hash;
	hash.Add("UrsaShaderCompileCacheV2");
	hash.Add(request.sourcePath.ToString());
	hash.Add(compilerVersion);
	hash.Add(TargetName(target));
	hash.Add(StageName(request.entryPoint.stage));
	hash.Add(request.entryPoint.name);
	hash.AddUInt64(request.entryPoint.location.line);
	hash.Add(request.source);
	hash.AddUInt64(request.sharedBlocks.size());
	for (const ProgramBlock& block : request.sharedBlocks)
	{
		hash.AddUInt64(static_cast<UInt64>(block.kind));
		hash.AddUInt64(block.location.line);
		hash.Add(block.source);
	}
	Vector<ShaderIncludeSource> includes = request.includes;
	std::sort(includes.begin(), includes.end(), [](const ShaderIncludeSource& left, const ShaderIncludeSource& right)
	          { return left.path.ToString() < right.path.ToString(); });
	hash.AddUInt64(includes.size());
	for (const ShaderIncludeSource& include : includes)
	{
		hash.Add(include.path.ToString());
		hash.Add(include.source);
	}
	const Vector<String> selectedKeywords = NormalizeKeywords(keywords);
	hash.AddUInt64(selectedKeywords.size());
	for (const String& keyword : selectedKeywords)
	{
		hash.Add(keyword);
	}
	return hash.Finish();
}

Result<CompiledShaderStage> ShaderCompileCache::CompileCached(VirtualFileSystem& filesystem,
                                                              const VirtualPath& cacheRoot,
                                                              const DxcShaderCompiler& compiler,
                                                              const String& compilerVersion,
                                                              const ShaderCompileRequest& request, ShaderTarget target,
                                                              const Vector<String>& keywords)
{
	if (target != ShaderTarget::VulkanSpirv)
	{
		return Result<CompiledShaderStage>::Failure("Shader target is not supported by the current compiler backend");
	}
	const String key = ComputeKey(request, compilerVersion, target, keywords);
	auto cachePaths = Paths(cacheRoot, key);
	if (!cachePaths)
	{
		return Result<CompiledShaderStage>::Failure(cachePaths.error);
	}
	auto bytecodeExists = filesystem.Exists(cachePaths.value.bytecode);
	auto reflectionExists = filesystem.Exists(cachePaths.value.reflection);
	auto metadataExists = filesystem.Exists(cachePaths.value.metadata);
	if (!bytecodeExists || !reflectionExists || !metadataExists)
	{
		return Result<CompiledShaderStage>::Failure(!bytecodeExists     ? bytecodeExists.error
		                                            : !reflectionExists ? reflectionExists.error
		                                                                : metadataExists.error);
	}
	if (bytecodeExists.value && reflectionExists.value && metadataExists.value)
	{
		auto cached = ReadCache(filesystem, cachePaths.value, key, compilerVersion, request, target, keywords);
		if (cached)
		{
			return cached;
		}
		auto removed = RemoveCache(filesystem, cachePaths.value);
		if (!removed)
		{
			return Result<CompiledShaderStage>::Failure(removed.error);
		}
	}
	ShaderCompileRequest compileRequest = request;
	compileRequest.keywords = NormalizeKeywords(keywords);
	auto compiled = compiler.Compile(compileRequest);
	if (!compiled)
	{
		return compiled;
	}
	auto reflectionBytes = Bytes(compiled.value.reflectionJson);
	if (!reflectionBytes)
	{
		return Result<CompiledShaderStage>::Failure(reflectionBytes.error);
	}
	nlohmann::json metadata = {{"key", ToStdString(key)},
	                           {"target", ToStdString(TargetName(target))},
	                           {"stage", ToStdString(StageName(request.entryPoint.stage))},
	                           {"entryPoint", ToStdString(request.entryPoint.name)},
	                           {"compilerVersion", ToStdString(compilerVersion)},
	                           {"keywords", nlohmann::json::array()}};
	const Vector<String> selectedKeywords = NormalizeKeywords(keywords);
	for (const String& keyword : selectedKeywords)
	{
		metadata["keywords"].push_back(ToStdString(keyword));
	}
	const String metadataText = ToUrsaString(metadata.dump(2));
	auto metadataBytes = Bytes(metadataText);
	if (!metadataBytes)
	{
		return Result<CompiledShaderStage>::Failure(metadataBytes.error);
	}
	auto savedBytecode = filesystem.WriteAtomic(cachePaths.value.bytecode, compiled.value.spirv, true);
	if (!savedBytecode)
	{
		return Result<CompiledShaderStage>::Failure(savedBytecode.error);
	}
	auto savedReflection = filesystem.WriteAtomic(cachePaths.value.reflection, reflectionBytes.value, true);
	if (!savedReflection)
	{
		return Result<CompiledShaderStage>::Failure(savedReflection.error);
	}
	auto savedMetadata = filesystem.WriteAtomic(cachePaths.value.metadata, metadataBytes.value, true);
	if (!savedMetadata)
	{
		return Result<CompiledShaderStage>::Failure(savedMetadata.error);
	}
	compiled.value.compilerVersion = compilerVersion;
	return compiled;
}
} // namespace Ursa::Shader
