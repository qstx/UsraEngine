#pragma once

#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Shader/ShaderSource.hpp>
#include <Vfs/VirtualPath.hpp>
#include <filesystem>

namespace Ursa::Shader
{
inline constexpr const char* shaderBindingPolicyVersion = "UrsaBindingMapV3Auto";

struct ShaderIncludeSource
{
	VirtualPath path;
	String source;
};

struct ShaderCompileRequest
{
	VirtualPath sourcePath;
	ProgramEntryPoint entryPoint;
	String source;
	Vector<ProgramBlock> sharedBlocks;
	Vector<ShaderIncludeSource> includes;
	Vector<String> keywords;
};

struct CompiledShaderStage
{
	ProgramStage stage = ProgramStage::Vertex;
	String entryPoint;
	Vector<UInt8> spirv;
	String reflectionJson;
	String compilerVersion;
	String diagnostics;
};

struct ShaderCompilerTools
{
	std::filesystem::path dxc;
	std::filesystem::path spirvValidator;
	std::filesystem::path spirvCross;
	std::filesystem::path stagingRoot;
};

class DxcShaderCompiler final
{
public:
	explicit DxcShaderCompiler(ShaderCompilerTools selectedTools);

	Result<String> Version() const;

	Result<CompiledShaderStage> Compile(const ShaderCompileRequest& request) const;

private:
	ShaderCompilerTools tools;
};
} // namespace Ursa::Shader
