#include <Shader/ShaderSource.hpp>
#include <Core/StringInterop.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}
} // namespace

int main()
{
	try
	{
		using namespace Ursa;
		using namespace Ursa::Shader;
		const String source = R"shader(
Shader "Ursa/Test"
{
	ID "c6f1e600-b5c6-4c56-a5e1-5bba91570113"
	Version 1
	Properties
	{
		_BaseColor ("Base Color", Color) = (1, 1, 1, 1)
		_Metallic ("Metallic", Range(0, 1)) = 0.4
		_Count ("Count", Int) = 4
		_Parameters ("Parameters", Vector) = (1, 2, 3, 4)
		[HideInInspector] _Hidden ("Hidden", Float) = 0.25
		[NoScaleOffset] _MainTex ("Base Map", 2D) = "white" {}
	}
	HLSLINCLUDE
float4 SharedColor;
ENDHLSL
	SubShader
	{
		Tags { "RenderPipeline" = "UrsaForward" }
		Pass
		{
			Name "Forward"
			Tags { "LightMode" = "Forward" }
			Cull Back
			ZWrite On
			HLSLPROGRAM
#pragma vertex VSMain
#pragma fragment PSMain
ENDHLSL
		}
		Pass
		{
			Name "DepthOnly"
			Tags { "LightMode" = "DepthOnly" }
			HLSLPROGRAM
#pragma vertex VSDepth
#pragma fragment PSDepth
ENDHLSL
		}
	}
	Fallback "Ursa/Error"
}
)shader";
		auto parsed = ParseShaderSource("prj:Shaders/Test.ursashader", source);
		if (!parsed)
		{
			std::cerr << Ursa::ToStdString(parsed.error) << '\n';
			return 1;
		}
		Expect(static_cast<bool>(parsed), "Valid shader source did not parse");
		Expect(parsed.value.name == "Ursa/Test", "Shader name was not preserved");
		Expect(parsed.value.properties.size() == 6, "Shader properties were not preserved");
		Expect(parsed.value.properties[0].type == PropertyType::Color &&
		           parsed.value.properties[0].defaultVector[0] == 1.0f,
		       "Color property metadata was not parsed");
		Expect(parsed.value.properties[1].type == PropertyType::Range &&
		           parsed.value.properties[1].rangeMaximum == 1.0f && parsed.value.properties[1].defaultFloat == 0.4f,
		       "Range property metadata was not parsed");
		Expect(parsed.value.properties[2].type == PropertyType::Int && parsed.value.properties[2].defaultInt == 4,
		       "Integer property metadata was not parsed");
		Expect(parsed.value.properties[3].type == PropertyType::Vector &&
		           parsed.value.properties[3].defaultVector[3] == 4.0f,
		       "Vector property metadata was not parsed");
		Expect(parsed.value.properties[4].attributes[0] == "HideInInspector" &&
		           parsed.value.properties[5].defaultTexture == "white",
		       "Property attributes or texture default were not parsed");
		Expect(parsed.value.subShaders.size() == 1, "SubShader was not preserved");
		Expect(parsed.value.subShaders[0].passes.size() == 2, "Pass order/count was not preserved");
		Expect(parsed.value.subShaders[0].passes[0].name == "Forward", "First Pass name was not preserved");
		Expect(parsed.value.subShaders[0].passes[1].name == "DepthOnly", "Second Pass name was not preserved");
		Expect(parsed.value.subShaders[0].passes[0].programs.size() == 1, "HLSL program was not preserved");
		Expect(parsed.value.subShaders[0].passes[0].programs[0].entryPoints.size() == 2 &&
		           parsed.value.subShaders[0].passes[0].programs[0].entryPoints[0].stage == ProgramStage::Vertex &&
		           parsed.value.subShaders[0].passes[0].programs[0].entryPoints[1].stage == ProgramStage::Fragment,
		       "HLSL stage entry points were not parsed");
		Expect(parsed.value.subShaders[0].passes[0].programs[0].source.find("VSMain") != String::npos,
		       "HLSL source block was not preserved");
		Expect(parsed.value.includes.size() == 1, "Shared HLSL include block was not preserved");
		Expect(parsed.value.fallback == "Ursa/Error", "Fallback shader was not preserved");

		auto malformed = ParseShaderSource("prj:Broken.ursashader", "Shader \"Broken\" { SubShader { Pass {\n");
		Expect(!malformed, "Unclosed shader blocks must fail parsing");
		Expect(malformed.error.find("prj:Broken.ursashader:") == 0, "Diagnostic must include the virtual source path");
		Expect(malformed.error.find(":2:") != String::npos, "Diagnostic must include the source line");

		auto invalidRange = ParseShaderSource(
		    "prj:Range.ursashader", "Shader \"Range\" { Version 1 Properties { _Value(\"Value\", Range(1, 0)) = 0.5 } "
		                            "SubShader { Pass {} } }");
		Expect(!invalidRange && invalidRange.error.find("Range bounds") != String::npos,
		       "Invalid property ranges must fail with a diagnostic");

		std::cout << "Shader source parser tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
