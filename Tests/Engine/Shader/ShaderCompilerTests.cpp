#include <Shader/ShaderCompiler.hpp>
#include <Shader/ShaderReflection.hpp>
#include <nlohmann/json.hpp>

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

		const char* automaticSource = R"hlsl(
cbuffer A { float4 dataA; };
cbuffer B { float4 dataB; };
Texture2D<float4> textureA;
Texture2D<float4> textureB;
Texture2D<float4> explicitTexture : register(t7);
SamplerState samplerA;
SamplerState samplerB;
SamplerState explicitSampler : register(s7);
float4 VSMain(float3 position : POSITION) : SV_Position
{
    return float4(position, 1) + dataB + textureB.SampleLevel(samplerB, 0, 0);
}
float4 PSMain() : SV_Target0
{
    return textureA.Sample(samplerA, 0) + textureB.Sample(samplerB, 0) + dataA + dataB
        + explicitTexture.Sample(explicitSampler, 0);
}
)hlsl";
		auto automaticVertex = compiler.Compile(Request(ProgramStage::Vertex, "VSMain", automaticSource));
		auto automaticFragment = compiler.Compile(Request(ProgramStage::Fragment, "PSMain", automaticSource));
		Expect(automaticVertex && automaticFragment, "Automatic and explicit resource bindings did not compile");
		ShaderVariantAsset automaticVariant;
		automaticVariant.stages = {{ShaderTarget::VulkanSpirv, ProgramStage::Vertex, "VSMain",
		                            automaticVertex.value.spirv, automaticVertex.value.reflectionJson},
		                           {ShaderTarget::VulkanSpirv, ProgramStage::Fragment, "PSMain",
		                            automaticFragment.value.spirv, automaticFragment.value.reflectionJson}};
		ShaderAsset automaticShader;
		automaticShader.name = "AutomaticBindings";
		auto automaticLayout = BuildShaderBindingLayout(automaticShader, automaticVariant);
		Expect(bool(automaticLayout), "Stages disagreed on automatically assigned shared bindings");
		const auto pixelReflection = nlohmann::json::parse(automaticFragment.value.reflectionJson.c_str());
		for (const auto& image : pixelReflection["separate_images"])
		{
			Expect(image["binding"].get<UInt32>() >= 16 && image["binding"].get<UInt32>() < 32,
			       "Automatic texture bindings were not shifted out of the uniform range");
			if (image["name"] == "explicitTexture")
			{
				Expect(image["binding"] == 23, "Explicit t7 binding was not preserved");
			}
		}
		for (const auto& sampler : pixelReflection["separate_samplers"])
		{
			Expect(sampler["binding"].get<UInt32>() >= 32 && sampler["binding"].get<UInt32>() < 48,
			       "Automatic sampler bindings collide with textures");
			if (sampler["name"] == "explicitSampler")
			{
				Expect(sampler["binding"] == 39, "Explicit s7 binding was not preserved");
			}
		}

		auto compute = Request(ProgramStage::Compute, "CSMain",
		                       "RWStructuredBuffer<uint> values : register(u0);\n[numthreads(1, 1, 1)]\n"
		                       "void CSMain(uint3 id : SV_DispatchThreadID) { values[id.x] = id.x; }\n");
		auto compiledCompute = compiler.Compile(compute);
		Expect(static_cast<bool>(compiledCompute) && compiledCompute.value.stage == ProgramStage::Compute,
		       "DXC did not compile a compute shader");
		Expect(compiledCompute.value.reflectionJson.find("ssbos") != String::npos,
		       "Compute resource reflection was not emitted");
		compute.source = "RWStructuredBuffer<uint> values; [numthreads(1, 1, 1)] "
		                 "void CSMain(uint3 id : SV_DispatchThreadID) { values[id.x] = id.x; }";
		auto automaticCompute = compiler.Compile(compute);
		Expect(automaticCompute &&
		           nlohmann::json::parse(automaticCompute.value.reflectionJson.c_str())["ssbos"][0]["binding"] == 48,
		       "Automatic UAV did not use the writable resource range");

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
