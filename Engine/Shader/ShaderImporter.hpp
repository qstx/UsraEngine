#pragma once

#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderCompileCache.hpp>
#include <Shader/ShaderSourceLibrary.hpp>

namespace Ursa::Shader
{
using UsedShaderKeywordSets = Map<String, Vector<Vector<String>>>;

class ShaderImporter final
{
public:
	static Result<Vector<ShaderAsset>> ImportAll(VirtualFileSystem& filesystem, const DxcShaderCompiler& compiler,
	                                             const String& compilerVersion, const String& sourceScheme,
	                                             const String& outputScheme, const VirtualPath& cacheRoot,
	                                             UInt32 maxVariantCount = 256,
	                                             const UsedShaderKeywordSets& usedKeywordSets = {});
};
} // namespace Ursa::Shader
