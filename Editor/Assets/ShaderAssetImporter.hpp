#pragma once

#include <Assets/AssetDatabase.hpp>
#include <Shader/ShaderAsset.hpp>
#include <Shader/ShaderCompiler.hpp>

namespace Ursa::Editor
{
struct ShaderAssetImportResult
{
	Shader::ShaderAsset asset;
	VirtualPath artifactPath;
	Vector<VirtualPath> dependencies;
	bool cacheHit = false;
};

Shader::ShaderCompilerTools DefaultShaderCompilerTools();

class ShaderAssetImporter final
{
public:
	static Result<ShaderAssetImportResult> Import(AssetDatabase& database, const VirtualPath& source,
	                                              Shader::ShaderCompilerTools tools = DefaultShaderCompilerTools(),
	                                              bool force = false);

	static Result<ShaderAssetImportResult> Preview(const VirtualFileSystem& files, const VirtualPath& source,
	                                               Shader::ShaderCompilerTools tools = DefaultShaderCompilerTools());
};
} // namespace Ursa::Editor
