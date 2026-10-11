#include <Shader/ShaderCompileCache.hpp>
#include <Vfs/MemoryFileProvider.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Shader;

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

ShaderCompileRequest MakeRequest(const String& includeSource)
{
	ShaderCompileRequest request;
	request.sourcePath = VirtualPath::Parse("prj:Shaders/CacheTest.ursashader").value;
	request.entryPoint = {ProgramStage::Vertex, "VSMain", {8, 3}};
	request.source = "#include \"Shared.hlsli\"\nfloat4 VSMain(float3 position : POSITION) : SV_Position "
	                 "{ return MakePosition(position); }\n";
	request.includes.push_back({VirtualPath::Parse("prj:Shaders/Shared.hlsli").value, includeSource});
	return request;
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem filesystem;
		auto provider = std::make_shared<MemoryFileProvider>();
		Expect(static_cast<bool>(filesystem.MountProvider("user", provider, MountAccess::ReadWrite)),
		       "Cannot mount the in-memory compiler cache");
		const auto cacheRoot = VirtualPath::Parse("user:ShaderCache").value;
		DxcShaderCompiler compiler({std::filesystem::u8path(URSA_TEST_DXC_EXECUTABLE),
		                            std::filesystem::u8path(URSA_TEST_SPIRV_VAL_EXECUTABLE),
		                            std::filesystem::u8path(URSA_TEST_SPIRV_CROSS_EXECUTABLE)});
		const String compilerVersion = compiler.Version().value;
		const Vector<String> keywords{"FOG_ON", "ALPHA_TEST"};
		auto first = MakeRequest("float4 MakePosition(float3 value) { return float4(value, 1.0); }\n");
		const String firstKey =
		    ShaderCompileCache::ComputeKey(first, compilerVersion, ShaderTarget::VulkanSpirv, keywords);
		Expect(firstKey == ShaderCompileCache::ComputeKey(first, compilerVersion, ShaderTarget::VulkanSpirv,
		                                                  {"ALPHA_TEST", "FOG_ON"}),
		       "Keyword order should not change the cache key");
		Expect(firstKey != ShaderCompileCache::ComputeKey(first, compilerVersion + "changed", ShaderTarget::VulkanSpirv,
		                                                  keywords),
		       "Changing the compiler version must change the cache key");
		Expect(firstKey != ShaderCompileCache::ComputeKey(first, compilerVersion, ShaderTarget::OpenGlGlsl, keywords) &&
		           firstKey !=
		               ShaderCompileCache::ComputeKey(first, compilerVersion, ShaderTarget::D3D12Dxil, keywords),
		       "Different backend targets must have different cache keys");
		auto changedSource = first;
		changedSource.source += "// changed source\n";
		Expect(firstKey !=
		           ShaderCompileCache::ComputeKey(changedSource, compilerVersion, ShaderTarget::VulkanSpirv, keywords),
		       "Changing shader source must change the cache key");
		auto changedEntry = first;
		changedEntry.entryPoint.name = "OtherVertex";
		Expect(firstKey !=
		           ShaderCompileCache::ComputeKey(changedEntry, compilerVersion, ShaderTarget::VulkanSpirv, keywords),
		       "Changing the entry point must change the cache key");
		auto compiled = ShaderCompileCache::CompileCached(filesystem, cacheRoot, compiler, compilerVersion, first,
		                                                  ShaderTarget::VulkanSpirv, keywords);
		if (!compiled)
		{
			std::cerr << compiled.error.c_str() << '\n';
		}
		Expect(static_cast<bool>(compiled), "First shader variant did not compile and cache");
		Expect(filesystem.List("user").value.size() == 3, "Cache should contain bytecode, reflection, and metadata");

		DxcShaderCompiler unavailableCompiler({});
		auto cached = ShaderCompileCache::CompileCached(filesystem, cacheRoot, unavailableCompiler, compilerVersion,
		                                                first, ShaderTarget::VulkanSpirv, {"ALPHA_TEST", "FOG_ON"});
		Expect(static_cast<bool>(cached) && cached.value.spirv == compiled.value.spirv,
		       "Equivalent keyword ordering should reuse the cached variant without invoking DXC");

		auto changedInclude = MakeRequest("float4 MakePosition(float3 value) { return float4(value, 0.5); }\n");
		const String includeKey =
		    ShaderCompileCache::ComputeKey(changedInclude, compilerVersion, ShaderTarget::VulkanSpirv, keywords);
		Expect(firstKey != includeKey, "Changing an included file must invalidate its dependent variant");
		auto changedCompiled = ShaderCompileCache::CompileCached(filesystem, cacheRoot, compiler, compilerVersion,
		                                                         changedInclude, ShaderTarget::VulkanSpirv, keywords);
		Expect(static_cast<bool>(changedCompiled) && filesystem.List("user").value.size() == 6,
		       "Changed include should generate a separate cache entry");

		const Vector<String> changedKeywords{"FOG_OFF"};
		const String keywordKey =
		    ShaderCompileCache::ComputeKey(first, compilerVersion, ShaderTarget::VulkanSpirv, changedKeywords);
		Expect(firstKey != keywordKey, "Changing shader keywords must invalidate only that variant key");
		auto keywordCompiled = ShaderCompileCache::CompileCached(filesystem, cacheRoot, compiler, compilerVersion,
		                                                         first, ShaderTarget::VulkanSpirv, changedKeywords);
		Expect(static_cast<bool>(keywordCompiled) && filesystem.List("user").value.size() == 9,
		       "A distinct keyword variant should receive its own cache entry");

		std::cout << "Shader compile cache tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
