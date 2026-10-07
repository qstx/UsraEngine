#pragma once

#include <Shader/ShaderCompiler.hpp>
#include <Vfs/VirtualFileSystem.hpp>

namespace Ursa::Shader
{
enum class ShaderTarget
{
	VulkanSpirv,
	OpenGlGlsl,
	D3D12Dxil
};

class ShaderCompileCache final
{
public:
	static String ComputeKey(const ShaderCompileRequest& request, const String& compilerVersion, ShaderTarget target,
	                         const Vector<String>& keywords);

	static Result<CompiledShaderStage> CompileCached(VirtualFileSystem& filesystem, const VirtualPath& cacheRoot,
	                                                 const DxcShaderCompiler& compiler, const String& compilerVersion,
	                                                 const ShaderCompileRequest& request, ShaderTarget target,
	                                                 const Vector<String>& keywords);
};
} // namespace Ursa::Shader
