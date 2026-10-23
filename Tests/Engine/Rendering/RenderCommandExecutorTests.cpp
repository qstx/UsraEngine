#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Rendering/Shader/ComputeKernel.hpp>
#include <ComputeFixtures.hpp>
#include <MeshFixtures.hpp>
#include <Shader/ShaderCompiler.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Rendering/Shader/ShaderPassSelection.hpp>
#include <Rendering/Scene/RendererList.hpp>
#include <Rendering/Scene/DirectionalShadowCascades.hpp>
#include <Rendering/Diagnostics/FrameImage.hpp>
#include <Scene/Scene.hpp>
#include <Animation/AnimatorComponent.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <nlohmann/json.hpp>
#include <Core/StringInterop.hpp>
#include <Vfs/AssetRoots.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Ursa;
using namespace Ursa::Rendering;

namespace
{
template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void Done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

void Expect(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

std::shared_ptr<MaterialInstance> CompositionMaterial(bool multipleAttachments = false)
{
	Shader::DxcShaderCompiler compiler({URSA_TEST_DXC_EXECUTABLE, URSA_TEST_SPIRV_VAL_EXECUTABLE,
	                                    URSA_TEST_SPIRV_CROSS_EXECUTABLE, URSA_SHADER_STAGING_ROOT});
	String source = R"(
Texture2D<float4> _Source : register(t0);
SamplerState sourceSampler : register(s0);
cbuffer Parameters : register(b0) { float4 _Tint; float _Intensity; int _Mode; float2 padding; row_major float4x4 _Transform; };
struct Varyings { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varyings VSMain(uint id : SV_VertexID) {
    Varyings result;
    float2 uv = float2((id << 1) & 2, id & 2);
    result.position = mul(_Transform, float4(uv * 2 - 1, 0, 1));
    result.uv = uv;
    return result;
}
float4 PSMain(Varyings input) : SV_Target { return _Source.Sample(sourceSampler, input.uv) * _Tint * _Intensity * (_Mode == 3 ? 1 : 0); }
)";
	if (multipleAttachments)
	{
		const auto fragment = source.find("float4 PSMain");
		source.erase(fragment);
		source += R"(
struct Targets { float4 first : SV_Target0; float4 second : SV_Target1; };
Targets PSMain(Varyings input) {
    Targets output;
    output.first = _Source.Sample(sourceSampler, input.uv) * _Tint * _Intensity * (_Mode == 3 ? 1 : 0);
    output.second = float4(0, 0, 1, 1);
    return output;
}
)";
	}
	auto shader = std::make_shared<Shader::ShaderAsset>();
	shader->id = ResourceId::Generate();
	shader->name = "Tests/Composition";
	shader->sourceHash = "composition-v1";
	Shader::PropertyDeclaration tint;
	tint.identifier = "_Tint";
	tint.type = Shader::PropertyType::Vector;
	tint.defaultVector = {1, 1, 1, 1};
	shader->properties.push_back(tint);
	Shader::ShaderPassAsset pass;
	pass.name = "Composition";
	pass.identity = "composition-pass-v1";
	Shader::ShaderVariantAsset variant;
	variant.layoutIdentity = "composition-layout-v1";
	for (const auto stage : {Shader::ProgramStage::Vertex, Shader::ProgramStage::Fragment})
	{
		Shader::ShaderCompileRequest request;
		request.sourcePath = Take(VirtualPath::Parse("prj:Tests/Composition.ursashader"));
		request.entryPoint = {stage, stage == Shader::ProgramStage::Vertex ? "VSMain" : "PSMain", {}};
		request.source = source;
		auto compiled = Take(compiler.Compile(request));
		variant.stages.push_back(
		    {Shader::ShaderTarget::VulkanSpirv, stage, compiled.entryPoint, compiled.spirv, compiled.reflectionJson});
	}
	pass.variants.push_back(std::move(variant));
	Shader::ShaderSubShaderAsset subShader;
	subShader.passes.push_back(std::move(pass));
	shader->subShaders.push_back(std::move(subShader));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetShaderAsset(shader));
	return material;
}

std::shared_ptr<MaterialInstance> ShadowProbeMaterial()
{
	Shader::DxcShaderCompiler compiler({URSA_TEST_DXC_EXECUTABLE, URSA_TEST_SPIRV_VAL_EXECUTABLE,
	                                    URSA_TEST_SPIRV_CROSS_EXECUTABLE, URSA_SHADER_STAGING_ROOT});
	const Vector<String> libraryFiles = {"Lighting.hlsl",     "InputData.hlsl",       "Input.hlsl",
	                                     "Core.hlsl",         "Color.hlsl",           "BRDF.hlsl",
	                                     "Skinning.hlsl",     "Shadows.hlsl",         "RealtimeLights.hlsl",
	                                     "Math.hlsl",         "SpaceTransforms.hlsl", "SurfaceData.hlsl",
	                                     "SurfaceInput.hlsl", "Textures.hlsl"};
	Vector<Shader::ShaderIncludeSource> includes;
	for (const String& file : libraryFiles)
	{
		const auto includePath = Take(VirtualPath::Parse("prj:Tests/ShadowProbe/ShaderLibrary/" + file));
		std::ifstream input(std::filesystem::path(URSA_SHADER_LIBRARY_ROOT) / ToStdString(file), std::ios::binary);
		if (!input)
		{
			throw std::runtime_error("Cannot open ShaderLibrary source " + ToStdString(file));
		}
		String includeSource;
		std::string line;
		while (std::getline(input, line))
		{
			includeSource += ToUrsaString(line);
			includeSource += '\n';
		}
		includes.push_back({includePath, std::move(includeSource)});
	}
	const String source = R"(
#include "ShaderLibrary/Core.hlsl"
#include "ShaderLibrary/Lighting.hlsl"
cbuffer ShadowProbeParameters { int _ShadowProbeMode; };
struct Varyings { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varyings VSMain(uint id : SV_VertexID) {
    Varyings output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    output.uv = uv;
    return output;
}
float4 PSMain(Varyings input) : SV_Target0 {
    if (_ShadowProbeMode == 1) {
        float depth = UrsaReadShadowDepth(input.uv);
        return float4(depth, depth, depth, 1.0);
    }
    float visibility = UrsaCompareMainLightShadow(float3(0, 0, 0), float3(0, 1, 0), float3(0, 1, 0));
    return visibility.xxxx;
}
)";
	auto shader = std::make_shared<Shader::ShaderAsset>();
	shader->id = ResourceId::Generate();
	shader->name = "Tests/ShadowProbe";
	shader->sourceHash = "shadow-probe-v1";
	Shader::ShaderPassAsset pass;
	pass.name = "ShadowProbe";
	pass.identity = "shadow-probe-pass-v1";
	Shader::ShaderVariantAsset variant;
	variant.layoutIdentity = "shadow-probe-layout-v1";
	for (const auto stage : {Shader::ProgramStage::Vertex, Shader::ProgramStage::Fragment})
	{
		Shader::ShaderCompileRequest request;
		request.sourcePath = Take(VirtualPath::Parse("prj:Tests/ShadowProbe/ShadowProbe.ursashader"));
		request.entryPoint = {stage, stage == Shader::ProgramStage::Vertex ? "VSMain" : "PSMain", {}};
		request.source = source;
		request.includes = includes;
		auto compiled = Take(compiler.Compile(request));
		variant.stages.push_back({Shader::ShaderTarget::VulkanSpirv, stage, request.entryPoint.name,
		                          std::move(compiled.spirv), std::move(compiled.reflectionJson)});
	}
	pass.variants.push_back(std::move(variant));
	Shader::ShaderSubShaderAsset subShader;
	subShader.passes.push_back(std::move(pass));
	shader->subShaders.push_back(std::move(subShader));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetShaderAsset(shader));
	Done(material->SetSampler({false, false}));
	return material;
}

Array<UInt8, 4> ReadPixel(const Rhi::DevicePtr& device, const std::shared_ptr<RenderTextureResource>& texture)
{
	auto readback = Take(device->CreateBuffer({Size(texture->color->desc.width) * texture->color->desc.height * 4,
	                                           Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(texture->color, texture->colorState, Rhi::ResourceState::TransferSource));
	Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(texture->color, readback));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	texture->colorState = Rhi::ResourceState::TransferSource;
	Array<UInt8, 4> pixel{};
	Done(readback->Read(0, pixel.data(), pixel.size()));
	return pixel;
}

Float32 HalfToFloat(UInt16 half)
{
	const UInt32 sign = UInt32(half & 0x8000) << 16;
	UInt32 exponent = (half >> 10) & 0x1f;
	UInt32 mantissa = half & 0x3ff;
	UInt32 bits;
	if (exponent == 0)
	{
		if (mantissa == 0)
		{
			bits = sign;
		}
		else
		{
			Int32 adjustedExponent = -14;
			while ((mantissa & 0x400) == 0)
			{
				mantissa <<= 1;
				--adjustedExponent;
			}
			mantissa &= 0x3ff;
			bits = sign | (UInt32(adjustedExponent + 127) << 23) | (mantissa << 13);
		}
	}
	else if (exponent == 0x1f)
	{
		bits = sign | 0x7f800000 | (mantissa << 13);
	}
	else
	{
		bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
	}
	Float32 value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

RenderCommand Parameter(const String& name, UInt32 kind, const void* data, Size size);
RenderCommand Tint();

void TestBloomAndFinalPost(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	auto engineFiles = Take(OpenEngineFileSystem());
	const auto loadBuiltin = [&](ResourceId id)
	{
		const char* assetPath = Shader::BuiltinShaderAssetPath(id);
		Expect(assetPath != nullptr, "Built-in postprocess Shader path was not registered");
		auto asset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(assetPath))));
		return std::make_shared<Shader::ShaderAsset>(std::move(asset));
	};
	auto bloomMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	auto finalMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(bloomMaterial->SetShaderAsset(loadBuiltin(Shader::BuiltinBloomShaderId())));
	Done(finalMaterial->SetShaderAsset(loadBuiltin(Shader::BuiltinFinalPostShaderId())));
	bloomMaterial->SetSampler({true, false});
	finalMaterial->SetSampler({true, false});

	const auto render = [&](UInt32 width, UInt32 height, Color sourceColor, Float32 exposure, Float32 threshold,
	                        Float32 bloomIntensity, Float32 scatter)
	{
		auto source = Take(CreateRenderTexture(*device, {width, height, 1}));
		auto output = Take(CreateRenderTexture(*device, {width, height}));
		Vector<std::shared_ptr<RenderTextureResource>> pyramid;
		Vector<std::pair<UInt32, UInt32>> sizes;
		UInt32 mipWidth = std::max(1u, width / 2), mipHeight = std::max(1u, height / 2);
		for (UInt32 level = 0; level < 6; ++level)
		{
			pyramid.push_back(Take(CreateRenderTexture(*device, {mipWidth, mipHeight, 1})));
			sizes.emplace_back(mipWidth, mipHeight);
			if (mipWidth == 1 && mipHeight == 1)
			{
				break;
			}
			mipWidth = std::max(1u, mipWidth / 2);
			mipHeight = std::max(1u, mipHeight / 2);
		}
		Vector<std::shared_ptr<RenderTextureResource>> upsampled;
		for (Size index = 1; index < pyramid.size(); ++index)
		{
			upsampled.push_back(
			    Take(CreateRenderTexture(*device, {sizes[index - 1].first, sizes[index - 1].second, 1})));
		}

		Vector<RenderCommand> commands;
		RenderCommand sourceTarget;
		sourceTarget.opcode = RenderCommandOpcode::SetRenderTarget;
		sourceTarget.target.texture = source;
		commands.push_back(sourceTarget);
		RenderCommand clearSource;
		clearSource.opcode = RenderCommandOpcode::ClearTarget;
		clearSource.clearFlags = 1;
		clearSource.color = sourceColor;
		commands.push_back(clearSource);
		commands.push_back(Parameter("_UrsaBloomThreshold", 1, &threshold, sizeof(threshold)));
		commands.push_back(Parameter("_UrsaBloomExposureEV", 1, &exposure, sizeof(exposure)));
		const Array<Float32, 4> fullRect{0, 0, 1, 1};
		commands.push_back(Parameter("_UrsaBloomSourceRect", 2, fullRect.data(), sizeof(fullRect)));
		RenderCommand blit;
		blit.opcode = RenderCommandOpcode::Blit;
		blit.material = bloomMaterial;
		blit.source.texture = source;
		blit.target.texture = pyramid[0];
		blit.pass = 0;
		commands.push_back(blit);
		for (Size level = 1; level < pyramid.size(); ++level)
		{
			const Array<Float32, 4> texelSize{1.0f / sizes[level - 1].first, 1.0f / sizes[level - 1].second,
			                                  Float32(sizes[level - 1].first), Float32(sizes[level - 1].second)};
			commands.push_back(Parameter("_UrsaBloomTexelSize", 2, texelSize.data(), sizeof(texelSize)));
			blit.source.texture = pyramid[level - 1];
			blit.target.texture = pyramid[level];
			blit.pass = 1;
			commands.push_back(blit);
		}
		RenderTargetReference bloomResult{pyramid.back(), 0};
		for (Size level = pyramid.size() - 1; level > 0; --level)
		{
			RenderCommand lowMip;
			lowMip.opcode = RenderCommandOpcode::SetGlobalParameters;
			lowMip.global.name = "_UrsaBloomLowMip";
			lowMip.global.kind = 3;
			lowMip.global.texture = bloomResult;
			commands.push_back(lowMip);
			commands.push_back(Parameter("_UrsaBloomScatter", 1, &scatter, sizeof(scatter)));
			blit.source.texture = pyramid[level - 1];
			blit.target.texture = upsampled[level - 1];
			blit.pass = 2;
			commands.push_back(blit);
			bloomResult.texture = upsampled[level - 1];
		}
		RenderCommand bloomTexture;
		bloomTexture.opcode = RenderCommandOpcode::SetGlobalParameters;
		bloomTexture.global.name = "_UrsaBloomTexture";
		bloomTexture.global.kind = 3;
		bloomTexture.global.texture = bloomResult;
		commands.push_back(bloomTexture);
		commands.push_back(Parameter("_UrsaExposureEV", 1, &exposure, sizeof(exposure)));
		const Int32 toneMapping = 1, fxaaEnabled = 0;
		commands.push_back(Parameter("_UrsaToneMappingMode", 6, &toneMapping, sizeof(toneMapping)));
		commands.push_back(Parameter("_UrsaFxaaEnabled", 6, &fxaaEnabled, sizeof(fxaaEnabled)));
		commands.push_back(Parameter("_UrsaBloomIntensity", 1, &bloomIntensity, sizeof(bloomIntensity)));
		const Array<Float32, 4> rect{0, 0, 1, 1};
		const Array<Float32, 4> sourceSize{1.0f / width, 1.0f / height, Float32(width), Float32(height)};
		commands.push_back(Parameter("_UrsaFinalSourceRect", 2, rect.data(), sizeof(rect)));
		commands.push_back(Parameter("_UrsaFinalSourceSize", 2, sourceSize.data(), sizeof(sourceSize)));
		blit.material = finalMaterial;
		blit.source.texture = source;
		blit.target = {};
		blit.pass = 0;
		commands.push_back(blit);

		Done(executor.Begin(output));
		Done(executor.Execute(commands));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		return ReadPixel(device, output);
	};

	const Color hdr{4, 2, 0.5f, 1};
	const auto noBloom = render(127, 95, hdr, 0, 10, 0.5f, 0.7f);
	const auto disabledBloom = render(127, 95, hdr, 0, 1, 0, 0.7f);
	Expect(noBloom == disabledBloom, "Zero Bloom intensity changed the final output");
	const auto withBloom = render(127, 95, hdr, 0, 1, 0.5f, 0.7f);
	Expect(withBloom[0] > noBloom[0] && withBloom[1] > noBloom[1] && withBloom[2] > noBloom[2],
	       "Multi-level Bloom did not add the extracted HDR highlights");
	const auto lowScatter = render(127, 95, hdr, 0, 1, 0.5f, 0);
	const auto highScatter = render(127, 95, hdr, 0, 1, 0.5f, 1);
	Expect(highScatter[0] > lowScatter[0] && highScatter[1] > lowScatter[1],
	       "Bloom scatter did not change the multi-level upsample contribution");
	const auto exposed = render(127, 95, hdr, 1, 10, 0, 0.7f);
	Expect(exposed[0] > noBloom[0], "One exposure EV did not increase a known HDR value");
	const auto onePixel = render(1, 1, hdr, 0, 1, 0.5f, 0.7f);
	Expect(onePixel[0] > 0, "Bloom failed for a one-pixel target");

	auto edgeSource = Take(CreateRenderTexture(*device, {16, 16, 1}));
	auto edgeOutput = Take(CreateRenderTexture(*device, {16, 16}));
	Vector<RenderCommand> edgeCommands;
	RenderCommand sourceTarget;
	sourceTarget.opcode = RenderCommandOpcode::SetRenderTarget;
	sourceTarget.target.texture = edgeSource;
	edgeCommands.push_back(sourceTarget);
	RenderCommand edgeViewport;
	edgeViewport.opcode = RenderCommandOpcode::SetViewport;
	edgeViewport.viewport = {0, 0, 16, 16, 0, 1};
	edgeCommands.push_back(edgeViewport);
	RenderCommand edgeScissor;
	edgeScissor.opcode = RenderCommandOpcode::SetScissor;
	edgeScissor.scissor = {0, 0, 16, 16};
	edgeCommands.push_back(edgeScissor);
	RenderCommand edgeClear;
	edgeClear.opcode = RenderCommandOpcode::ClearTarget;
	edgeClear.clearFlags = 1;
	edgeClear.color = {0, 0, 0, 1};
	edgeCommands.push_back(edgeClear);
	edgeClear.color = {1, 1, 1, 1};
	for (Int32 y = 0; y < 16; ++y)
	{
		const Int32 firstWhiteX = 3 + y / 2;
		edgeViewport.viewport = {Float32(firstWhiteX), Float32(y), Float32(16 - firstWhiteX), 1, 0, 1};
		edgeCommands.push_back(edgeViewport);
		edgeScissor.scissor = {firstWhiteX, y, UInt32(16 - firstWhiteX), 1};
		edgeCommands.push_back(edgeScissor);
		edgeCommands.push_back(edgeClear);
	}
	edgeViewport.viewport = {0, 0, 16, 16, 0, 1};
	edgeCommands.push_back(edgeViewport);
	edgeScissor.scissor = {0, 0, 16, 16};
	edgeCommands.push_back(edgeScissor);
	const Int32 noToneMapping = 0, fxaaEnabled = 1;
	const Float32 zero = 0;
	const Array<Float32, 4> fullRect{0, 0, 1, 1};
	const Array<Float32, 4> edgeSize{1.0f / 16, 1.0f / 16, 16, 16};
	edgeCommands.push_back(Parameter("_UrsaExposureEV", 1, &zero, sizeof(zero)));
	edgeCommands.push_back(Parameter("_UrsaToneMappingMode", 6, &noToneMapping, sizeof(noToneMapping)));
	edgeCommands.push_back(Parameter("_UrsaFxaaEnabled", 6, &fxaaEnabled, sizeof(fxaaEnabled)));
	edgeCommands.push_back(Parameter("_UrsaBloomIntensity", 1, &zero, sizeof(zero)));
	edgeCommands.push_back(Parameter("_UrsaFinalSourceRect", 2, fullRect.data(), sizeof(fullRect)));
	edgeCommands.push_back(Parameter("_UrsaFinalSourceSize", 2, edgeSize.data(), sizeof(edgeSize)));
	RenderCommand edgeBloom;
	edgeBloom.opcode = RenderCommandOpcode::SetGlobalParameters;
	edgeBloom.global.name = "_UrsaBloomTexture";
	edgeBloom.global.kind = 3;
	edgeBloom.global.texture.texture = edgeSource;
	edgeCommands.push_back(edgeBloom);
	RenderCommand edgeBlit;
	edgeBlit.opcode = RenderCommandOpcode::Blit;
	edgeBlit.source.texture = edgeSource;
	edgeBlit.target.texture = edgeOutput;
	edgeBlit.material = finalMaterial;
	edgeBlit.pass = 0;
	edgeCommands.push_back(edgeBlit);
	Done(executor.Begin(edgeOutput));
	Done(executor.Execute(edgeCommands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto edgePixels = Take(ReadRenderTexture(*device, *edgeOutput));
	bool foundTransition = false;
	for (Size pixel = 0; pixel < 16 * 16; ++pixel)
	{
		foundTransition = foundTransition || (edgePixels[pixel * 4] > 8 && edgePixels[pixel * 4] < 247);
	}
	const Size flatBlack = (0 * 16 + 0) * 4, flatWhite = (15 * 16 + 15) * 4;
	Expect(edgePixels[flatBlack] < 5 && edgePixels[flatWhite] > 250 && foundTransition,
	       "FXAA changed flat regions or failed to soften a known diagonal edge");

	auto viewportSource = Take(CreateRenderTexture(*device, {16, 16, 1}));
	auto viewportOutput = Take(CreateRenderTexture(*device, {16, 16}));
	auto isolatedOutput = Take(CreateRenderTexture(*device, {16, 16}));
	Vector<RenderCommand> viewportCommands;
	RenderCommand viewportTarget;
	viewportTarget.opcode = RenderCommandOpcode::SetRenderTarget;
	viewportTarget.target.texture = viewportSource;
	viewportCommands.push_back(viewportTarget);
	RenderCommand viewportCommand;
	viewportCommand.opcode = RenderCommandOpcode::SetViewport;
	viewportCommand.viewport = {0, 0, 16, 16, 0, 1};
	viewportCommands.push_back(viewportCommand);
	RenderCommand scissorCommand;
	scissorCommand.opcode = RenderCommandOpcode::SetScissor;
	scissorCommand.scissor = {0, 0, 16, 16};
	viewportCommands.push_back(scissorCommand);
	RenderCommand clearCommand;
	clearCommand.opcode = RenderCommandOpcode::ClearTarget;
	clearCommand.clearFlags = 1;
	clearCommand.color = {4, 0, 0, 1};
	viewportCommands.push_back(clearCommand);
	viewportCommand.viewport = {8, 0, 8, 16, 0, 1};
	viewportCommands.push_back(viewportCommand);
	scissorCommand.scissor = {8, 0, 8, 16};
	viewportCommands.push_back(scissorCommand);
	clearCommand.color = {0, 4, 0, 1};
	viewportCommands.push_back(clearCommand);

	RenderCommand viewportBloom;
	viewportBloom.opcode = RenderCommandOpcode::SetGlobalParameters;
	viewportBloom.global.name = "_UrsaBloomTexture";
	viewportBloom.global.kind = 3;
	viewportBloom.global.texture.texture = viewportSource;
	viewportCommands.push_back(viewportBloom);
	const Float32 viewportExposure = 0, viewportBloomIntensity = 0;
	const Int32 viewportReinhard = 1, viewportFxaa = 1, combinedOutput = 0, unormOutput = 0;
	const Array<Float32, 4> viewportSize{1.0f / 16, 1.0f / 16, 16, 16};
	viewportTarget.target.texture = viewportOutput;
	viewportCommands.push_back(viewportTarget);
	viewportCommand.viewport = {0, 0, 16, 16, 0, 1};
	viewportCommands.push_back(viewportCommand);
	scissorCommand.scissor = {0, 0, 16, 16};
	viewportCommands.push_back(scissorCommand);
	clearCommand.color = {0.2f, 0.3f, 0.4f, 1};
	viewportCommands.push_back(clearCommand);
	const auto appendViewportCamera = [&](Int32 x, Int32 width, const auto& outputTarget)
	{
		const Array<Float32, 4> rect{Float32(x) / 16, 0, Float32(width) / 16, 1};
		viewportCommand.viewport = {Float32(x), 0, Float32(width), 16, 0, 1};
		viewportCommands.push_back(viewportCommand);
		scissorCommand.scissor = {x, 0, UInt32(width), 16};
		viewportCommands.push_back(scissorCommand);
		viewportCommands.push_back(Parameter("_UrsaExposureEV", 1, &viewportExposure, sizeof(viewportExposure)));
		viewportCommands.push_back(Parameter("_UrsaToneMappingMode", 6, &viewportReinhard, sizeof(viewportReinhard)));
		viewportCommands.push_back(Parameter("_UrsaFxaaEnabled", 6, &viewportFxaa, sizeof(viewportFxaa)));
		viewportCommands.push_back(Parameter("_UrsaFinalPostOutputMode", 6, &combinedOutput, sizeof(combinedOutput)));
		viewportCommands.push_back(Parameter("_UrsaFinalOutputIsSrgb", 6, &unormOutput, sizeof(unormOutput)));
		viewportCommands.push_back(
		    Parameter("_UrsaBloomIntensity", 1, &viewportBloomIntensity, sizeof(viewportBloomIntensity)));
		viewportCommands.push_back(Parameter("_UrsaFinalSourceRect", 2, rect.data(), sizeof(rect)));
		viewportCommands.push_back(Parameter("_UrsaFinalSourceSize", 2, viewportSize.data(), sizeof(viewportSize)));
		RenderCommand cameraBlit;
		cameraBlit.opcode = RenderCommandOpcode::Blit;
		cameraBlit.source.texture = viewportSource;
		cameraBlit.target.texture = outputTarget;
		cameraBlit.material = finalMaterial;
		viewportCommands.push_back(cameraBlit);
	};
	appendViewportCamera(4, 4, viewportOutput);
	appendViewportCamera(8, 4, viewportOutput);
	viewportTarget.target.texture = isolatedOutput;
	viewportCommands.push_back(viewportTarget);
	viewportCommand.viewport = {0, 0, 16, 16, 0, 1};
	viewportCommands.push_back(viewportCommand);
	scissorCommand.scissor = {0, 0, 16, 16};
	viewportCommands.push_back(scissorCommand);
	clearCommand.color = {0.7f, 0.6f, 0.1f, 1};
	viewportCommands.push_back(clearCommand);
	appendViewportCamera(8, 4, isolatedOutput);
	Done(executor.Begin(viewportOutput));
	Done(executor.Execute(viewportCommands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto viewportPixels = Take(ReadRenderTexture(*device, *viewportOutput));
	const auto isolatedPixels = Take(ReadRenderTexture(*device, *isolatedOutput));
	const auto backingPixels = Take(ReadRenderTexture(*device, *viewportSource));
	const Size viewportOutside = (8 * 16 + 1) * 4, viewportLeft = (8 * 16 + 5) * 4, viewportLeftEdge = (8 * 16 + 7) * 4,
	           viewportRightEdge = (8 * 16 + 8) * 4, viewportRight = (8 * 16 + 10) * 4;
	Expect(std::abs(int(viewportPixels[viewportOutside]) - 51) <= 1 &&
	           std::abs(int(viewportPixels[viewportOutside + 1]) - 77) <= 1 &&
	           std::abs(int(viewportPixels[viewportOutside + 2]) - 102) <= 1 && viewportPixels[viewportLeft] > 225 &&
	           viewportPixels[viewportLeft + 1] < 8 && viewportPixels[viewportLeftEdge] > 225 &&
	           viewportPixels[viewportLeftEdge + 1] < 8 && viewportPixels[viewportRightEdge + 1] > 225 &&
	           viewportPixels[viewportRightEdge] < 8 && viewportPixels[viewportRight + 1] > 225 &&
	           viewportPixels[viewportRight] < 8,
	       "Viewport postprocessing crossed camera regions or overwrote pixels outside its viewport");
	const Size isolatedGreen = (8 * 16 + 10) * 4, isolatedOutside = (8 * 16 + 5) * 4;
	Expect(isolatedPixels[isolatedGreen + 1] > 225 && isolatedPixels[isolatedGreen] < 8 &&
	           std::abs(int(isolatedPixels[isolatedOutside]) - 179) <= 1 &&
	           std::abs(int(isolatedPixels[isolatedOutside + 1]) - 153) <= 1,
	       "Postprocessing state from another output target leaked into the isolated target");
	UInt16 rawRed = 0, rawGreen = 0;
	std::memcpy(&rawRed, backingPixels.data() + ((8 * 16 + 5) * 8), sizeof(rawRed));
	std::memcpy(&rawGreen, backingPixels.data() + ((8 * 16 + 10) * 8) + sizeof(UInt16), sizeof(rawGreen));
	Expect(HalfToFloat(rawRed) == 4 && HalfToFloat(rawGreen) == 4,
	       "Postprocessing changed the raw HDR backing used by a later camera");
}

void TestPostProcessingColorDomains(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	auto engineFiles = Take(OpenEngineFileSystem());
	const char* path = Shader::BuiltinShaderAssetPath(Shader::BuiltinFinalPostShaderId());
	Expect(path != nullptr, "FinalPost Shader path is unavailable for color-domain testing");
	auto shaderAsset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(path))));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetShaderAsset(std::make_shared<Shader::ShaderAsset>(std::move(shaderAsset))));
	Done(material->SetSampler({true, false}));
	auto source = Take(CreateRenderTexture(*device, {1, 1, 1}));
	auto linearLdr = Take(CreateRenderTexture(*device, {1, 1, 1}));
	auto encoded = Take(CreateRenderTexture(*device, {1, 1}));
	const char* blitPath = Shader::BuiltinShaderAssetPath(Shader::BuiltinBlitShaderId());
	Expect(blitPath != nullptr, "Built-in Blit Shader path is unavailable");
	auto blitShader = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(blitPath))));
	auto blitMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(blitMaterial->SetShaderAsset(std::make_shared<Shader::ShaderAsset>(std::move(blitShader))));
	auto blitSource = Take(CreateRenderTexture(*device, {1, 1, 1}));
	auto blitTarget = Take(CreateRenderTexture(*device, {1, 1, 1}));
	RenderCommand basicBlitTarget;
	basicBlitTarget.opcode = RenderCommandOpcode::SetRenderTarget;
	basicBlitTarget.target.texture = blitSource;
	RenderCommand basicBlitClear;
	basicBlitClear.opcode = RenderCommandOpcode::ClearTarget;
	basicBlitClear.clearFlags = 1;
	basicBlitClear.color = {0.25f, 0.5f, 0.75f, 1};
	RenderCommand basicBlit;
	basicBlit.opcode = RenderCommandOpcode::Blit;
	basicBlit.source.texture = blitSource;
	basicBlit.target.texture = blitTarget;
	basicBlit.material = blitMaterial;
	basicBlit.activePipeline = "UrsaUniversal";
	Done(executor.Begin(blitTarget));
	Done(executor.ResetCamera());
	Done(executor.Execute({basicBlitTarget, basicBlitClear, basicBlit}));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto blitBytes = Take(ReadRenderTexture(*device, *blitTarget));
	Array<UInt16, 4> copiedHalf{};
	std::memcpy(copiedHalf.data(), blitBytes.data(), sizeof(copiedHalf));
	Expect(std::abs(HalfToFloat(copiedHalf[0]) - 0.25f) < 0.002f &&
	           std::abs(HalfToFloat(copiedHalf[1]) - 0.5f) < 0.002f &&
	           std::abs(HalfToFloat(copiedHalf[2]) - 0.75f) < 0.002f,
	       "Built-in Universal Blit did not copy linear source color to its target");
	const Float32 zero = 0, bloomIntensity = 0;
	const Int32 reinhard = 1, noFxaa = 0, linearLdrMode = 1;
	const Array<Float32, 4> rect{0, 0, 1, 1};
	const Array<Float32, 4> size{1, 1, 1, 1};
	RenderCommand sourceTarget;
	sourceTarget.opcode = RenderCommandOpcode::SetRenderTarget;
	sourceTarget.target.texture = source;
	RenderCommand clearSource;
	clearSource.opcode = RenderCommandOpcode::ClearTarget;
	clearSource.clearFlags = 1;
	clearSource.color = {2, 1, 0.5f, 1};
	RenderCommand bloomTexture;
	bloomTexture.opcode = RenderCommandOpcode::SetGlobalParameters;
	bloomTexture.global.name = "_UrsaBloomTexture";
	bloomTexture.global.kind = 3;
	bloomTexture.global.texture.texture = source;
	RenderCommand linearBlit;
	linearBlit.opcode = RenderCommandOpcode::Blit;
	linearBlit.source.texture = source;
	linearBlit.target.texture = linearLdr;
	linearBlit.material = material;
	Vector<RenderCommand> postCommands{sourceTarget,
	                                   clearSource,
	                                   Parameter("_UrsaExposureEV", 1, &zero, sizeof(zero)),
	                                   Parameter("_UrsaToneMappingMode", 6, &reinhard, sizeof(reinhard)),
	                                   Parameter("_UrsaFxaaEnabled", 6, &noFxaa, sizeof(noFxaa)),
	                                   Parameter("_UrsaFinalPostOutputMode", 6, &linearLdrMode, sizeof(linearLdrMode)),
	                                   Parameter("_UrsaBloomIntensity", 1, &bloomIntensity, sizeof(bloomIntensity)),
	                                   Parameter("_UrsaFinalSourceRect", 2, rect.data(), sizeof(rect)),
	                                   Parameter("_UrsaFinalSourceSize", 2, size.data(), sizeof(size)),
	                                   bloomTexture,
	                                   linearBlit};
	Done(executor.Begin(linearLdr));
	Done(executor.ResetCamera());
	Done(executor.Execute(postCommands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto linearBytes = Take(ReadRenderTexture(*device, *linearLdr));
	Array<UInt16, 4> linearHalf{};
	std::memcpy(linearHalf.data(), linearBytes.data(), sizeof(linearHalf));
	Expect(std::abs(HalfToFloat(linearHalf[0]) - (2.0f / 3.0f)) < 0.002f &&
	           std::abs(HalfToFloat(linearHalf[1]) - 0.5f) < 0.002f &&
	           std::abs(HalfToFloat(linearHalf[2]) - (1.0f / 3.0f)) < 0.002f,
	       "Postprocessing intermediate was not left in linear LDR after Reinhard tone mapping");
	const auto exposedLinear = [&](Float32 exposureEV)
	{
		clearSource.color = {0.1f, 0.2f, 0.3f, 1};
		const Int32 noToneMapping = 0;
		Vector<RenderCommand> exposureCommands{
		    sourceTarget,
		    clearSource,
		    Parameter("_UrsaExposureEV", 1, &exposureEV, sizeof(exposureEV)),
		    Parameter("_UrsaToneMappingMode", 6, &noToneMapping, sizeof(noToneMapping)),
		    Parameter("_UrsaFxaaEnabled", 6, &noFxaa, sizeof(noFxaa)),
		    Parameter("_UrsaFinalPostOutputMode", 6, &linearLdrMode, sizeof(linearLdrMode)),
		    Parameter("_UrsaBloomIntensity", 1, &bloomIntensity, sizeof(bloomIntensity)),
		    Parameter("_UrsaFinalSourceRect", 2, rect.data(), sizeof(rect)),
		    Parameter("_UrsaFinalSourceSize", 2, size.data(), sizeof(size)),
		    bloomTexture,
		    linearBlit};
		Done(executor.Begin(linearLdr));
		Done(executor.ResetCamera());
		Done(executor.Execute(exposureCommands));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		const auto pixels = Take(ReadRenderTexture(*device, *linearLdr));
		Array<UInt16, 4> values{};
		std::memcpy(values.data(), pixels.data(), sizeof(values));
		return Array<Float32, 3>{HalfToFloat(values[0]), HalfToFloat(values[1]), HalfToFloat(values[2])};
	};
	const auto exposureZero = exposedLinear(0);
	const auto exposureOne = exposedLinear(1);
	for (Size channel = 0; channel < exposureZero.size(); ++channel)
	{
		Expect(std::abs(exposureOne[channel] - exposureZero[channel] * 2.0f) < 0.002f,
		       "An EV increase of one did not double the linear HDR scene color before tone mapping");
	}
	Done(executor.Begin(linearLdr));
	Done(executor.ResetCamera());
	Done(executor.Execute(postCommands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());

	const Int32 finalOutputMode = 2;
	bloomTexture.global.texture.texture = linearLdr;
	RenderCommand finalBlit;
	finalBlit.opcode = RenderCommandOpcode::Blit;
	finalBlit.source.texture = linearLdr;
	finalBlit.target.texture = encoded;
	finalBlit.material = material;
	Vector<RenderCommand> outputCommands{
	    Parameter("_UrsaFinalPostOutputMode", 6, &finalOutputMode, sizeof(finalOutputMode)),
	    Parameter("_UrsaFinalSourceRect", 2, rect.data(), sizeof(rect)),
	    Parameter("_UrsaFinalSourceSize", 2, size.data(), sizeof(size)), bloomTexture, finalBlit};
	Done(executor.Begin(encoded));
	Done(executor.ResetCamera());
	Done(executor.Execute(outputCommands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto encodedPixel = ReadPixel(device, encoded);
	Expect(encodedPixel[0] >= 212 && encodedPixel[0] <= 214 && encodedPixel[1] >= 186 && encodedPixel[1] <= 188 &&
	           encodedPixel[2] >= 155 && encodedPixel[2] <= 157,
	       "Final output did not encode the linear LDR intermediate exactly once as sRGB");
	const auto srgbCapabilities = Take(device->QueryFormat(Rhi::Format::RgbA8Srgb));
	Expect(srgbCapabilities.sampled && srgbCapabilities.colorAttachment && srgbCapabilities.transferSource &&
	           srgbCapabilities.transferDestination,
	       "The test device does not expose a sampleable sRGB color attachment with readback support");
	auto srgbOutput = std::make_shared<RenderTextureResource>();
	srgbOutput->description = {1, 1, 0, 0, false, 1};
	srgbOutput->color =
	    Take(device->CreateTexture({1, 1, Rhi::Format::RgbA8Srgb,
	                                Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled |
	                                    Rhi::TextureUsage::TransferSource | Rhi::TextureUsage::TransferDestination}));
	const Int32 srgbAttachment = 1;
	RenderCommand srgbEncoding = Parameter("_UrsaFinalOutputIsSrgb", 6, &srgbAttachment, sizeof(srgbAttachment));
	finalBlit.target.texture = srgbOutput;
	Vector<RenderCommand> srgbCommands{
	    Parameter("_UrsaFinalPostOutputMode", 6, &finalOutputMode, sizeof(finalOutputMode)),
	    srgbEncoding,
	    Parameter("_UrsaFinalSourceRect", 2, rect.data(), sizeof(rect)),
	    Parameter("_UrsaFinalSourceSize", 2, size.data(), sizeof(size)),
	    bloomTexture,
	    finalBlit};
	Done(executor.Begin(srgbOutput));
	Done(executor.ResetCamera());
	Done(executor.Execute(srgbCommands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto srgbPixel = ReadPixel(device, srgbOutput);
	for (Size channel = 0; channel < 3; ++channel)
	{
		Expect(std::abs(Int32(srgbPixel[channel]) - Int32(encodedPixel[channel])) <= 1,
		       "sRGB attachment conversion differs from the UNorm shader-encoded output");
	}
}

void TestDepthOnlyAndNormalsPasses(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	auto engineFiles = Take(OpenEngineFileSystem());
	const char* path = Shader::BuiltinShaderAssetPath(Shader::BuiltinPbrShaderId());
	Expect(path != nullptr, "Built-in Lit Shader path was not registered");
	auto shaderAsset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(path))));
	auto shader = std::make_shared<Shader::ShaderAsset>(std::move(shaderAsset));
	const auto depthSelection = Take(SelectGraphicsPass(shader, "UrsaUniversal", {"DepthOnly"}));
	const auto normalSelection = Take(SelectGraphicsPass(shader, "UrsaUniversal", {"DepthNormals"}));
	Expect(depthSelection.has_value() && normalSelection.has_value(),
	       "Built-in Lit Shader does not expose both depth preprocessing passes");
	auto material = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
	Done(material->SetShaderAsset(shader));
	auto mesh = Take(Test::Cube());
	SceneDrawRecord record;
	record.object.mesh = mesh;
	record.object.meshSnapshot = mesh->Snapshot();
	record.object.material = material;
	record.object.shader = shader;
	record.object.parameters = material->Parameters();
	record.selectedPass = true;
	const auto makeDraw = [&](const ResolvedGraphicsPass& selected)
	{
		SceneDrawRecord selectedRecord = record;
		selectedRecord.subShaderIndex = selected.subShaderIndex;
		selectedRecord.passIndex = selected.passIndex;
		selectedRecord.resolvedPass = selected;
		RenderCommand command;
		command.opcode = RenderCommandOpcode::DrawMesh;
		command.activePipeline = "UrsaUniversal";
		command.draws.push_back(std::move(selectedRecord));
		Camera camera;
		camera.position = {0, 0, 5};
		camera.target = {};
		command.camera.viewProjection = camera.ViewProjection(1);
		command.camera.position = camera.position;
		return command;
	};
	const RenderCommand depthDraw = makeDraw(*depthSelection);
	const RenderCommand normalDraw = makeDraw(*normalSelection);
	const UInt32 width = 16, height = 16;
	auto depth = Take(CreateRenderTexture(*device, {width, height, 2}));
	RenderCommand target;
	target.opcode = RenderCommandOpcode::SetRenderTarget;
	target.target.texture = depth;
	RenderCommand viewport;
	viewport.opcode = RenderCommandOpcode::SetViewport;
	viewport.viewport = {0, 0, Float32(width), Float32(height), 0, 1};
	RenderCommand scissor;
	scissor.opcode = RenderCommandOpcode::SetScissor;
	scissor.scissor = {0, 0, width, height};
	RenderCommand clear;
	clear.opcode = RenderCommandOpcode::ClearTarget;
	clear.clearFlags = 2;
	clear.clearDepth = 1;
	Done(executor.Begin());
	Done(executor.Execute({target, viewport, scissor, clear, depthDraw}));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto depthBytes = Take(ReadRenderTexture(*device, *depth));
	Float32 centerDepth = 1;
	std::memcpy(&centerDepth, depthBytes.data() + (Size(height / 2) * width + width / 2) * sizeof(Float32),
	            sizeof(centerDepth));
	Expect(centerDepth > 0 && centerDepth < 1,
	       "DepthOnly rendered without a color attachment or failed to write camera depth");
	Float32 backgroundDepth = 0;
	std::memcpy(&backgroundDepth, depthBytes.data(), sizeof(backgroundDepth));
	Expect(backgroundDepth == 1, "Depth input background was not initialized to the neutral far value");

	const Int32 depthBiasEnabled = 1;
	const Int32 depthBiasReset = 0;
	const Int32 depthBiasConstant = 10000;
	const Float32 depthBiasSlope = 0;
	const auto drawDepthWithBias = [&](Int32 enabled)
	{
		auto targetTexture = Take(CreateRenderTexture(*device, {width, height, 2}));
		RenderCommand depthTarget;
		depthTarget.opcode = RenderCommandOpcode::SetRenderTarget;
		depthTarget.target.texture = targetTexture;
		Done(executor.Begin());
		Done(executor.Execute(
		    {depthTarget, viewport, scissor, clear, Parameter("_UrsaDepthBiasOverride", 6, &enabled, sizeof(enabled)),
		     Parameter("_UrsaDepthBiasConstant", 6, &depthBiasConstant, sizeof(depthBiasConstant)),
		     Parameter("_UrsaDepthBiasSlope", 1, &depthBiasSlope, sizeof(depthBiasSlope)), depthDraw}));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		const auto bytes = Take(ReadRenderTexture(*device, *targetTexture));
		Float32 value = 1;
		std::memcpy(&value, bytes.data() + (Size(height / 2) * width + width / 2) * sizeof(Float32), sizeof(value));
		return value;
	};
	const Float32 biasedDepth = drawDepthWithBias(depthBiasEnabled);
	const Float32 restoredDepth = drawDepthWithBias(depthBiasReset);
	Expect(biasedDepth > centerDepth + 0.0001f,
	       "Command depth-bias override did not alter the depth-only rasterization result");
	Expect(std::abs(restoredDepth - centerDepth) < 0.000001f,
	       "Restoring command depth bias did not recover the receiving camera's original depth result");

	auto normals = Take(CreateRenderTexture(*device, {width, height, 1, 32}));
	RenderCommand normalTarget;
	normalTarget.opcode = RenderCommandOpcode::SetRenderTarget;
	normalTarget.target.texture = normals;
	RenderCommand normalClear;
	normalClear.opcode = RenderCommandOpcode::ClearTarget;
	normalClear.clearFlags = 3;
	normalClear.clearDepth = 1;
	normalClear.color = {0, 0, 0, 0};
	Done(executor.Begin());
	Done(executor.Execute({normalTarget, viewport, scissor, normalClear, normalDraw}));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto normalBytes = Take(ReadRenderTexture(*device, *normals));
	Array<UInt16, 4> centerNormal{};
	const Size normalOffset = (Size(height / 2) * width + width / 2) * sizeof(centerNormal);
	std::memcpy(centerNormal.data(), normalBytes.data() + normalOffset, sizeof(centerNormal));
	Expect(centerNormal[2] == 0x3c00 && centerNormal[3] == 0x3c00,
	       "DepthNormals did not write a normalized world-space normal into its color attachment");
	Array<UInt16, 4> backgroundNormal{};
	std::memcpy(backgroundNormal.data(), normalBytes.data(), sizeof(backgroundNormal));
	Expect(backgroundNormal == Array<UInt16, 4>{0, 0, 0, 0},
	       "DepthNormals background was not initialized to the zero neutral value");

	auto depthSample = Take(CreateRenderTexture(*device, {width, height}));
	const Float32 intensity = 1;
	const Int32 mode = 3;
	const Array<Float32, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	RenderCommand sampleDepth;
	sampleDepth.opcode = RenderCommandOpcode::SetGlobalParameters;
	sampleDepth.global.name = "_Source";
	sampleDepth.global.kind = 3;
	sampleDepth.global.texture.texture = depth;
	RenderCommand depthBlit;
	depthBlit.opcode = RenderCommandOpcode::Blit;
	depthBlit.source.texture = depth;
	depthBlit.material = CompositionMaterial();
	Done(executor.Begin(depthSample));
	Done(executor.Execute({sampleDepth, Tint(), Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
	                       Parameter("_Mode", 6, &mode, sizeof(mode)),
	                       Parameter("_Transform", 5, matrix.data(), sizeof(matrix)), depthBlit}));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto sampledDepth = Take(ReadRenderTexture(*device, *depthSample));
	const Size sampleOffset = (Size(height / 2) * width + width / 2) * 4;
	Expect(sampledDepth[sampleOffset] > 40 && sampledDepth[sampleOffset] < 180,
	       "Depth-only output could not be transitioned and sampled by a later full-screen pass");
	Done(device->WaitIdle());
}

void TestColorSnapshotsForInjectedGroups(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	const auto target = [&]() { return Take(CreateRenderTexture(*device, {1, 1, 1, 32})); };
	auto sceneColor = target();
	auto afterOpaques = target();
	auto firstConsumerRead = target();
	auto secondConsumerRead = target();
	auto afterTransparents = target();
	auto transparentConsumerRead = target();
	const auto setTarget = [](const std::shared_ptr<RenderTextureResource>& texture)
	{
		RenderCommand command;
		command.opcode = RenderCommandOpcode::SetRenderTarget;
		command.target.texture = texture;
		return command;
	};
	const auto clearColor = [](Color color)
	{
		RenderCommand command;
		command.opcode = RenderCommandOpcode::ClearTarget;
		command.clearFlags = 1;
		command.color = color;
		return command;
	};
	const auto copy = [](const std::shared_ptr<RenderTextureResource>& source,
	                     const std::shared_ptr<RenderTextureResource>& destination)
	{
		RenderCommand command;
		command.opcode = RenderCommandOpcode::CopyTexture;
		command.source.texture = source;
		command.target.texture = destination;
		return command;
	};
	Done(executor.Begin());
	Done(executor.Execute({setTarget(sceneColor), clearColor({0.25f, 0, 0, 1}), copy(sceneColor, afterOpaques)}));
	// A first consumer mutates the writable scene color. A second consumer in
	// the same injection group must keep reading the immutable group snapshot.
	Done(executor.Execute({setTarget(sceneColor), clearColor({0.5f, 0, 0, 1}), copy(afterOpaques, firstConsumerRead),
	                       copy(afterOpaques, secondConsumerRead)}));
	Done(executor.Execute({setTarget(sceneColor), clearColor({0.75f, 0, 0, 1}), copy(sceneColor, afterTransparents),
	                       copy(afterTransparents, transparentConsumerRead)}));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto readRed = [&](const std::shared_ptr<RenderTextureResource>& texture)
	{
		const auto bytes = Take(ReadRenderTexture(*device, *texture));
		UInt16 half = 0;
		std::memcpy(&half, bytes.data(), sizeof(half));
		return HalfToFloat(half);
	};
	Expect(std::abs(readRed(afterOpaques) - 0.25f) < 0.001f && std::abs(readRed(firstConsumerRead) - 0.25f) < 0.001f &&
	           std::abs(readRed(secondConsumerRead) - 0.25f) < 0.001f,
	       "Consumers in one injection group did not share the color snapshot from before the group");
	Expect(std::abs(readRed(afterTransparents) - 0.75f) < 0.001f &&
	           std::abs(readRed(transparentConsumerRead) - 0.75f) < 0.001f,
	       "A later injection group did not capture its updated post-transparent scene color");
	Done(device->WaitIdle());
}

void TestShadowAtlasAndCameraRestore(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	auto engineFiles = Take(OpenEngineFileSystem());
	const char* path = Shader::BuiltinShaderAssetPath(Shader::BuiltinPbrShaderId());
	Expect(path != nullptr, "Built-in Lit Shader path was not registered for shadow atlas testing");
	auto shaderAsset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(path))));
	auto shader = std::make_shared<Shader::ShaderAsset>(std::move(shaderAsset));
	const char* unlitPath = Shader::BuiltinShaderAssetPath(Shader::BuiltinUnlitShaderId());
	Expect(unlitPath != nullptr, "Built-in Unlit Shader path was not registered for camera restore testing");
	auto unlitAsset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(unlitPath))));
	auto unlitShader = std::make_shared<Shader::ShaderAsset>(std::move(unlitAsset));
	auto makeMaterial = [&](AlphaMode alphaMode, Color color)
	{
		auto material = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
		Done(material->SetShaderAsset(shader));
		Done(material->SetAlphaMode(alphaMode, 0.5f));
		Done(material->SetBaseColor(color));
		return material;
	};
	auto mesh = Take(Test::Cube());
	Scene scene;
	const auto createStatic = [&](const char* name, Float32 x, AlphaMode alphaMode, Color color)
	{
		auto object = Take(scene.CreateGameObject(name));
		LocalTransform transform;
		transform.position = {x, 0, 0};
		Done(object->SetLocalTransform(transform));
		Done(Take(object->AddComponent<MeshFilterComponent>())->SetSharedMesh(mesh));
		Done(Take(object->AddComponent<MeshRendererComponent>())->SetSharedMaterial(makeMaterial(alphaMode, color)));
		return object;
	};
	createStatic("Opaque shadow caster", 0, AlphaMode::Opaque, {1, 1, 1, 1});
	createStatic("Masked shadow caster", -2, AlphaMode::Mask, {1, 1, 1, 0});
	createStatic("Blend must not cast", -2, AlphaMode::Blend, {1, 1, 1, 1});
	auto skinnedObject = Take(scene.CreateGameObject("Skinned shadow caster"));
	LocalTransform skinnedTransform;
	skinnedTransform.position = {3, 0, 0};
	Done(skinnedObject->SetLocalTransform(skinnedTransform));
	MeshPtr skinnedMesh = Take(Test::Cube());
	MeshData skinnedData = Take(skinnedMesh->GetData());
	auto skeleton = std::make_shared<ModelSkeleton>();
	SkeletonJoint joint;
	joint.nodeLocalFileId = 1;
	joint.bindLocalTransform.position = {3, 0, 0};
	joint.inverseBindMatrix = Mat4::Identity();
	skeleton->joints = {joint};
	skeleton->modelGuid = ResourceId::Generate();
	skeleton->localFileId = 1;
	skinnedData.skeleton = skeleton;
	skinnedData.skeletonReference = {skeleton->modelGuid, 1};
	skinnedData.skinInfluences.resize(skinnedData.positions.size());
	for (auto& influence : skinnedData.skinInfluences)
	{
		influence.weights[0] = 1;
	}
	Done(skinnedMesh->SetData(std::move(skinnedData)));
	Done(skinnedMesh->RecalculateBounds());
	auto skinnedRenderer = Take(skinnedObject->AddComponent<SkinnedMeshRendererComponent>());
	Done(skinnedRenderer->SetSharedMesh(skinnedMesh));
	Done(skinnedRenderer->SetSharedMaterial(makeMaterial(AlphaMode::Opaque, {1, 1, 1, 1})));

	RenderScene snapshot;
	Done(snapshot.Capture(scene, 1));
	Vector<Mat4> sceneWorldBefore;
	for (const auto& object : snapshot.Objects())
	{
		sceneWorldBefore.push_back(object.world);
	}
	CameraData receiver;
	receiver.position = {0, 0, 5};
	receiver.view = Mat4::LookAt(receiver.position, {0, 0, 0}, {0, 1, 0});
	receiver.projection = Mat4::Perspective(0.6f, 1, 0.1f, 100);
	receiver.viewProjection = receiver.projection * receiver.view;
	receiver.inverseViewProjection = receiver.viewProjection.Inverse();
	receiver.nearPlane = 0.1f;
	receiver.farPlane = 100;
	RenderSceneContext renderContext;
	Take(renderContext.Begin(711, 1, snapshot));
	DrawingSettings shadowDrawing;
	shadowDrawing.shaderTags = {{"ShadowCaster"}};
	shadowDrawing.sorting.cameraPosition = receiver.position;
	shadowDrawing.activePipeline = "UrsaUniversal";
	FilteringSettings opaqueFiltering;
	opaqueFiltering.maximumQueue = 2500;
	const Mat4 lightViewProjection =
	    Mat4::Orthographic(8, 1, 0.1f, 100) * Mat4::LookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
	const Mat4 secondCascadeViewProjection =
	    Mat4::Orthographic(8, 1, 0.1f, 100) * Mat4::LookAt({3, 0, 10}, {3, 0, 0}, {0, 1, 0});
	const auto makeDrawList = [&](CullingResults culling)
	{
		const RendererList list = Take(renderContext.CreateRendererList(culling, shadowDrawing, opaqueFiltering));
		const auto entries = Take(renderContext.Entries(list));
		RenderCommand command;
		command.opcode = RenderCommandOpcode::DrawRendererList;
		command.camera = receiver;
		command.lighting = snapshot.Lighting();
		for (const auto& entry : *entries)
		{
			SceneDrawRecord draw;
			draw.object = snapshot.Objects()[entry.objectIndex];
			draw.object.material = entry.material;
			draw.subShaderIndex = entry.subShaderIndex;
			draw.passIndex = entry.passIndex;
			draw.selectedPass = true;
			draw.activePipeline = entry.activePipeline;
			draw.resolvedPass = entry.resolvedPass;
			command.draws.push_back(std::move(draw));
		}
		return command;
	};
	const auto firstCulling = Take(renderContext.CullShadowCasters(receiver, lightViewProjection));
	const auto secondCulling = Take(renderContext.CullShadowCasters(receiver, secondCascadeViewProjection));
	const auto firstShadowDraws = makeDrawList(firstCulling);
	const auto secondShadowDraws = makeDrawList(secondCulling);
	Expect(firstShadowDraws.draws.size() == 3 && secondShadowDraws.draws.size() == 2,
	       "Shadow caster lists did not include static, skinned and Mask geometry while filtering Blend and off-tile "
	       "objects");

	const UInt32 tileWidth = 64, atlasWidth = 128, atlasHeight = 64;
	auto atlas = Take(CreateRenderTexture(*device, {atlasWidth, atlasHeight, 2}));
	auto cameraDepth = Take(CreateRenderTexture(*device, {tileWidth, tileWidth, 2}));
	const auto setTarget = [](const std::shared_ptr<RenderTextureResource>& texture)
	{
		RenderCommand command;
		command.opcode = RenderCommandOpcode::SetRenderTarget;
		command.target.texture = texture;
		return command;
	};
	const auto setViewportAndScissor = [](UInt32 x, UInt32 y, UInt32 width, UInt32 height)
	{
		RenderCommand viewport;
		viewport.opcode = RenderCommandOpcode::SetViewport;
		viewport.viewport = {Float32(x), Float32(y), Float32(width), Float32(height), 0, 1};
		RenderCommand scissor;
		scissor.opcode = RenderCommandOpcode::SetScissor;
		scissor.scissor = {static_cast<Int32>(x), static_cast<Int32>(y), width, height};
		return Vector<RenderCommand>{viewport, scissor};
	};
	const auto setViewProjection = [](const Mat4& matrix)
	{
		RenderCommand command = Parameter("_UrsaViewProjection", 5, matrix.values.data(), sizeof(matrix.values));
		return command;
	};
	RenderCommand clearDepth;
	clearDepth.opcode = RenderCommandOpcode::ClearTarget;
	clearDepth.clearFlags = 2;
	clearDepth.clearDepth = 1;
	Vector<RenderCommand> commands{setTarget(atlas)};
	const auto fullAtlas = setViewportAndScissor(0, 0, atlasWidth, atlasHeight);
	commands.insert(commands.end(), fullAtlas.begin(), fullAtlas.end());
	commands.push_back(clearDepth);
	for (UInt32 cascade = 0; cascade < 2; ++cascade)
	{
		const UInt32 tileX = cascade * tileWidth;
		const auto tile = setViewportAndScissor(tileX, 0, tileWidth, tileWidth);
		commands.insert(commands.end(), tile.begin(), tile.end());
		commands.push_back(setViewProjection(cascade == 0 ? lightViewProjection : secondCascadeViewProjection));
		commands.push_back(cascade == 0 ? firstShadowDraws : secondShadowDraws);
	}
	commands.push_back(setTarget(cameraDepth));
	const auto cameraViewport = setViewportAndScissor(0, 0, tileWidth, tileWidth);
	commands.insert(commands.end(), cameraViewport.begin(), cameraViewport.end());
	commands.push_back(clearDepth);
	commands.push_back(setViewProjection(receiver.viewProjection));
	const auto receiverCulling = Take(renderContext.Cull(receiver));
	DrawingSettings depthDrawing = shadowDrawing;
	depthDrawing.shaderTags = {{"DepthOnly"}};
	FilteringSettings depthFiltering;
	depthFiltering.maximumQueue = 2500;
	const RendererList receiverList =
	    Take(renderContext.CreateRendererList(receiverCulling, depthDrawing, depthFiltering));
	const auto receiverEntries = Take(renderContext.Entries(receiverList));
	RenderCommand receiverDepthDraw;
	receiverDepthDraw.opcode = RenderCommandOpcode::DrawRendererList;
	receiverDepthDraw.camera = receiver;
	receiverDepthDraw.lighting = snapshot.Lighting();
	for (const auto& entry : *receiverEntries)
	{
		SceneDrawRecord draw;
		draw.object = snapshot.Objects()[entry.objectIndex];
		draw.object.material = entry.material;
		draw.subShaderIndex = entry.subShaderIndex;
		draw.passIndex = entry.passIndex;
		draw.selectedPass = true;
		draw.activePipeline = entry.activePipeline;
		draw.resolvedPass = entry.resolvedPass;
		receiverDepthDraw.draws.push_back(std::move(draw));
	}
	commands.push_back(std::move(receiverDepthDraw));
	auto receiverColor = Take(CreateRenderTexture(*device, {tileWidth, tileWidth, 0, 32}));
	commands.push_back(setTarget(receiverColor));
	commands.push_back(setViewProjection(receiver.viewProjection));
	const auto colorViewport = setViewportAndScissor(0, 0, tileWidth, tileWidth);
	commands.insert(commands.end(), colorViewport.begin(), colorViewport.end());
	RenderCommand clearColor;
	clearColor.opcode = RenderCommandOpcode::ClearTarget;
	clearColor.clearFlags = 3;
	clearColor.clearDepth = 1;
	clearColor.color = {0, 0, 0, 1};
	commands.push_back(clearColor);
	const auto colorSelection = Take(SelectGraphicsPass(unlitShader, "UrsaUniversal", {"UniversalForward"}));
	Expect(colorSelection.has_value(), "Built-in Unlit Shader is missing its UniversalForward pass");
	auto receiverMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(receiverMaterial->SetShaderAsset(unlitShader));
	Done(receiverMaterial->SetBaseColor({1, 0, 0, 1}));
	const auto opaqueObject = std::find_if(snapshot.Objects().begin(), snapshot.Objects().end(),
	                                       [](const auto& object) { return object.world.At(0, 3) == 0; });
	Expect(opaqueObject != snapshot.Objects().end(), "Opaque receiver test object was not captured");
	SceneDrawRecord receiverColorRecord;
	receiverColorRecord.object = *opaqueObject;
	receiverColorRecord.object.material = receiverMaterial;
	receiverColorRecord.object.parameters = receiverMaterial->Parameters();
	receiverColorRecord.subShaderIndex = colorSelection->subShaderIndex;
	receiverColorRecord.passIndex = colorSelection->passIndex;
	receiverColorRecord.selectedPass = true;
	receiverColorRecord.activePipeline = "UrsaUniversal";
	receiverColorRecord.resolvedPass = *colorSelection;
	RenderCommand receiverColorDraw;
	receiverColorDraw.opcode = RenderCommandOpcode::DrawMesh;
	receiverColorDraw.camera = receiver;
	receiverColorDraw.lighting = snapshot.Lighting();
	receiverColorDraw.draws.push_back(std::move(receiverColorRecord));
	commands.push_back(std::move(receiverColorDraw));
	Done(executor.Begin());
	Done(executor.Execute(commands));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());
	const auto atlasBytes = Take(ReadRenderTexture(*device, *atlas));
	const auto depthAt = [](const Vector<UInt8>& pixels, UInt32 width, UInt32 x, UInt32 y)
	{
		Float32 depth = 1;
		std::memcpy(&depth, pixels.data() + (Size(y) * width + x) * sizeof(depth), sizeof(depth));
		return depth;
	};
	const Float32 staticDepth0 = depthAt(atlasBytes, atlasWidth, 32, 32);
	const Float32 skinnedDepth0 = depthAt(atlasBytes, atlasWidth, 56, 32);
	const Float32 maskDepth0 = depthAt(atlasBytes, atlasWidth, 16, 32);
	const Float32 staticDepth1 = depthAt(atlasBytes, atlasWidth, 64 + 8, 32);
	const Float32 skinnedDepth1 = depthAt(atlasBytes, atlasWidth, 64 + 32, 32);
	Expect(staticDepth0 < 1 && skinnedDepth0 < 1 && maskDepth0 == 1 && staticDepth1 < 1 && skinnedDepth1 < 1,
	       ("Shadow atlas pixel contract failed: " + std::to_string(staticDepth0) + "," +
	        std::to_string(skinnedDepth0) + "," + std::to_string(maskDepth0) + "," + std::to_string(staticDepth1) +
	        "," + std::to_string(skinnedDepth1))
	           .c_str());
	const auto cameraBytes = Take(ReadRenderTexture(*device, *cameraDepth));
	Expect(depthAt(cameraBytes, tileWidth, tileWidth / 2, tileWidth / 2) < 1,
	       "Shadow rendering did not restore the receiver camera projection before its depth draw");
	const auto colorBytes = Take(ReadRenderTexture(*device, *receiverColor));
	const Size centerColor = (Size(tileWidth / 2) * tileWidth + tileWidth / 2) * 4;
	Expect(colorBytes[centerColor] > 240 && colorBytes[centerColor + 1] < 15 && colorBytes[centerColor + 2] < 15,
	       ("Shadow rendering did not restore the receiver camera state before the color stage: " +
	        std::to_string(colorBytes[centerColor]) + "," + std::to_string(colorBytes[centerColor + 1]) + "," +
	        std::to_string(colorBytes[centerColor + 2]) + "," + std::to_string(colorBytes[centerColor + 3]))
	           .c_str());
	Expect(snapshot.Objects().size() == sceneWorldBefore.size(),
	       "Shadow view override changed the scene snapshot size");
	for (Size index = 0; index < sceneWorldBefore.size(); ++index)
	{
		Expect(std::memcmp(snapshot.Objects()[index].world.values.data(), sceneWorldBefore[index].values.data(),
		                   sizeof(sceneWorldBefore[index].values)) == 0,
		       "A draw view override modified a captured scene transform");
	}
	Done(renderContext.End());
}

void TestAnimatedMaskedShadowProjection(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	using Json = nlohmann::json;
	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const ResourceId modelGuid = ResourceId::Generate();
	const auto skeletonPath = Take(VirtualPath::Parse("prj:ShadowValidation.skeleton"));
	const auto clipPath = Take(VirtualPath::Parse("prj:ShadowValidation.clip"));
	const auto writeCbor = [&](const VirtualPath& path, const Json& document)
	{
		const auto bytes = Json::to_cbor(document);
		Vector<UInt8> payload;
		payload.reserve(bytes.size());
		for (UInt8 byte : bytes)
		{
			payload.push_back(byte);
		}
		Done(files.WriteAtomic(path, payload));
	};
	const Json identityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const Json bindTransform = {{"position", {-1, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}};
	writeCbor(skeletonPath, {{"format", "UrsaSkeleton"},
	                         {"version", 1},
	                         {"localFileId", 1},
	                         {"joints",
	                          {{{"index", 0},
	                            {"nodeLocalFileId", 1},
	                            {"parentJointIndex", nullptr},
	                            {"name", "ShadowMaskJoint"},
	                            {"bindLocalTransform", bindTransform},
	                            {"inverseBindMatrix", identityMatrix}}}}});
	writeCbor(clipPath, {{"format", "UrsaAnimationClip"},
	                     {"version", 1},
	                     {"localFileId", 2},
	                     {"name", "ShadowMaskSlide"},
	                     {"durationSeconds", 1.0},
	                     {"channels",
	                      {{{"nodeLocalFileId", 1},
	                        {"positionKeys", {{0.0, {-1, 0, 0}}, {1.0, {0, 0, 0}}}},
	                        {"rotationKeys", Json::array()},
	                        {"scaleKeys", Json::array()}}}}});
	auto resources = Take(
	    ResourceSession::Open(files,
	                          [&](ResourceId guid, UInt64 localFileId)
	                          {
		                          if (guid != modelGuid)
		                          {
			                          return Result<VirtualPath>::Failure("Unexpected shadow validation asset GUID");
		                          }
		                          if (localFileId == 1)
		                          {
			                          return Result<VirtualPath>::Success(skeletonPath);
		                          }
		                          if (localFileId == 2)
		                          {
			                          return Result<VirtualPath>::Success(clipPath);
		                          }
		                          return Result<VirtualPath>::Failure("Unexpected shadow validation local file ID");
	                          }));
	Scene scene;
	auto object = Take(scene.CreateGameObject("Animated Mask Shadow Caster"));
	auto animator = Take(object->AddComponent<AnimatorComponent>());
	Done(animator->SetAnimation(modelGuid, 1, 2, *resources));
	animator->SetLoop(false);
	MeshData meshData;
	meshData.positions = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}};
	meshData.normals = Vector<Vec3>(4, {0, 0, 1});
	meshData.uv0 = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	meshData.tangents = Vector<Vec4>(4, {1, 0, 0, 1});
	meshData.indices = {0, 2, 1, 0, 3, 2};
	meshData.subMeshes = {{0, 6}};
	auto skeleton = std::make_shared<ModelSkeleton>();
	SkeletonJoint joint;
	joint.nodeLocalFileId = 1;
	joint.bindLocalTransform.position = {-1, 0, 0};
	joint.inverseBindMatrix = Mat4::Identity();
	skeleton->joints = {joint};
	skeleton->modelGuid = modelGuid;
	skeleton->localFileId = 1;
	meshData.skeleton = skeleton;
	meshData.skeletonReference = {modelGuid, 1};
	meshData.skinInfluences.resize(meshData.positions.size());
	for (auto& influence : meshData.skinInfluences)
	{
		influence.weights[0] = 1;
	}
	auto mesh = Take(Mesh::Create(std::move(meshData)));
	Done(mesh->RecalculateBounds());
	const auto alphaTexture = Take(MaterialTexture::Create(2, 1, TextureColorSpace::Srgb,
	                                                       {255, 255, 255, 0, 255, 255, 255, 255}, {}, {false, false}));
	auto engineFiles = Take(OpenEngineFileSystem());
	const char* litPath = Shader::BuiltinShaderAssetPath(Shader::BuiltinPbrShaderId());
	auto litAsset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(litPath))));
	auto litShader = std::make_shared<Shader::ShaderAsset>(std::move(litAsset));
	auto material = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
	Done(material->SetShaderAsset(litShader));
	Done(material->SetAlphaMode(AlphaMode::Mask, 0.5f));
	Done(material->SetBaseColor({1, 1, 1, 1}));
	Done(material->SetTexture(MaterialTextureSlot::BaseColor, alphaTexture));
	Done(material->SetSampler({false, false}));
	auto skinnedRenderer = Take(object->AddComponent<SkinnedMeshRendererComponent>());
	Done(skinnedRenderer->SetSharedMesh(mesh));
	Done(skinnedRenderer->SetSharedMaterial(material));

	const auto shadowAtlas = Take(CreateRenderTexture(*device, {64, 64, 2}));
	const auto camera = []
	{
		CameraData result;
		result.position = {0, 0, 5};
		result.view = Mat4::LookAt(result.position, {0, 0, 0}, {0, 1, 0});
		result.projection = Mat4::Perspective(0.8f, 1, 0.1f, 100);
		result.viewProjection = result.projection * result.view;
		result.inverseViewProjection = result.viewProjection.Inverse();
		result.nearPlane = 0.1f;
		result.farPlane = 100;
		return result;
	}();
	const Mat4 lightViewProjection =
	    Mat4::Orthographic(8, 1, 0.1f, 100) * Mat4::LookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
	UInt64 frame = 1;
	const auto renderPose = [&](Ursa::Float64 time, UInt32 holeX, UInt32 solidX, const char* imageName)
	{
		Done(animator->SetTime(time));
		RenderScene snapshot;
		Done(snapshot.Capture(scene, 1));
		RenderSceneContext renderContext;
		Take(renderContext.Begin(929, frame++, snapshot));
		const auto culling = Take(renderContext.CullShadowCasters(camera, lightViewProjection));
		DrawingSettings drawing;
		drawing.shaderTags = {{"ShadowCaster"}};
		drawing.sorting.cameraPosition = camera.position;
		drawing.activePipeline = "UrsaUniversal";
		FilteringSettings filtering;
		filtering.maximumQueue = 2500;
		const auto list = Take(renderContext.CreateRendererList(culling, drawing, filtering));
		const auto entries = Take(renderContext.Entries(list));
		Expect(entries->size() == 1, "Animated Mask did not produce a shadow caster entry");
		RenderCommand draw;
		draw.opcode = RenderCommandOpcode::DrawRendererList;
		draw.camera = camera;
		draw.lighting = snapshot.Lighting();
		for (const auto& entry : *entries)
		{
			SceneDrawRecord item;
			item.object = snapshot.Objects()[entry.objectIndex];
			item.object.material = entry.material;
			item.subShaderIndex = entry.subShaderIndex;
			item.passIndex = entry.passIndex;
			item.selectedPass = true;
			item.activePipeline = entry.activePipeline;
			item.resolvedPass = entry.resolvedPass;
			draw.draws.push_back(std::move(item));
		}
		RenderCommand target;
		target.opcode = RenderCommandOpcode::SetRenderTarget;
		target.target.texture = shadowAtlas;
		RenderCommand viewport;
		viewport.opcode = RenderCommandOpcode::SetViewport;
		viewport.viewport = {0, 0, 64, 64, 0, 1};
		RenderCommand scissor;
		scissor.opcode = RenderCommandOpcode::SetScissor;
		scissor.scissor = {0, 0, 64, 64};
		RenderCommand clear;
		clear.opcode = RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 2;
		clear.clearDepth = 1;
		const Float32 normalBias = 0;
		const Array<Float32, 4> mainLightDirection{0, 0, 1, 0};
		const RenderCommand lightMatrix =
		    Parameter("_UrsaViewProjection", 5, lightViewProjection.values.data(), sizeof(lightViewProjection.values));
		Done(executor.Begin());
		Done(executor.ResetCamera(camera));
		Done(executor.Execute(
		    {target, viewport, scissor, clear,
		     Parameter("_UrsaDirectionalShadowNormalBias", 1, &normalBias, sizeof(normalBias)),
		     Parameter("_UrsaMainLightDirection", 2, mainLightDirection.data(), sizeof(mainLightDirection)),
		     lightMatrix, draw}));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		const auto depth = Take(ReadRenderTexture(*device, *shadowAtlas));
		const auto depthAt = [&](UInt32 x, UInt32 y)
		{
			Float32 value = 1;
			std::memcpy(&value, depth.data() + (Size(y) * 64 + x) * sizeof(value), sizeof(value));
			return value;
		};
		const Float32 holeDepth = depthAt(holeX, 32), solidDepth = depthAt(solidX, 32);
		UInt32 written = 0, minimumX = 64, maximumX = 0, minimumY = 64, maximumY = 0;
		for (UInt32 y = 0; y < 64; ++y)
		{
			for (UInt32 x = 0; x < 64; ++x)
			{
				if (depthAt(x, y) < 0.9999f)
				{
					++written;
					minimumX = std::min(minimumX, x);
					maximumX = std::max(maximumX, x);
					minimumY = std::min(minimumY, y);
					maximumY = std::max(maximumY, y);
				}
			}
		}
		Expect(holeDepth == 1 && solidDepth < 1,
		       ("Animated SkinnedMesh Mask failed to preserve its alpha hole in the shadow atlas at time " +
		        std::to_string(time) + ": " + std::to_string(holeDepth) + "," + std::to_string(solidDepth) +
		        " written=" + std::to_string(written) + " bounds=" + std::to_string(minimumX) + "," +
		        std::to_string(minimumY) + "-" + std::to_string(maximumX) + "," + std::to_string(maximumY))
		           .c_str());
		const auto preview = Take(CreateRenderTexture(*device, {256, 256}));
		RenderCommand atlasBinding;
		atlasBinding.opcode = RenderCommandOpcode::SetGlobalParameters;
		atlasBinding.global.name = "_UrsaDirectionalShadowAtlas";
		atlasBinding.global.kind = 3;
		atlasBinding.global.texture.texture = shadowAtlas;
		const Int32 mode = 1;
		RenderCommand previewTarget;
		previewTarget.opcode = RenderCommandOpcode::SetRenderTarget;
		previewTarget.target.texture = preview;
		RenderCommand previewClear;
		previewClear.opcode = RenderCommandOpcode::ClearTarget;
		previewClear.clearFlags = 1;
		previewClear.color = {0, 0, 0, 1};
		RenderCommand previewViewport;
		previewViewport.opcode = RenderCommandOpcode::SetViewport;
		previewViewport.viewport = {0, 0, 256, 256, 0, 1};
		RenderCommand previewScissor;
		previewScissor.opcode = RenderCommandOpcode::SetScissor;
		previewScissor.scissor = {0, 0, 256, 256};
		RenderCommand visualize;
		visualize.opcode = RenderCommandOpcode::DrawFullscreen;
		visualize.material = ShadowProbeMaterial();
		Done(executor.Begin(preview));
		Done(executor.ResetCamera());
		Done(executor.Execute({previewTarget, previewViewport, previewScissor, previewClear, atlasBinding,
		                       Parameter("_ShadowProbeMode", 6, &mode, sizeof(mode)), visualize}));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		const auto image = Take(ReadFrameImage(device, preview->color, preview->colorState));
		Done(WriteFrameImage(std::filesystem::path(URSA_SHADER_STAGING_ROOT) / "ShadowValidation" / imageName, image));
		Done(renderContext.End());
	};

	renderPose(0, 22, 26, "AnimatedMaskPose0.png");
	renderPose(1, 30, 34, "AnimatedMaskPose1.png");
}

void TestStableCascadeShadowImage(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	auto engineFiles = Take(OpenEngineFileSystem());
	const char* litPath = Shader::BuiltinShaderAssetPath(Shader::BuiltinPbrShaderId());
	auto litAsset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(litPath))));
	auto litShader = std::make_shared<Shader::ShaderAsset>(std::move(litAsset));
	auto mesh = Take(Test::Cube());
	auto material = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
	Done(material->SetShaderAsset(litShader));
	Scene scene;
	auto object = Take(scene.CreateGameObject("Stable shadow receiver"));
	Done(Take(object->AddComponent<MeshFilterComponent>())->SetSharedMesh(mesh));
	Done(Take(object->AddComponent<MeshRendererComponent>())->SetSharedMaterial(material));
	RenderScene snapshot;
	Done(snapshot.Capture(scene, 1));
	CameraData receiver;
	receiver.position = {0, 0, 5};
	receiver.view = Mat4::LookAt(receiver.position, {0, 0, 0}, {0, 1, 0});
	receiver.projection = Mat4::Perspective(0.8f, 1, 0.1f, 20);
	receiver.viewProjection = receiver.projection * receiver.view;
	receiver.inverseViewProjection = receiver.viewProjection.Inverse();
	receiver.nearPlane = 0.1f;
	receiver.farPlane = 20;
	DirectionalShadowCascadeSettings settings;
	settings.cascadeCount = 1;
	settings.shadowDistance = 20;
	settings.splitFractions.clear();
	settings.atlasWidth = 64;
	settings.atlasHeight = 64;
	const auto initialCascade = Take(ComputeDirectionalShadowCascades(receiver, {0, -1, 0}, settings))[0];
	const Float32 movement = initialCascade.worldUnitsPerTexel * 0.1f;
	CameraData movedReceiver = receiver;
	movedReceiver.position.x += movement;
	movedReceiver.view = Mat4::LookAt(movedReceiver.position, {movement, 0, 0}, {0, 1, 0});
	movedReceiver.viewProjection = movedReceiver.projection * movedReceiver.view;
	movedReceiver.inverseViewProjection = movedReceiver.viewProjection.Inverse();
	const auto movedCascade = Take(ComputeDirectionalShadowCascades(movedReceiver, {0, -1, 0}, settings))[0];
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			Expect(std::abs(initialCascade.worldToShadow.At(row, column) - movedCascade.worldToShadow.At(row, column)) <
			           1e-5f,
			       "Sub-texel receiver motion changed the stabilized cascade matrix");
		}
	}
	auto atlas = Take(CreateRenderTexture(*device, {64, 64, 2}));
	auto preview = Take(CreateRenderTexture(*device, {256, 256}));
	auto probe = ShadowProbeMaterial();
	UInt64 frame = 1;
	const auto render = [&](const CameraData& camera, const char* imageName)
	{
		RenderSceneContext context;
		Take(context.Begin(932, frame++, snapshot));
		const Mat4 lightViewProjection = initialCascade.lightProjection * initialCascade.lightView;
		const auto culling = Take(context.CullShadowCasters(camera, lightViewProjection));
		DrawingSettings drawing;
		drawing.shaderTags = {{"ShadowCaster"}};
		drawing.sorting.cameraPosition = camera.position;
		drawing.activePipeline = "UrsaUniversal";
		FilteringSettings filtering;
		filtering.maximumQueue = 2500;
		const auto list = Take(context.CreateRendererList(culling, drawing, filtering));
		const auto entries = Take(context.Entries(list));
		Expect(entries->size() == 1, "Stable projection fixture did not retain its caster");
		RenderCommand draw;
		draw.opcode = RenderCommandOpcode::DrawRendererList;
		draw.camera = camera;
		draw.lighting = snapshot.Lighting();
		for (const auto& entry : *entries)
		{
			SceneDrawRecord item;
			item.object = snapshot.Objects()[entry.objectIndex];
			item.object.material = entry.material;
			item.subShaderIndex = entry.subShaderIndex;
			item.passIndex = entry.passIndex;
			item.selectedPass = true;
			item.activePipeline = entry.activePipeline;
			item.resolvedPass = entry.resolvedPass;
			draw.draws.push_back(std::move(item));
		}
		RenderCommand target;
		target.opcode = RenderCommandOpcode::SetRenderTarget;
		target.target.texture = atlas;
		RenderCommand viewport;
		viewport.opcode = RenderCommandOpcode::SetViewport;
		viewport.viewport = {0, 0, 64, 64, 0, 1};
		RenderCommand scissor;
		scissor.opcode = RenderCommandOpcode::SetScissor;
		scissor.scissor = {0, 0, 64, 64};
		RenderCommand clear;
		clear.opcode = RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 2;
		clear.clearDepth = 1;
		const auto viewProjection =
		    Parameter("_UrsaViewProjection", 5, lightViewProjection.values.data(), sizeof(lightViewProjection.values));
		Done(executor.Begin());
		Done(executor.ResetCamera(camera));
		Done(executor.Execute({target, viewport, scissor, clear, viewProjection, draw}));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		const auto depth = Take(ReadRenderTexture(*device, *atlas));
		UInt32 writtenPixels = 0;
		for (Size offset = 0; offset < depth.size(); offset += sizeof(Float32))
		{
			Float32 value = 1;
			std::memcpy(&value, depth.data() + offset, sizeof(value));
			writtenPixels += value < 0.9999f;
		}
		Expect(writtenPixels > 0, "Stabilized cascade shadow atlas contains no caster pixels");

		RenderCommand atlasBinding;
		atlasBinding.opcode = RenderCommandOpcode::SetGlobalParameters;
		atlasBinding.global.name = "_UrsaDirectionalShadowAtlas";
		atlasBinding.global.kind = 3;
		atlasBinding.global.texture.texture = atlas;
		const Int32 probeMode = 1;
		RenderCommand previewTarget;
		previewTarget.opcode = RenderCommandOpcode::SetRenderTarget;
		previewTarget.target.texture = preview;
		RenderCommand previewViewport;
		previewViewport.opcode = RenderCommandOpcode::SetViewport;
		previewViewport.viewport = {0, 0, 256, 256, 0, 1};
		RenderCommand previewScissor;
		previewScissor.opcode = RenderCommandOpcode::SetScissor;
		previewScissor.scissor = {0, 0, 256, 256};
		RenderCommand previewClear;
		previewClear.opcode = RenderCommandOpcode::ClearTarget;
		previewClear.clearFlags = 1;
		RenderCommand visualize;
		visualize.opcode = RenderCommandOpcode::DrawFullscreen;
		visualize.material = probe;
		Done(executor.Begin(preview));
		Done(executor.ResetCamera());
		Done(executor.Execute({previewTarget, previewViewport, previewScissor, previewClear, atlasBinding,
		                       Parameter("_ShadowProbeMode", 6, &probeMode, sizeof(probeMode)), visualize}));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		const auto image = Take(ReadFrameImage(device, preview->color, preview->colorState));
		Done(WriteFrameImage(std::filesystem::path(URSA_SHADER_STAGING_ROOT) / "ShadowValidation" / imageName, image));
		Done(context.End());
		return depth;
	};
	const auto originalDepth = render(receiver, "StableProjectionBase.png");
	const auto movedDepth = render(movedReceiver, "StableProjectionSubTexel.png");
	Expect(originalDepth.size() == movedDepth.size() &&
	           std::equal(originalDepth.begin(), originalDepth.end(), movedDepth.begin()),
	       "A sub-texel receiver move changed the rendered stabilized shadow image");
}

void TestMainLightShadowSampling(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	auto material = ShadowProbeMaterial();
	auto atlas = Take(CreateRenderTexture(*device, {8, 8, 2}));
	auto output = Take(CreateRenderTexture(*device, {1, 1}));
	const auto clearAtlas = [&](Float32 leftHalfDepth, Float32 rightHalfDepth)
	{
		RenderCommand target;
		target.opcode = RenderCommandOpcode::SetRenderTarget;
		target.target.texture = atlas;
		RenderCommand viewport;
		viewport.opcode = RenderCommandOpcode::SetViewport;
		viewport.viewport = {0, 0, 8, 8, 0, 1};
		RenderCommand fullScissor;
		fullScissor.opcode = RenderCommandOpcode::SetScissor;
		fullScissor.scissor = {0, 0, 8, 8};
		RenderCommand clear;
		clear.opcode = RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 2;
		clear.clearDepth = leftHalfDepth;
		RenderCommand rightScissor = fullScissor;
		rightScissor.scissor = {4, 0, 4, 8};
		RenderCommand clearRight = clear;
		clearRight.clearDepth = 1;
		Done(executor.Begin());
		Done(executor.ResetCamera());
		Done(executor.Execute({target, viewport, fullScissor, clear, rightScissor, clearRight}));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
	};
	const auto render =
	    [&](Int32 cascadeCount, Int32 soft, Float32 strength, Float32 shadowU, Float32 secondShadowU, Float32 viewDepth)
	{
		Mat4 shadowMatrix;
		shadowMatrix.At(0, 3) = shadowU;
		shadowMatrix.At(1, 3) = 0.4375f;
		shadowMatrix.At(2, 3) = 0.5f;
		shadowMatrix.At(3, 3) = 1;
		Mat4 secondShadowMatrix = shadowMatrix;
		secondShadowMatrix.At(0, 3) = secondShadowU;
		Array<Mat4, 4> matrices{shadowMatrix, secondShadowMatrix, shadowMatrix, shadowMatrix};
		const Array<Float32, 4> splits{10, 20, 30, 40};
		const Array<Float32, 4> texelSize{1.0f / 8, 1.0f / 8, 8, 8};
		const Array<Float32, 4> lightDirection{0, 1, 0, 0};
		Mat4 view = Mat4::Identity();
		view.At(2, 3) = -viewDepth;
		RenderCommand atlasBinding;
		atlasBinding.opcode = RenderCommandOpcode::SetGlobalParameters;
		atlasBinding.global.name = "_UrsaDirectionalShadowAtlas";
		atlasBinding.global.kind = 3;
		atlasBinding.global.texture.texture = atlas;
		RenderCommand matricesBinding =
		    Parameter("_UrsaDirectionalShadowMatrices", 8, matrices.data(), sizeof(matrices));
		RenderCommand splitsBinding =
		    Parameter("_UrsaDirectionalShadowCascadeSplits", 2, splits.data(), sizeof(splits));
		RenderCommand texelBinding =
		    Parameter("_UrsaDirectionalShadowTexelSize", 2, texelSize.data(), sizeof(texelSize));
		RenderCommand lightBinding =
		    Parameter("_UrsaMainLightDirection", 2, lightDirection.data(), sizeof(lightDirection));
		RenderCommand viewBinding = Parameter("_UrsaView", 5, view.values.data(), sizeof(view.values));
		const Int32 mainLightIndex = 0;
		const Int32 probeMode = 0;
		const Float32 bias = 0, normalBias = 0, blendRatio = 0.1f;
		Vector<RenderCommand> commands{
		    atlasBinding,
		    matricesBinding,
		    splitsBinding,
		    texelBinding,
		    lightBinding,
		    viewBinding,
		    Parameter("_UrsaDirectionalShadowCascadeCount", 6, &cascadeCount, sizeof(cascadeCount)),
		    Parameter("_UrsaDirectionalShadowSoft", 6, &soft, sizeof(soft)),
		    Parameter("_UrsaMainLightIndex", 6, &mainLightIndex, sizeof(mainLightIndex)),
		    Parameter("_UrsaDirectionalShadowStrength", 1, &strength, sizeof(strength)),
		    Parameter("_UrsaDirectionalShadowBias", 1, &bias, sizeof(bias)),
		    Parameter("_UrsaDirectionalShadowNormalBias", 1, &normalBias, sizeof(normalBias)),
		    Parameter("_UrsaDirectionalShadowCascadeBlend", 1, &blendRatio, sizeof(blendRatio)),
		    Parameter("_ShadowProbeMode", 6, &probeMode, sizeof(probeMode))};
		RenderCommand fullscreen;
		fullscreen.opcode = RenderCommandOpcode::DrawFullscreen;
		fullscreen.material = material;
		commands.push_back(fullscreen);
		Done(executor.Begin(output));
		Done(executor.ResetCamera());
		Done(executor.Execute(commands));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		return ReadPixel(device, output);
	};

	clearAtlas(0, 1);
	const auto hard = render(1, 0, 0.75f, 0.4375f, 0.5625f, 0);
	const auto soft = render(1, 1, 0.75f, 0.4375f, 0.5625f, 0);
	Expect(hard[0] >= 62 && hard[0] <= 66,
	       "Hard shadow comparison did not apply the expected 0.75 strength to a fully occluded sample");
	Expect(soft[0] >= 126 && soft[0] <= 130,
	       "URP-style 3x3 PCF did not match the expected one-third visible tap fraction");
	clearAtlas(0, 1);
	const auto strengthZero = render(1, 1, 0, 0.4375f, 0.5625f, 0);
	Expect(strengthZero[0] > 250, "Zero shadow strength did not blend back to fully lit");
	const auto noCascade = render(0, 0, 1, 0.4375f, 0.5625f, 5);
	Expect(noCascade[0] > 250, "A no-shadow camera did not use the neutral fully lit result");
	clearAtlas(1, 1);
	const auto unoccluded = render(1, 1, 1, 0.4375f, 0.5625f, 0);
	Expect(unoccluded[0] > 250, "Unoccluded atlas depth produced a shadow");
	clearAtlas(0, 1);
	const auto tileClamped = render(2, 1, 0.75f, 0.4375f, 0.5625f, 0);
	Expect(tileClamped[0] >= 62 && tileClamped[0] <= 66,
	       "PCF taps escaped the first cascade tile into the neighboring lit tile");
	const auto outside = render(1, 1, 1, 1.1f, 0.5625f, 0);
	Expect(outside[0] > 250, "Shadow coordinates outside the light frustum were not treated as lit");
	clearAtlas(0, 1);
	const auto cascadeStart = render(2, 0, 1, 0.4375f, 0.5625f, 9);
	const auto cascadeMidpoint = render(2, 0, 1, 0.4375f, 0.5625f, 10);
	const auto cascadeEnd = render(2, 0, 1, 0.4375f, 0.5625f, 11);
	Expect(cascadeStart[0] < 8 && cascadeMidpoint[0] >= 126 && cascadeMidpoint[0] <= 130 && cascadeEnd[0] > 250,
	       "Cascade transition did not blend continuously between the adjacent shadow slices");
	clearAtlas(0, 1);
	const auto distanceFadeStart = render(1, 0, 1, 0.4375f, 0.5625f, 9);
	const auto distanceFadeMidpoint = render(1, 0, 1, 0.4375f, 0.5625f, 9.5f);
	const auto distanceFadeEnd = render(1, 0, 1, 0.4375f, 0.5625f, 10);
	Expect(distanceFadeStart[0] < 8 && distanceFadeMidpoint[0] >= 126 && distanceFadeMidpoint[0] <= 130 &&
	           distanceFadeEnd[0] > 250,
	       ("Farthest cascade did not fade continuously to fully lit at its distance limit: " +
	        std::to_string(distanceFadeStart[0]) + "," + std::to_string(distanceFadeMidpoint[0]) + "," +
	        std::to_string(distanceFadeEnd[0]))
	           .c_str());
}

void TestShadowLightingIntegration(const Rhi::DevicePtr& device, RenderCommandExecutor& executor)
{
	Shader::ShaderUniformMember matrixArrayMember;
	matrixArrayMember.name = "_UrsaDirectionalShadowMatrices";
	matrixArrayMember.type = "float4x4";
	matrixArrayMember.byteSize = 4 * sizeof(Mat4);
	matrixArrayMember.arrayCount = 4;
	matrixArrayMember.arrayStride = sizeof(Mat4);
	matrixArrayMember.matrixStride = sizeof(Float32) * 4;
	matrixArrayMember.spirvRowMajor = true;
	Vector<UInt8> matrixArrayDestination(matrixArrayMember.byteSize, 0);
	Vector<Mat4> matrixArraySource(4, Mat4::Identity());
	Done(Shader::WriteShaderUniformValue(matrixArrayDestination, matrixArrayMember, matrixArraySource.data(),
	                                     matrixArraySource.size() * sizeof(Mat4)));
	Expect(matrixArrayDestination.size() == matrixArraySource.size() * sizeof(Mat4),
	       "Reflected matrix array write changed the uniform block size");
	const auto shortMatrixArray = Shader::WriteShaderUniformValue(matrixArrayDestination, matrixArrayMember,
	                                                              matrixArraySource.data(), 3 * sizeof(Mat4));
	Expect(!shortMatrixArray, "Reflected matrix array accepted a value with the wrong layout size");

	auto engineFiles = Take(OpenEngineFileSystem());
	const auto loadBuiltin = [&](ResourceId id)
	{
		const char* assetPath = Shader::BuiltinShaderAssetPath(id);
		Expect(assetPath != nullptr, "Built-in scene Shader path was not registered for shadow lighting tests");
		auto asset = Take(Shader::LoadShaderAssetResource(engineFiles, Take(VirtualPath::Parse(assetPath))));
		return std::make_shared<Shader::ShaderAsset>(std::move(asset));
	};
	auto litShader = loadBuiltin(Shader::BuiltinPbrShaderId());
	auto unlitShader = loadBuiltin(Shader::BuiltinUnlitShaderId());
	auto atlas = Take(CreateRenderTexture(*device, {8, 8, 2}));
	auto output = Take(CreateRenderTexture(*device, {32, 32, 0, 32}));
	RenderCommand atlasTarget;
	atlasTarget.opcode = RenderCommandOpcode::SetRenderTarget;
	atlasTarget.target.texture = atlas;
	RenderCommand atlasViewport;
	atlasViewport.opcode = RenderCommandOpcode::SetViewport;
	atlasViewport.viewport = {0, 0, 8, 8, 0, 1};
	RenderCommand atlasScissor;
	atlasScissor.opcode = RenderCommandOpcode::SetScissor;
	atlasScissor.scissor = {0, 0, 8, 8};
	RenderCommand atlasClear;
	atlasClear.opcode = RenderCommandOpcode::ClearTarget;
	atlasClear.clearFlags = 2;
	atlasClear.clearDepth = 0;
	Done(executor.Begin());
	Done(executor.ResetCamera());
	Done(executor.Execute({atlasTarget, atlasViewport, atlasScissor, atlasClear}));
	Done(executor.Submit());
	Done(Take(executor.End())->Wait());

	UInt64 frame = 20;
	String evidenceImage;
	const auto render = [&](bool unlit, bool receiveShadows, Float32 shadowStrength, bool extraLighting)
	{
		Scene scene;
		Done(scene.SetAmbientLight({0, 0, 0, 1}, 0));
		auto receiverObject = Take(scene.CreateGameObject("Lit receiver"));
		auto mesh = Take(Test::Cube());
		Done(Take(receiverObject->AddComponent<MeshFilterComponent>())->SetSharedMesh(mesh));
		auto material = std::make_shared<MaterialInstance>(MaterialType::PbrMetallicRoughness);
		Done(material->SetShaderAsset(unlit ? unlitShader : litShader));
		Done(material->SetBaseColor({1, 1, 1, 1}));
		Done(material->SetEmissive(extraLighting ? Color{0.25f, 0, 0, 1} : Color{0, 0, 0, 1}));
		auto renderer = Take(receiverObject->AddComponent<MeshRendererComponent>());
		Done(renderer->SetSharedMaterial(material));
		renderer->SetReceiveShadows(receiveShadows);
		auto sunObject = Take(scene.CreateGameObject("Shadowed Sun"));
		auto sun = Take(sunObject->AddComponent<DirectionalLightComponent>());
		Done(sun->SetColor({1, 1, 1, 1}));
		Done(sun->SetIntensity(4));
		auto pointObject = Take(scene.CreateGameObject("Unshadowed Point"));
		LocalTransform pointTransform;
		pointTransform.position = {0, 0, 3};
		Done(pointObject->SetLocalTransform(pointTransform));
		auto point = Take(pointObject->AddComponent<PointLightComponent>());
		Done(point->SetColor({0, 1, 0, 1}));
		Done(point->SetIntensity(extraLighting ? 8 : 0));
		Done(point->SetRange(10));

		RenderScene snapshot;
		Done(snapshot.Capture(scene, 1));
		CameraData camera;
		camera.position = {0, 0, 5};
		camera.view = Mat4::LookAt(camera.position, {0, 0, 0}, {0, 1, 0});
		camera.projection = Mat4::Perspective(0.8f, 1, 0.1f, 100);
		camera.viewProjection = camera.projection * camera.view;
		camera.inverseViewProjection = camera.viewProjection.Inverse();
		camera.nearPlane = 0.1f;
		camera.farPlane = 100;
		RenderSceneContext renderContext;
		Take(renderContext.Begin(920, frame++, snapshot));
		const CullingResults culling = Take(renderContext.Cull(camera));
		DrawingSettings drawing;
		drawing.shaderTags = {{"UniversalForward"}};
		drawing.sorting.cameraPosition = camera.position;
		drawing.activePipeline = "UrsaUniversal";
		FilteringSettings filtering;
		filtering.maximumQueue = 2500;
		const RendererList list = Take(renderContext.CreateRendererList(culling, drawing, filtering));
		RenderCommand draw;
		draw.opcode = RenderCommandOpcode::DrawRendererList;
		draw.camera = camera;
		draw.lighting = snapshot.Lighting();
		for (const auto& entry : *Take(renderContext.Entries(list)))
		{
			SceneDrawRecord item;
			item.object = snapshot.Objects()[entry.objectIndex];
			item.object.material = entry.material;
			item.subShaderIndex = entry.subShaderIndex;
			item.passIndex = entry.passIndex;
			item.selectedPass = true;
			item.activePipeline = entry.activePipeline;
			item.resolvedPass = entry.resolvedPass;
			draw.draws.push_back(std::move(item));
		}
		Expect(draw.draws.size() == 1, "Lit receiver did not produce one visible UniversalForward draw");
		Mat4 worldToShadow;
		worldToShadow.At(0, 3) = 0.5f;
		worldToShadow.At(1, 3) = 0.5f;
		worldToShadow.At(2, 3) = 0.5f;
		worldToShadow.At(3, 3) = 1;
		Vector<Mat4> shadowMatrices{worldToShadow, Mat4::Identity(), Mat4::Identity(), Mat4::Identity()};
		const Array<Float32, 4> split{100, 100, 100, 100};
		const Array<Float32, 4> texelSize{1.0f / 8, 1.0f / 8, 8, 8};
		const Array<Float32, 4> lightDirection{0, 0, 1, 0};
		const Int32 cascadeCount = 1, soft = 0, mainLightIndex = 0;
		const Float32 bias = 0, normalBias = 0;
		RenderCommand atlasBinding;
		atlasBinding.opcode = RenderCommandOpcode::SetGlobalParameters;
		atlasBinding.global.name = "_UrsaDirectionalShadowAtlas";
		atlasBinding.global.kind = 3;
		atlasBinding.global.texture.texture = atlas;
		Vector<RenderCommand> commands{
		    atlasBinding,
		    Parameter("_UrsaDirectionalShadowMatrices", 8, shadowMatrices.data(), shadowMatrices.size() * sizeof(Mat4)),
		    Parameter("_UrsaDirectionalShadowCascadeSplits", 2, split.data(), sizeof(split)),
		    Parameter("_UrsaDirectionalShadowTexelSize", 2, texelSize.data(), sizeof(texelSize)),
		    Parameter("_UrsaMainLightDirection", 2, lightDirection.data(), sizeof(lightDirection)),
		    Parameter("_UrsaDirectionalShadowCascadeCount", 6, &cascadeCount, sizeof(cascadeCount)),
		    Parameter("_UrsaDirectionalShadowSoft", 6, &soft, sizeof(soft)),
		    Parameter("_UrsaMainLightIndex", 6, &mainLightIndex, sizeof(mainLightIndex)),
		    Parameter("_UrsaDirectionalShadowStrength", 1, &shadowStrength, sizeof(shadowStrength)),
		    Parameter("_UrsaDirectionalShadowBias", 1, &bias, sizeof(bias)),
		    Parameter("_UrsaDirectionalShadowNormalBias", 1, &normalBias, sizeof(normalBias))};
		RenderCommand setTarget;
		setTarget.opcode = RenderCommandOpcode::SetRenderTarget;
		setTarget.target.texture = output;
		RenderCommand viewport;
		viewport.opcode = RenderCommandOpcode::SetViewport;
		viewport.viewport = {0, 0, 32, 32, 0, 1};
		RenderCommand scissor;
		scissor.opcode = RenderCommandOpcode::SetScissor;
		scissor.scissor = {0, 0, 32, 32};
		RenderCommand clear;
		clear.opcode = RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 3;
		clear.clearDepth = 1;
		clear.color = {0, 0, 0, 1};
		commands.insert(commands.begin(), {setTarget, viewport, scissor, clear});
		commands.push_back(std::move(draw));
		Done(executor.Begin(output));
		Done(executor.ResetCamera(camera));
		Done(executor.Execute(commands));
		Done(executor.Submit());
		Done(Take(executor.End())->Wait());
		Done(renderContext.End());
		if (!evidenceImage.empty())
		{
			const auto image = Take(ReadFrameImage(device, output->color, output->colorState));
			Done(WriteFrameImage(std::filesystem::path(URSA_SHADER_STAGING_ROOT) / "ShadowValidation" /
			                         ToStdString(evidenceImage),
			                     image));
		}
		const auto pixels = Take(ReadRenderTexture(*device, *output));
		Array<UInt8, 4> centerPixel{};
		const Size centerOffset = (Size(16) * 32 + 16) * centerPixel.size();
		std::memcpy(centerPixel.data(), pixels.data() + centerOffset, centerPixel.size());
		return centerPixel;
	};

	evidenceImage = "MainLightUnshadowed.png";
	const auto lit = render(false, true, 0, false);
	evidenceImage = "MainLightShadowed.png";
	const auto shadowed = render(false, true, 1, false);
	evidenceImage.clear();
	Expect(lit[0] > 80 && shadowed[0] < 8, "Main light shadows did not attenuate Lit direct illumination");
	const auto shadowedWithOtherLightAndEmission = render(false, true, 1, true);
	Expect(shadowedWithOtherLightAndEmission[0] > shadowed[0] + 20 &&
	           shadowedWithOtherLightAndEmission[1] > shadowed[1] + 20,
	       "Main light shadowing incorrectly removed point-light or emissive contribution");
	const auto receiveDisabled = render(false, false, 1, false);
	Expect(receiveDisabled == lit, "ReceiveShadows=false changed the Lit main-light contribution");
	const auto partialStrength = render(false, true, 0.5f, false);
	Expect(partialStrength[0] > shadowed[0] + 20 && partialStrength[0] < lit[0],
	       "Directional shadow strength did not blend between lit and occluded direct light");
	const auto unlit = render(true, true, 0, false);
	const auto unlitShadowed = render(true, true, 1, true);
	Expect(unlit == unlitShadowed, "Unlit output changed when directional shadow receiving was enabled");
}

RenderCommand Tint()
{
	RenderCommand tint;
	tint.opcode = RenderCommandOpcode::SetGlobalParameters;
	tint.global.name = "_Tint";
	tint.global.kind = 2;
	const Array<Float32, 4> value{0.5f, 1, 1, 1};
	tint.global.bytes.resize(sizeof(value));
	std::memcpy(tint.global.bytes.data(), value.data(), sizeof(value));
	return tint;
}

RenderCommand Parameter(const String& name, UInt32 kind, const void* data, Size size)
{
	RenderCommand command;
	command.opcode = RenderCommandOpcode::SetGlobalParameters;
	command.global.name = name;
	command.global.kind = kind;
	command.global.bytes.resize(size);
	std::memcpy(command.global.bytes.data(), data, size);
	return command;
}
} // namespace

int main()
{
	try
	{
		Rhi::DeviceDesc deviceDesc;
		deviceDesc.validation = true;
		auto device = Take(Rhi::CreateDevice(deviceDesc));
		auto executor = Take(RenderCommandExecutor::Create(device));
		auto bloomExecutor = Take(RenderCommandExecutor::Create(device));
		TestBloomAndFinalPost(device, *bloomExecutor);
		TestPostProcessingColorDomains(device, *bloomExecutor);
		Done(bloomExecutor->Drain());
		auto depthExecutor = Take(RenderCommandExecutor::Create(device));
		TestDepthOnlyAndNormalsPasses(device, *depthExecutor);
		TestColorSnapshotsForInjectedGroups(device, *depthExecutor);
		TestShadowAtlasAndCameraRestore(device, *depthExecutor);
		TestAnimatedMaskedShadowProjection(device, *depthExecutor);
		TestStableCascadeShadowImage(device, *depthExecutor);
		TestMainLightShadowSampling(device, *depthExecutor);
		TestShadowLightingIntegration(device, *depthExecutor);
		Done(depthExecutor->Drain());
		auto output = Take(CreateRenderTexture(*device, {8, 8}));
		auto copied = Take(CreateRenderTexture(*device, {8, 8}));
		auto atomicExecutor = Take(RenderCommandExecutor::Create(device));
		RenderCommand initialColor;
		initialColor.opcode = RenderCommandOpcode::ClearTarget;
		initialColor.clearFlags = 1;
		initialColor.color = {0, 1, 0, 1};
		Done(atomicExecutor->Begin(output));
		Done(atomicExecutor->Execute({initialColor}));
		Done(atomicExecutor->Submit());
		Done(Take(atomicExecutor->End())->Wait());
		RenderCommand rejectedColor = initialColor;
		rejectedColor.color = {1, 0, 0, 1};
		const Array<Float32, 2> shortVectorArray{1, 2};
		RenderCommand malformedArray =
		    Parameter("_MalformedVectorArray", 9, shortVectorArray.data(), sizeof(shortVectorArray));
		Done(atomicExecutor->Begin(output));
		Expect(!atomicExecutor->Execute({rejectedColor, malformedArray}),
		       "Global vector array below the minimum element size bypassed batch preflight");
		RenderCommand viewportAfterRejectedBatch;
		viewportAfterRejectedBatch.opcode = RenderCommandOpcode::SetViewport;
		viewportAfterRejectedBatch.viewport = {0, 0, 8, 8, 0, 1};
		Done(atomicExecutor->Execute({viewportAfterRejectedBatch}));
		Done(atomicExecutor->Submit());
		Done(Take(atomicExecutor->End())->Wait());
		Expect(ReadPixel(device, output) == Array<UInt8, 4>{0, 255, 0, 255},
		       "An invalid global array partially executed earlier commands from its batch");
		Done(atomicExecutor->Drain());
		Expect(!CreateRenderTexture(*device, {0, 8}) && !CreateRenderTexture(*device, {8, 8, 0, 0, false, 4}) &&
		           !CreateRenderTexture(*device, {device->Capabilities().maxTextureSize + 1, 8}),
		       "Unsupported target descriptor was accepted");
		Expect(!CreateGraphicsBuffer(*device, 4, 0, 16), "Empty graphics buffer was accepted");
		auto material = CompositionMaterial();
		const Float32 intensity = 1;
		const Int32 mode = 3;
		const Array<Float32, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
		for (UInt32 frame = 0; frame < 3; ++frame)
		{
			Done(executor->Begin(output));
			RenderCommand allocate;
			allocate.opcode = RenderCommandOpcode::GetTemporaryTarget;
			allocate.target.id = 17;
			allocate.description = {8, 8, 0, 32};
			RenderCommand target;
			target.opcode = RenderCommandOpcode::SetRenderTarget;
			target.target.id = 17;
			RenderCommand clear;
			clear.opcode = RenderCommandOpcode::ClearTarget;
			clear.clearFlags = 3;
			clear.color = {1, 0.5f, 0.25f, 1};
			RenderCommand blit;
			blit.opcode = RenderCommandOpcode::Blit;
			blit.source.id = 17;
			blit.material = material;
			RenderCommand release;
			release.opcode = RenderCommandOpcode::ReleaseTemporaryTarget;
			release.target.id = 17;
			Vector<RenderCommand> commands{allocate,
			                               target,
			                               clear,
			                               Tint(),
			                               Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
			                               Parameter("_Mode", 6, &mode, sizeof(mode)),
			                               Parameter("_Transform", 5, matrix.data(), sizeof(matrix)),
			                               blit,
			                               release};
			Done(executor->Execute(commands));
			commands.clear();
			Expect(!executor->End(), "Unsubmitted commands were accepted at frame end");
			Done(executor->Submit());
			Expect(executor->Statistics().physicalSubmissions == frame,
			       "Logical Submit performed a physical GPU submission");
			Done(Take(executor->End())->Wait());
			const auto pixel = ReadPixel(device, output);
			Expect(pixel[0] >= 126 && pixel[0] <= 129 && pixel[1] >= 126 && pixel[1] <= 129 && pixel[2] >= 62 &&
			           pixel[2] <= 65 && pixel[3] == 255,
			       "Offscreen Blit and global parameters produced incorrect pixels");
		}
		Expect(executor->Statistics().temporaryTargetsCreated == 1 &&
		           executor->Statistics().temporaryTargetsReused == 2 && executor->Statistics().pipelinesCreated == 1,
		       "Temporary target or fullscreen pipeline was not reused");
		Done(executor->Begin());
		RenderCommand copy;
		copy.opcode = RenderCommandOpcode::CopyTexture;
		copy.source.texture = output;
		copy.target.texture = copied;
		Done(executor->Execute({copy}));
		Done(executor->Submit());
		Done(Take(executor->End())->Wait());
		Expect(ReadPixel(device, copied) == ReadPixel(device, output), "Texture-to-texture copy changed the pixels");
		Done(executor->Begin(copied));
		RenderCommand stale;
		stale.opcode = RenderCommandOpcode::SetRenderTarget;
		stale.target.id = 17;
		Expect(!executor->Execute({stale}), "Temporary target ID leaked into another context");
		RenderCommand feedback;
		feedback.opcode = RenderCommandOpcode::SetGlobalParameters;
		feedback.global.kind = 3;
		feedback.global.name = "_Source";
		feedback.global.texture.texture = copied;
		RenderCommand fullscreen;
		fullscreen.opcode = RenderCommandOpcode::DrawFullscreen;
		fullscreen.material = material;
		Done(executor->Execute({feedback, Tint(), fullscreen}));
		Done(executor->Submit());
		const auto rejected = executor->End();
		Expect(!rejected && rejected.error.find("writable attachments") != String::npos,
		       "Fullscreen sampling feedback was not rejected");
		Done(executor->Begin(output));
		RenderCommand clear;
		clear.opcode = RenderCommandOpcode::ClearTarget;
		clear.clearFlags = 1;
		clear.color = {0, 1, 0, 1};
		Done(executor->Execute({clear}));
		Done(executor->Submit());
		clear.color = {1, 0, 0, 1};
		Done(executor->Execute({clear}));
		Done(Take(executor->Abort())->Wait());
		Expect(ReadPixel(device, output)[1] == 255,
		       "Aborting discarded a submitted segment or executed an unsubmitted segment");
		auto hdr = Take(CreateRenderTexture(*device, {8, 8, 1}));
		Done(executor->Begin(hdr));
		clear.color = {1, 0.5f, 0.25f, 1};
		Done(executor->Execute({clear}));
		Done(executor->Submit());
		Done(Take(executor->End())->Wait());
		const auto hdrBytes = Take(ReadRenderTexture(*device, *hdr));
		const Array<UInt16, 4> expectedHdr{0x3c00, 0x3800, 0x3400, 0x3c00};
		Expect(hdrBytes.size() == 8u * 8u * 8u &&
		           std::memcmp(hdrBytes.data(), expectedHdr.data(), sizeof(expectedHdr)) == 0,
		       "HDR target clear/readback changed its half-float format");
		auto firstAttachment = Take(CreateRenderTexture(*device, {8, 8}));
		auto secondAttachment = Take(CreateRenderTexture(*device, {8, 8}));
		auto depthAttachment = Take(CreateRenderTexture(*device, {8, 8, 2}));
		Done(executor->Begin());
		RenderCommand attachments;
		attachments.opcode = RenderCommandOpcode::SetRenderTargets;
		attachments.colors = {{firstAttachment, 0}, {secondAttachment, 0}};
		attachments.hasDepthTarget = true;
		attachments.depthTarget.texture = depthAttachment;
		RenderCommand sourceParameter;
		sourceParameter.opcode = RenderCommandOpcode::SetGlobalParameters;
		sourceParameter.global.kind = 3;
		sourceParameter.global.name = "_Source";
		sourceParameter.global.texture.texture = output;
		fullscreen.material = CompositionMaterial(true);
		clear.clearFlags = 3;
		clear.color = {0, 0, 0, 1};
		clear.clearDepth = 0.25f;
		Done(executor->Execute({attachments, clear, sourceParameter, Tint(),
		                        Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
		                        Parameter("_Mode", 6, &mode, sizeof(mode)),
		                        Parameter("_Transform", 5, matrix.data(), sizeof(matrix)), fullscreen}));
		Done(executor->Submit());
		Done(Take(executor->End())->Wait());
		Expect(ReadPixel(device, firstAttachment) == Array<UInt8, 4>{0, 255, 0, 255} &&
		           ReadPixel(device, secondAttachment) == Array<UInt8, 4>{0, 0, 255, 255},
		       "Fullscreen rendering did not write both independent color attachments");
		const auto depthBytes = Take(ReadRenderTexture(*device, *depthAttachment));
		Float32 depthPixel = 0;
		std::memcpy(&depthPixel, depthBytes.data(), sizeof(depthPixel));
		Expect(depthPixel == 0.25f, "Multiple attachment target did not clear its selected depth attachment");
		Done(executor->Begin());
		attachments.colors = {{firstAttachment, 0}, {firstAttachment, 0}};
		Expect(!executor->Execute({attachments}), "Duplicate color attachments were accepted");
		attachments.colors.resize(device->Capabilities().maxColorAttachments + 1, {firstAttachment, 0});
		Expect(!executor->Execute({attachments}), "Color attachment count exceeded the device limit");
		Take(executor->Abort());
		auto sourceBuffer = Take(CreateGraphicsBuffer(*device, 4, 4, 4));
		auto destinationBuffer = Take(CreateGraphicsBuffer(*device, 4, 8, 4));
		const Array<UInt32, 4> values{5, 17, 29, 41};
		auto upload =
		    Take(device->CreateBuffer({sizeof(values), Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload}));
		Done(upload->Write(0, values.data(), sizeof(values)));
		auto uploadCommand = Take(device->CreateCommandList());
		Done(uploadCommand->Begin());
		Done(uploadCommand->Barrier(upload, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferSource));
		Done(uploadCommand->Barrier(sourceBuffer->buffer, Rhi::ResourceState::Undefined,
		                            Rhi::ResourceState::TransferDestination));
		Done(uploadCommand->CopyBuffer(upload, sourceBuffer->buffer, sizeof(values)));
		Done(uploadCommand->End());
		Done(Take(device->Submit(uploadCommand))->Wait());
		sourceBuffer->state = Rhi::ResourceState::TransferDestination;
		Done(executor->Begin());
		RenderCommand bufferCopy;
		bufferCopy.opcode = RenderCommandOpcode::CopyBuffer;
		bufferCopy.sourceBuffer = sourceBuffer;
		bufferCopy.targetBuffer = destinationBuffer;
		bufferCopy.sourceOffset = 4;
		bufferCopy.targetOffset = 8;
		bufferCopy.byteSize = 8;
		Done(executor->Execute({bufferCopy}));
		bufferCopy.byteSize = 1024;
		Expect(!executor->Execute({bufferCopy}), "Out-of-range buffer copy was accepted");
		Done(executor->Submit());
		Done(Take(executor->End())->Wait());
		const auto bufferBytes = Take(ReadGraphicsBuffer(*device, *destinationBuffer, 8, 8));
		Expect(std::memcmp(bufferBytes.data(), values.data() + 1, 8) == 0, "GraphicsBuffer copy offsets were ignored");
		Done(executor->Begin());
		RenderCommand allocation;
		allocation.opcode = RenderCommandOpcode::GetTemporaryTarget;
		allocation.description = {8, 8, 0, 32};
		allocation.target.id = 17;
		Done(executor->Execute({allocation}));
		Done(executor->Submit());
		Expect(!executor->End(), "Unreleased temporary target escaped the context");
		RenderCommand release;
		release.opcode = RenderCommandOpcode::ReleaseTemporaryTarget;
		release.target.id = 17;
		Done(executor->Execute({release}));
		Done(executor->Submit());
		Take(executor->End());
		Done(executor->Begin());
		Vector<RenderCommand> excessiveTargets;
		for (UInt32 id = 1; id <= 33; ++id)
		{
			allocation.target.id = id;
			excessiveTargets.push_back(allocation);
		}
		Expect(!executor->Execute(excessiveTargets), "Temporary target context exceeded its bounded pool capacity");
		Take(executor->Abort());
		Done(executor->Drain());
		auto computeShader =
		    Take(Test::CreateComputeFixture({URSA_TEST_DXC_EXECUTABLE, URSA_TEST_SPIRV_VAL_EXECUTABLE,
		                                     URSA_TEST_SPIRV_CROSS_EXECUTABLE, URSA_SHADER_STAGING_ROOT}));
		const UInt32 kernel = Take(FindComputeKernel(*computeShader, "Fill"));
		Expect(kernel == 0 && !FindComputeKernel(*computeShader, "Missing") && !ComputeKernelAt(*computeShader, 99),
		       "Compute kernel lookup accepted a missing name or index");
		auto computeTexture = Take(CreateRenderTexture(*device, {8, 8, 0, 0, true}));
		auto computeBuffer = Take(CreateGraphicsBuffer(*device, 4, 64, 4));
		RenderCommand computeOutput;
		computeOutput.opcode = RenderCommandOpcode::SetComputeParameters;
		computeOutput.shader = computeShader;
		computeOutput.kernel = kernel;
		computeOutput.global.name = "_Output";
		computeOutput.global.kind = 4;
		computeOutput.global.texture.texture = computeTexture;
		RenderCommand computeData = computeOutput;
		computeData.global = {};
		computeData.global.name = "_Data";
		computeData.global.kind = 7;
		computeData.global.buffer = computeBuffer;
		RenderCommand dispatch;
		dispatch.opcode = RenderCommandOpcode::DispatchCompute;
		dispatch.shader = computeShader;
		dispatch.kernel = kernel;
		RenderCommand composition;
		composition.opcode = RenderCommandOpcode::Blit;
		composition.source.texture = computeTexture;
		composition.material = material;
		for (UInt32 frame = 0; frame < 3; ++frame)
		{
			const Array<Float32, 4> color{0.5f + Float32(frame) * 0.125f, 0.25f, 0.5f, 1};
			const Int32 bias = Int32(frame) + 7;
			RenderCommand colorParameter = Parameter("_Color", 2, color.data(), sizeof(color));
			colorParameter.opcode = RenderCommandOpcode::SetComputeParameters;
			colorParameter.shader = computeShader;
			RenderCommand biasParameter = Parameter("_Bias", 6, &bias, sizeof(bias));
			biasParameter.opcode = RenderCommandOpcode::SetComputeParameters;
			biasParameter.shader = computeShader;
			Done(executor->Begin(output));
			const UInt64 physical = executor->Statistics().physicalSubmissions;
			Done(executor->Execute({computeOutput, computeData, colorParameter, biasParameter, dispatch}));
			Done(executor->Submit());
			Done(executor->Execute({Tint(), Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
			                        Parameter("_Mode", 6, &mode, sizeof(mode)),
			                        Parameter("_Transform", 5, matrix.data(), sizeof(matrix)), composition}));
			Done(executor->Submit());
			Expect(executor->Statistics().physicalSubmissions == physical,
			       "Logical compute Submit submitted or waited prematurely");
			Done(Take(executor->End())->Wait());
			Expect(executor->Statistics().physicalSubmissions == physical + 1,
			       "Compute/graphics logical segments were not merged into one physical submission");
			const auto pixel = ReadPixel(device, output);
			Expect(pixel[0] >= 62 + frame * 16 && pixel[0] <= 66 + frame * 16 && pixel[1] >= 62 && pixel[1] <= 66 &&
			           pixel[2] >= 126 && pixel[2] <= 129 && pixel[3] == 255,
			       "Graphics did not sample this frame's compute texture output");
			const auto bytes = Take(ReadGraphicsBuffer(*device, *computeBuffer, 0, 64 * 4));
			Array<UInt32, 64> words{};
			std::memcpy(words.data(), bytes.data(), bytes.size());
			for (UInt32 index = 0; index < 64; ++index)
			{
				Expect(words[index] == index + UInt32(bias),
				       "Compute buffer output or per-frame integer parameters were stale");
			}
		}
		Done(executor->Begin());
		RenderCommand invalidDispatch = dispatch;
		invalidDispatch.groups[0] = device->Capabilities().maxComputeWorkGroupCount[0] + 1;
		Expect(!executor->Execute({invalidDispatch}), "Compute dispatch dimensions exceeded device limits");
		invalidDispatch = dispatch;
		invalidDispatch.kernel = 99;
		Expect(!executor->Execute({invalidDispatch}), "Compute dispatch accepted an absent kernel");
		computeOutput.global.texture.texture = copied;
		Expect(!executor->Execute({computeOutput}), "Compute output accepted a texture without storage usage");
		Take(executor->Abort());
		Done(executor->Begin());
		computeOutput.global.texture.texture = computeTexture;
		Done(executor->Execute({computeOutput, dispatch}));
		Done(executor->Submit());
		const auto missingBinding = executor->End();
		Expect(!missingBinding && missingBinding.error.find("required resource parameter") != String::npos,
		       "Compute execution accepted a missing reflected buffer binding");
		Done(executor->Drain());
		Done(executor->Begin(output));
		Done(executor->ResetCamera());
		RenderCommand viewport;
		viewport.opcode = RenderCommandOpcode::SetViewport;
		viewport.viewport = {0, 0, 4, 8, 0, 1};
		RenderCommand scissor;
		scissor.opcode = RenderCommandOpcode::SetScissor;
		scissor.scissor = {0, 0, 4, 8};
		const Array<Float32, 4> suppressedRed{0, 1, 1, 1};
		RenderCommand firstTint = Parameter("_Tint", 2, suppressedRed.data(), sizeof(suppressedRed));
		RenderCommand changeTarget;
		changeTarget.opcode = RenderCommandOpcode::SetRenderTarget;
		changeTarget.target.texture = copied;
		composition.source.texture = copied;
		clear.clearFlags = 1;
		clear.color = {0, 0, 0, 1};
		Done(executor->Execute({clear, viewport, scissor, firstTint,
		                        Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
		                        Parameter("_Mode", 6, &mode, sizeof(mode)),
		                        Parameter("_Transform", 5, matrix.data(), sizeof(matrix)), composition, changeTarget}));
		Expect(!executor->ResetCamera(), "Camera setup discarded a previous unsubmitted segment");
		Done(executor->Submit());
		Done(executor->ResetCamera());
		sourceParameter.global.texture.texture = copied;
		fullscreen.material = material;
		Done(executor->Execute({sourceParameter, Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
		                        Parameter("_Mode", 6, &mode, sizeof(mode)),
		                        Parameter("_Transform", 5, matrix.data(), sizeof(matrix)), fullscreen}));
		Done(executor->Submit());
		Done(Take(executor->End())->Wait());
		const auto cameraBytes = Take(ReadRenderTexture(*device, *output));
		Expect(cameraBytes[0] >= 126 && cameraBytes[0] <= 129 && cameraBytes[7 * 4] >= 126 && cameraBytes[7 * 4] <= 129,
		       "Second camera inherited a global parameter, viewport, scissor or render target from the first camera");
		Done(executor->Drain());
		auto window = Take(Window::Create(640, 480, "Ursa logical submit window test", false));
		Rhi::DeviceDesc windowDescription;
		windowDescription.window = window;
#ifndef NDEBUG
		windowDescription.validation = true;
#endif
		auto windowDevice = Take(Rhi::CreateDevice(windowDescription));
		auto swapchain = Take(windowDevice->CreateSwapchain({}));
		auto windowExecutor = Take(RenderCommandExecutor::Create(windowDevice));
		auto intermediate = Take(CreateRenderTexture(*windowDevice, {8, 8}));
		for (UInt32 frame = 0; frame < 3; ++frame)
		{
			if (frame == 1)
			{
				window->Resize(800, 600);
				window->PollEvents();
			}
			Done(windowExecutor->Begin({}, Rhi::ResourceState::Present, swapchain));
			Expect(swapchain->Statistics().acquisitions == frame + 1 && swapchain->Statistics().presentations == frame,
			       "Window frame acquired or presented more than one swapchain image");
			RenderCommand offscreenTarget;
			offscreenTarget.opcode = RenderCommandOpcode::SetRenderTarget;
			offscreenTarget.target.texture = intermediate;
			clear.color = {0.5f, 0.5f, 0.25f, 1};
			Done(windowExecutor->Execute({offscreenTarget, clear}));
			Done(windowExecutor->Submit());
			RenderCommand windowComposition;
			windowComposition.opcode = RenderCommandOpcode::Blit;
			windowComposition.source.texture = intermediate;
			windowComposition.material = material;
			Done(windowExecutor->Execute({Tint(), Parameter("_Intensity", 1, &intensity, sizeof(intensity)),
			                              Parameter("_Mode", 6, &mode, sizeof(mode)),
			                              Parameter("_Transform", 5, matrix.data(), sizeof(matrix)),
			                              windowComposition}));
			Done(windowExecutor->Submit());
			Expect(swapchain->Statistics().presentations == frame,
			       "An offscreen logical Submit consumed window present synchronization");
			Done(Take(windowExecutor->End())->Wait());
			Expect(swapchain->Statistics().acquisitions == frame + 1 &&
			           swapchain->Statistics().presentations == frame + 1 &&
			           windowExecutor->Statistics().physicalSubmissions == frame + 1,
			       "Window context did not perform one physical submission and presentation");
		}
		Expect(swapchain->Statistics().rebuilds >= 2, "Window resize did not rebuild the swapchain");
		Done(windowExecutor->Begin({}, Rhi::ResourceState::Present, swapchain));
		Expect(!windowExecutor->Begin({}, Rhi::ResourceState::Present, swapchain) && windowExecutor->IsActive(),
		       "Reentrant context begin aborted the existing acquired window frame");
		const auto abortedWindow = Take(windowExecutor->Abort());
		if (abortedWindow)
		{
			Done(abortedWindow->Wait());
		}
		Expect(swapchain->Statistics().acquisitions == 4 && swapchain->Statistics().presentations == 4,
		       "Aborted window frame leaked or repeated acquisition/presentation synchronization");
		Done(windowExecutor->Drain());
		auto sceneTarget = Take(CreateRenderTexture(*device, {64, 64, 0, 32}));
		RenderCommand meshDraw;
		meshDraw.opcode = RenderCommandOpcode::DrawMesh;
		SceneDrawRecord cubeDraw;
		cubeDraw.selectedPass = true;
		cubeDraw.object.mesh = Take(Test::Cube());
		cubeDraw.object.meshSnapshot = cubeDraw.object.mesh->Snapshot();
		cubeDraw.object.material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Done(cubeDraw.object.material->SetBaseColor({1, 0, 0, 1}));
		cubeDraw.object.parameters = cubeDraw.object.material->Parameters();
		meshDraw.draws.push_back(cubeDraw);
		Camera drawCamera;
		drawCamera.position = {0, 0, 5};
		drawCamera.target = {};
		meshDraw.camera.viewProjection = drawCamera.ViewProjection(1);
		meshDraw.camera.position = drawCamera.position;
		for (UInt32 frame = 0; frame < 3; ++frame)
		{
			Done(executor->Begin(sceneTarget));
			Done(executor->ResetCamera());
			clear.clearFlags = 3;
			clear.clearDepth = 1;
			clear.color = {0, 0, 0, 1};
			Done(executor->Execute({clear, meshDraw}));
			Done(executor->Submit());
			Done(Take(executor->End())->Wait());
			const auto pixels = Take(ReadRenderTexture(*device, *sceneTarget));
			const Size center = (32 * 64 + 32) * 4;
			Expect(pixels[center] > 240 && pixels[center + 1] < 15 && pixels[center + 2] < 15,
			       "Scene draw expansion, selected material or repeated depth state produced incorrect pixels");
		}
		Done(executor->Begin(sceneTarget));
		Done(executor->Execute({clear, meshDraw}));
		Done(cubeDraw.object.material->SetBaseColor({0, 0, 1, 1}));
		Done(executor->Submit());
		Done(Take(executor->End())->Wait());
		const auto snapshotPixels = Take(ReadRenderTexture(*device, *sceneTarget));
		Expect(snapshotPixels[(32 * 64 + 32) * 4] > 240,
		       "Material mutation after Execute changed an already accepted scene draw");
		Done(executor->Drain());
		Done(device->WaitIdle());
		std::cout << "Render command executor GPU tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
