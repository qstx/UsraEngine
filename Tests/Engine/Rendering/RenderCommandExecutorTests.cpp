#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Rendering/Shader/ComputeKernel.hpp>
#include <ComputeFixtures.hpp>
#include <MeshFixtures.hpp>
#include <Shader/ShaderCompiler.hpp>
#include <Core/StringInterop.hpp>
#include <cstring>
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
#ifndef NDEBUG
		deviceDesc.validation = true;
#endif
		auto device = Take(Rhi::CreateDevice(deviceDesc));
		auto executor = Take(RenderCommandExecutor::Create(device));
		auto output = Take(CreateRenderTexture(*device, {8, 8}));
		auto copied = Take(CreateRenderTexture(*device, {8, 8}));
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
		std::cout << "Render command executor GPU tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
