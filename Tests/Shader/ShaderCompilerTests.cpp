#include <Shader/ShaderCompiler.hpp>

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

ShaderCompileRequest Request(ProgramStage stage, const char* entryPoint, const char* source)
{
	ShaderCompileRequest request;
	request.sourcePath = VirtualPath::Parse("prj:Shaders/CompilerTest.ursashader").value;
	request.entryPoint = {stage, entryPoint, {12, 1}};
	request.source = source;
	return request;
}
} // namespace

int main()
{
	try
	{
		DxcShaderCompiler compiler({std::filesystem::u8path(URSA_TEST_DXC_EXECUTABLE),
		                            std::filesystem::u8path(URSA_TEST_SPIRV_VAL_EXECUTABLE),
		                            std::filesystem::u8path(URSA_TEST_SPIRV_CROSS_EXECUTABLE)});
		auto version = compiler.Version();
		Expect(static_cast<bool>(version) && version.value.find("dxcompiler") != String::npos,
		       "DXC version was not reported");

		auto vertex = Request(ProgramStage::Vertex, "VSMain",
		                      "float4 VSMain(float3 position : POSITION) : SV_Position "
		                      "{ return float4(position, 1.0); }\n");
		vertex.includes.push_back({VirtualPath::Parse("prj:Shaders/Shared.hlsli").value,
		                           "float4 MakePosition(float3 value) { return float4(value, 1.0); }\n"});
		vertex.source = "#include \"Shared.hlsli\"\nfloat4 VSMain(float3 position : POSITION) : SV_Position "
		                "{ return MakePosition(position); }\n";
		auto compiledVertex = compiler.Compile(vertex);
		Expect(static_cast<bool>(compiledVertex), "DXC did not compile a vertex shader");
		Expect(compiledVertex.value.spirv.size() >= sizeof(UInt32) &&
		           compiledVertex.value.reflectionJson.find("VSMain") != String::npos,
		       "Vertex SPIR-V or reflection output is missing");

		auto keywordVariant = Request(ProgramStage::Vertex, "VSMain",
		                              "#pragma shader_feature_local _ URSA_TEST_OFFSET\n"
		                              "float4 VSMain(float3 position : POSITION) : SV_Position\n"
		                              "{\n"
		                              "#ifdef URSA_TEST_OFFSET\n"
		                              "    position.x += 0.25;\n"
		                              "#endif\n"
		                              "    return float4(position, 1.0);\n"
		                              "}\n");
		auto defaultVariant = compiler.Compile(keywordVariant);
		keywordVariant.keywords = {"URSA_TEST_OFFSET"};
		auto enabledVariant = compiler.Compile(keywordVariant);
		Expect(defaultVariant && enabledVariant && defaultVariant.value.spirv != enabledVariant.value.spirv,
		       "Selected keyword did not define the corresponding HLSL compilation macro");

		auto fragment = Request(ProgramStage::Fragment, "PSMain",
		                        "float4 PSMain() : SV_Target0 { return float4(1.0, 0.0, 0.0, 1.0); }\n");
		auto compiledFragment = compiler.Compile(fragment);
		Expect(static_cast<bool>(compiledFragment) && compiledFragment.value.stage == ProgramStage::Fragment,
		       "DXC did not compile a fragment shader");

		auto compute = Request(ProgramStage::Compute, "CSMain",
		                       "RWStructuredBuffer<uint> values : register(u0);\n[numthreads(1, 1, 1)]\n"
		                       "void CSMain(uint3 id : SV_DispatchThreadID) { values[id.x] = id.x; }\n");
		auto compiledCompute = compiler.Compile(compute);
		Expect(static_cast<bool>(compiledCompute) && compiledCompute.value.stage == ProgramStage::Compute,
		       "DXC did not compile a compute shader");
		Expect(compiledCompute.value.reflectionJson.find("ssbos") != String::npos,
		       "Compute resource reflection was not emitted");

		auto invalid = Request(ProgramStage::Vertex, "Broken", "float4 Broken( : SV_Position { return 0; }\n");
		auto failed = compiler.Compile(invalid);
		Expect(!failed && failed.error.find("prj:Shaders/CompilerTest.ursashader:12") != String::npos,
		       "HLSL compiler diagnostics did not include the shader path and source line");

		std::cout << "Shader compiler tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
