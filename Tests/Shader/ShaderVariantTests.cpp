#include <Shader/ShaderSource.hpp>

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

const char* shaderSource = R"(
Shader "Tests/Variants"
{
	ID "830a0e0d-bab6-4c21-aeea-e21de227a901"
	Version 1
	SubShader
	{
		Pass
		{
			HLSLPROGRAM
#pragma vertex VSMain
#pragma fragment PSMain
#pragma shader_feature_local _ URSA_NORMALMAP
#pragma multi_compile URSA_FOG_OFF URSA_FOG_ON
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target0 { return 1.0; }
			ENDHLSL
		}
	}
}
)";
} // namespace

int main()
{
	try
	{
		auto parsed = ParseShaderSource("prj:Shaders/Variants.ursashader", shaderSource);
		Expect(static_cast<bool>(parsed), "Shader keyword directives failed to parse");
		const auto& program = parsed.value.subShaders[0].passes[0].programs[0];
		Expect(program.keywordDirectives.size() == 2, "Expected local feature and required multi_compile directives");
		Expect(program.keywordDirectives[0].kind == ShaderKeywordDirectiveKind::ShaderFeature &&
		           program.keywordDirectives[0].options.size() == 2 && program.keywordDirectives[0].options[0].empty(),
		       "shader_feature_local did not include its implicit no-keyword option");
		Expect(program.keywordDirectives[1].kind == ShaderKeywordDirectiveKind::MultiCompile,
		       "multi_compile was incorrectly treated as optional");

		Vector<Vector<String>> usedSets{{"URSA_NORMALMAP"}};
		auto variants = ExpandShaderKeywordVariants(program.keywordDirectives, usedSets, 4);
		Expect(static_cast<bool>(variants) && variants.value.size() == 4,
		       "Expected both required multi_compile variants and used shader_feature variants");
		UInt32 requiredCount = 0;
		UInt32 optionalCount = 0;
		for (const ShaderKeywordVariant& variant : variants.value)
		{
			requiredCount += variant.required ? 1u : 0u;
			optionalCount += variant.required ? 0u : 1u;
		}
		Expect(requiredCount == 2 && optionalCount == 2,
		       "multi_compile variants were not required or unused shader_feature variants were not stripped");
		auto overLimit = ExpandShaderKeywordVariants(program.keywordDirectives, usedSets, 3);
		Expect(!overLimit && overLimit.error.find("variant limit") != String::npos,
		       "Potential shader variant count did not enforce its configured upper bound");
		auto unknown = ExpandShaderKeywordVariants(program.keywordDirectives, {{"UNKNOWN_KEYWORD"}}, 4);
		Expect(!unknown && unknown.error.find("undeclared") != String::npos,
		       "Undeclared material keyword was not diagnosed during variant planning");
		Vector<ShaderKeywordDirective> mutuallyExclusive{
		    {ShaderKeywordDirectiveKind::ShaderFeature, {String{}, "FEATURE_A", "FEATURE_B"}, {}}};
		auto conflict = ExpandShaderKeywordVariants(mutuallyExclusive, {{"FEATURE_A", "FEATURE_B"}}, 8);
		Expect(!conflict && conflict.error.find("mutually exclusive") != String::npos,
		       "Conflicting feature choices were not rejected");
		auto noDirectives = ExpandShaderKeywordVariants({}, {}, 1);
		Expect(noDirectives && noDirectives.value.size() == 1 && noDirectives.value[0].required &&
		           noDirectives.value[0].keywords.empty(),
		       "A shader without keyword pragmas did not produce its single required default variant");
		std::cout << "Shader variant tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
