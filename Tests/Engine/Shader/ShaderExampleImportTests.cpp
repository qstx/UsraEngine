#include <Shader/ShaderCompiler.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Shader/ShaderImporter.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <Core/StringInterop.hpp>
#include <MeshFixtures.hpp>
#include <RenderingFixtures.hpp>
#include <Rendering/Pipeline/NativeForwardPipeline.hpp>
#include <Rhi/RHI.hpp>
#include <Scene/RenderingComponents.hpp>
#include <Serialization/ResourceSession.hpp>
#include <Vfs/AssetRoots.hpp>
#include <Vfs/MemoryFileProvider.hpp>
#include <Vfs/PakArchive.hpp>
#include <Vfs/PakBuilder.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Shader;

template <class T> T Take(Result<T> result);
void Done(Result<void> result);

void ConfigureBuiltinShaderResources()
{
	ConfigureEngineFileSystem(std::make_shared<VirtualFileSystem>(Take(OpenEngineFileSystem())));
}

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

void CollectSharedBlocks(const ShaderSourceDocument& source, Vector<ProgramBlock>& blocks)
{
	for (const ProgramBlock& block : source.includes)
	{
		blocks.push_back(block);
	}
	for (const SubShaderDeclaration& subShader : source.subShaders)
	{
		for (const ProgramBlock& block : subShader.includes)
		{
			blocks.push_back(block);
		}
	}
}

void RenderReflectedTexture(const ShaderVariantAsset& variant, const ShaderBindingLayout& bindings,
                            const Vector<UInt8>& uniformBytes, const MaterialTexturePtr& sourceTexture)
{
	Rhi::DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = Take(Rhi::CreateDevice(description));
	Rhi::ShaderPtr vertexShader;
	Rhi::ShaderPtr fragmentShader;
	for (const ShaderStageAsset& stage : variant.stages)
	{
		if (stage.stage == ProgramStage::Vertex)
		{
			vertexShader = Take(device->CreateShader(
			    {Rhi::ShaderStage::Vertex, Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint}));
		}
		else if (stage.stage == ProgramStage::Fragment)
		{
			fragmentShader = Take(device->CreateShader(
			    {Rhi::ShaderStage::Fragment, Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint}));
		}
	}
	Expect(vertexShader && fragmentShader, "Imported Shader variant is missing a graphics stage");
	auto layout = Take(CreateShaderBindGroupLayout(bindings, *device));
	Ursa::UInt32 objectBinding = 0;
	Ursa::UInt32 objectBufferSize = 0;
	Ursa::UInt32 materialBinding = 0;
	const ShaderUniformBufferLayout* objectUniform = nullptr;
	for (const ShaderUniformBufferLayout& uniform : bindings.uniformBuffers)
	{
		if (uniform.binding == 0)
		{
			objectBinding = uniform.binding;
			objectBufferSize = uniform.byteSize;
			objectUniform = &uniform;
		}
		else
		{
			materialBinding = uniform.binding;
		}
	}
	Expect(objectBufferSize != 0 && objectUniform && materialBinding != 0,
	       "UrsaPerObject or material uniform block was not reflected");
	Vector<UInt8> objectData(objectBufferSize, 0);
	Array<Float32, 16> identity{};
	for (Ursa::Size diagonal = 0; diagonal < 4; ++diagonal)
	{
		identity[diagonal * 5] = 1.0f;
	}
	for (const ShaderUniformMember& member : objectUniform->members)
	{
		if (member.type == "mat4" || member.type == "float4x4")
		{
			Done(WriteShaderUniformValue(objectData, member, identity.data(), member.byteSize));
		}
	}
	auto objectBuffer = Take(device->CreateBuffer({objectBufferSize, Rhi::BufferUsage::Uniform, Rhi::Memory::Upload}));
	Done(objectBuffer->Write(0, objectData.data(), objectData.size()));
	auto materialBuffer =
	    Take(device->CreateBuffer({uniformBytes.size(), Rhi::BufferUsage::Uniform, Rhi::Memory::Upload}));
	Done(materialBuffer->Write(0, uniformBytes.data(), uniformBytes.size()));
	const Rhi::Format textureFormat =
	    sourceTexture->GetColorSpace() == TextureColorSpace::Srgb ? Rhi::Format::RgbA8Srgb : Rhi::Format::RgbA8Unorm;
	auto texture = Take(device->CreateTexture({sourceTexture->GetWidth(), sourceTexture->GetHeight(), textureFormat,
	                                           Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination}));
	auto textureUpload = Take(device->CreateBuffer(
	    {sourceTexture->GetPixels().size(), Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload}));
	Done(textureUpload->Write(0, sourceTexture->GetPixels().data(), sourceTexture->GetPixels().size()));
	auto sampler = Take(device->CreateSampler({false, false}));
	const auto textureBinding = bindings.properties.find("_BaseMap");
	Expect(textureBinding != bindings.properties.end(), "Reflected base texture binding was not found");
	Ursa::UInt32 samplerBinding = 0;
	for (const Rhi::LayoutBinding& binding : bindings.descriptors)
	{
		if (binding.type == Rhi::BindingType::Sampler)
		{
			samplerBinding = binding.binding;
		}
	}
	Expect(samplerBinding != 0, "Reflected sampler binding is missing");
	auto group = Take(device->CreateBindGroup({layout,
	                                           {{objectBinding, objectBuffer, {}, {}},
	                                            {materialBinding, materialBuffer, {}, {}},
	                                            {textureBinding->second.binding, {}, texture, {}},
	                                            {samplerBinding, {}, {}, sampler}}}));
	Rhi::GraphicsPipelineDesc pipelineDescription;
	pipelineDescription.vertex = vertexShader;
	pipelineDescription.fragment = fragmentShader;
	pipelineDescription.layout = layout;
	pipelineDescription.colorFormat = Rhi::Format::RgbA8Unorm;
	pipelineDescription.vertexStride = sizeof(Float32) * 5;
	pipelineDescription.attributes = {{0, 0, Rhi::VertexFormat::Float3},
	                                  {1, sizeof(Float32) * 3, Rhi::VertexFormat::Float2}};
	auto pipeline = Take(device->CreateGraphicsPipeline(pipelineDescription));
	const Array<Float32, 15> vertices{
	    {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 3.0f, -1.0f, 0.0f, 2.0f, 0.0f, -1.0f, 3.0f, 0.0f, 0.0f, 2.0f}};
	auto vertexBuffer = Take(device->CreateBuffer({sizeof(vertices), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload}));
	Done(vertexBuffer->Write(0, vertices.data(), sizeof(vertices)));
	auto color = Take(device->CreateTexture(
	    {1, 1, Rhi::Format::RgbA8Unorm, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource}));
	auto readback = Take(device->CreateBuffer({4, Rhi::BufferUsage::TransferDestination, Rhi::Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
	Done(command->CopyBufferToTexture(textureUpload, texture));
	Done(command->Barrier(texture, Rhi::ResourceState::TransferDestination, Rhi::ResourceState::ShaderRead));
	Done(command->Barrier(color, Rhi::ResourceState::Undefined, Rhi::ResourceState::ColorAttachment));
	Rhi::RenderTarget target;
	target.color = color;
	Done(command->BeginRendering(target));
	Done(command->BindPipeline(pipeline));
	Done(command->BindGroup(group));
	Done(command->BindVertexBuffer(vertexBuffer));
	Done(command->Draw(3));
	Done(command->EndRendering());
	Done(command->Barrier(color, Rhi::ResourceState::ColorAttachment, Rhi::ResourceState::TransferSource));
	Done(command->Barrier(readback, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(color, readback));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	Array<UInt8, 4> pixels{};
	Done(readback->Read(0, pixels.data(), pixels.size()));
	Expect(pixels[0] == 0 && pixels[1] >= 120 && pixels[1] <= 136 && pixels[2] == 0 && pixels[3] == 255,
	       "Shader reflection texture binding did not sample the expected texel");
	Done(device->WaitIdle());
}

ShaderStageAsset CompileTestStage(const DxcShaderCompiler& compiler, ProgramStage stage, const String& entryPoint,
                                  const String& source)
{
	ShaderCompileRequest request;
	request.sourcePath = Take(VirtualPath::Parse("prj:Tests/ShaderPasses.ursashader"));
	request.entryPoint = {stage, entryPoint, {}};
	request.source = source;
	const auto compiled = Take(compiler.Compile(request));
	return {ShaderTarget::VulkanSpirv, compiled.stage, compiled.entryPoint, compiled.spirv, compiled.reflectionJson};
}

ShaderAsset BuildPassOrderShader(const DxcShaderCompiler& compiler)
{
	ShaderAsset shader;
	shader.id = Take(ResourceId::Parse("5403c8ea-669d-4b64-8c30-6d63d128de01"));
	shader.name = "Tests/PassOrder";
	ShaderSubShaderAsset incompatible;
	incompatible.tags.push_back({"RenderPipeline", "UrsaDeferred"});
	ShaderPassAsset deferred;
	deferred.name = "DeferredOnly";
	deferred.tags.push_back({"LightMode", "Forward"});
	ShaderVariantAsset deferredVariant;
	deferredVariant.stages.push_back(
	    CompileTestStage(compiler, ProgramStage::Vertex, "VSMain",
	                     "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }"));
	deferredVariant.stages.push_back(CompileTestStage(compiler, ProgramStage::Fragment, "PSMain",
	                                                  "float4 PSMain() : SV_Target0 { return float4(1, 0, 1, 1); }"));
	deferred.variants.push_back(std::move(deferredVariant));
	incompatible.passes.push_back(std::move(deferred));
	shader.subShaders.push_back(std::move(incompatible));

	ShaderSubShaderAsset forward;
	forward.tags.push_back({"RenderPipeline", "UrsaUniversal"});
	const String vertex = "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }";
	const auto addPass = [&](const String& name, const String& lightMode, const String& fragmentSource,
	                         Vector<RenderStateDeclaration> states)
	{
		ShaderPassAsset pass;
		pass.name = name;
		pass.tags.push_back({"LightMode", lightMode});
		pass.renderStates = std::move(states);
		ShaderVariantAsset variant;
		variant.stages.push_back(CompileTestStage(compiler, ProgramStage::Vertex, "VSMain", vertex));
		variant.stages.push_back(CompileTestStage(compiler, ProgramStage::Fragment, "PSMain", fragmentSource));
		pass.variants.push_back(std::move(variant));
		forward.passes.push_back(std::move(pass));
		forward.passOrder.push_back({static_cast<UInt32>(forward.passes.size() - 1), {}, false});
	};
	addPass("Shadow", "ShadowCaster", "float4 PSMain() : SV_Target0 { return float4(0, 0, 1, 1); }", {});
	addPass("Base", "Forward", "float4 PSMain() : SV_Target0 { return float4(1, 0, 0, 1); }",
	        {{"Cull", "Off", {}}, {"ZTest", "Always", {}}, {"ZWrite", "On", {}}});
	addPass("Overlay", "SRPDefaultUnlit", "float4 PSMain() : SV_Target0 { return float4(0, 1, 0, 0.5); }",
	        {{"Cull", "Off", {}},
	         {"ZTest", "Always", {}},
	         {"ZWrite", "Off", {}},
	         {"Blend", "SrcAlpha OneMinusSrcAlpha", {}}});
	shader.subShaders.push_back(std::move(forward));
	return shader;
}

class CountingDevice final : public Rhi::Device
{
public:
	explicit CountingDevice(Rhi::DevicePtr selectedDevice) : device(std::move(selectedDevice))
	{
	}

	Rhi::AdapterInfo AdapterInfo() const override
	{
		return device->AdapterInfo();
	}

	Rhi::Capabilities Capabilities() const override
	{
		return device->Capabilities();
	}

	Result<Rhi::FormatCapabilities> QueryFormat(Rhi::Format format) const override
	{
		return device->QueryFormat(format);
	}

	Result<Rhi::BufferPtr> CreateBuffer(const Rhi::BufferDesc& desc) override
	{
		if (Rhi::Has(desc.usage, Rhi::BufferUsage::Storage))
		{
			++storageBufferCreates;
		}
		return device->CreateBuffer(desc);
	}

	Result<Rhi::TexturePtr> CreateTexture(const Rhi::TextureDesc& desc) override
	{
		return device->CreateTexture(desc);
	}

	Result<Rhi::SamplerPtr> CreateSampler(const Rhi::SamplerDesc& desc) override
	{
		return device->CreateSampler(desc);
	}

	Result<Rhi::ShaderPtr> CreateShader(const Rhi::ShaderDesc& desc) override
	{
		return device->CreateShader(desc);
	}

	Result<Rhi::LayoutPtr> CreateBindGroupLayout(const Vector<Rhi::LayoutBinding>& bindings) override
	{
		return device->CreateBindGroupLayout(bindings);
	}

	Result<Rhi::BindGroupPtr> CreateBindGroup(const Rhi::BindGroupDesc& desc) override
	{
		++bindGroupCreates;
		return device->CreateBindGroup(desc);
	}

	Result<Rhi::PipelinePtr> CreateGraphicsPipeline(const Rhi::GraphicsPipelineDesc& desc) override
	{
		++graphicsPipelineCreates;
		return device->CreateGraphicsPipeline(desc);
	}

	Result<Rhi::PipelinePtr> CreateComputePipeline(const Rhi::ComputePipelineDesc& desc) override
	{
		return device->CreateComputePipeline(desc);
	}

	Result<Rhi::CommandListPtr> CreateCommandList() override
	{
		return device->CreateCommandList();
	}

	Result<Rhi::SwapchainPtr> CreateSwapchain(const Rhi::SwapchainDesc& desc) override
	{
		return device->CreateSwapchain(desc);
	}

	Result<Rhi::FencePtr> Submit(const Rhi::CommandListPtr& command, const Rhi::SwapchainPtr& swapchain = {}) override
	{
		return device->Submit(command, swapchain);
	}

	Result<void> WaitIdle() override
	{
		return device->WaitIdle();
	}

	Ursa::Size graphicsPipelineCreates = 0;
	Ursa::Size bindGroupCreates = 0;
	Ursa::Size storageBufferCreates = 0;

private:
	Rhi::DevicePtr device;
};

void TestMaterialBackingReuse(const DxcShaderCompiler& compiler)
{
	using namespace Ursa;
	using namespace Ursa::Rhi;
	DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = std::make_shared<CountingDevice>(Take(CreateDevice(description)));
	auto shader = std::make_shared<ShaderAsset>();
	shader->id = ResourceId::Generate();
	shader->name = "Tests/MaterialBacking";
	shader->sourcePath = "prj:Tests/MaterialBacking.ursashader";
	shader->sourceHash = "material-backing-v1";
	shader->compilerVersion = Take(compiler.Version());
	PropertyDeclaration tint;
	tint.identifier = "_Tint";
	tint.displayName = "Tint";
	tint.type = PropertyType::Color;
	tint.defaultVector = {1, 1, 1, 1};
	shader->properties.push_back(tint);
	ShaderSubShaderAsset subShader;
	subShader.tags.push_back({"RenderPipeline", "UrsaUniversal"});
	ShaderPassAsset pass;
	pass.name = "Forward";
	pass.tags.push_back({"LightMode", "UniversalForward"});
	ShaderVariantAsset variant;
	variant.stages.push_back(
	    CompileTestStage(compiler, ProgramStage::Vertex, "VSMain",
	                     "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }"));
	variant.stages.push_back(CompileTestStage(compiler, ProgramStage::Fragment, "PSMain",
	                                          "cbuffer MaterialData : register(b0) { float4 _Tint; }; "
	                                          "float4 PSMain() : SV_Target0 { return _Tint; }"));
	pass.variants.push_back(std::move(variant));
	subShader.passes.push_back(std::move(pass));
	shader->subShaders.push_back(std::move(subShader));
	auto left = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	auto right = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(left->SetShader(shader->id));
	Done(right->SetShader(shader->id));
	Done(left->SetShaderAsset(shader));
	Done(right->SetShaderAsset(shader));
	Done(left->SetPropertyValue("_Tint", *shader, MaterialPropertyValue::ColorValue({1, 0, 0, 1})));
	Done(right->SetPropertyValue("_Tint", *shader, MaterialPropertyValue::ColorValue({0, 0, 1, 1})));
	Vector<Test::MeshDraw> boxes{{Mat4::Identity(), {}, left}};
	auto renderer = Take(Test::SceneRenderer::Create(device));
	const auto render = [&]()
	{
		constexpr UInt32 size = 64;
		auto color = Take(device->CreateTexture(
		    {size, size, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		Done(Take(renderer->DrawOffscreen(color, Mat4::Identity(), boxes))->Wait());
		auto readback =
		    Take(device->CreateBuffer({size * size * 4, BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(color, readback));
		Done(command->End());
		Done(Take(device->Submit(command))->Wait());
		Array<UInt8, size * size * 4> pixels{};
		Done(readback->Read(0, pixels.data(), pixels.size()));
		return pixels;
	};
	const auto hasColor = [](const auto& pixels, UInt8 red, UInt8 green, UInt8 blue)
	{
		for (Size index = 0; index < pixels.size(); index += 4)
		{
			if ((red ? pixels[index] >= 240 : pixels[index] < 10) &&
			    (green ? pixels[index + 1] >= 240 : pixels[index + 1] < 10) &&
			    (blue ? pixels[index + 2] >= 240 : pixels[index + 2] < 10))
			{
				return true;
			}
		}
		return false;
	};
	const auto first = render();
	if (!hasColor(first, 255, 0, 0) || device->bindGroupCreates != 1)
	{
		UInt8 maximumRed = 0, maximumGreen = 0, maximumBlue = 0;
		for (Size index = 0; index < first.size(); index += 4)
		{
			maximumRed = std::max(maximumRed, first[index]);
			maximumGreen = std::max(maximumGreen, first[index + 1]);
			maximumBlue = std::max(maximumBlue, first[index + 2]);
		}
		throw std::runtime_error(
		    "The first Shader material did not render its tint with one backing (max=" + std::to_string(maximumRed) +
		    "," + std::to_string(maximumGreen) + "," + std::to_string(maximumBlue) +
		    "; bind groups=" + std::to_string(device->bindGroupCreates) + ")");
	}
	boxes[0].material = right;
	const auto second = render();
	Expect(hasColor(second, 0, 0, 255) && device->bindGroupCreates == 2,
	       "A second Shader material reused another material's parameter backing");
	Expect(render() == second && device->bindGroupCreates == 2,
	       "Stable material versions did not reuse their descriptor backing");
	Done(left->SetPropertyValue("_Tint", *shader, MaterialPropertyValue::ColorValue({0, 1, 0, 1})));
	boxes[0].material = left;
	const auto changed = render();
	Expect(hasColor(changed, 0, 255, 0) && device->bindGroupCreates == 3,
	       "Changing a shared material version did not refresh its backing");
	Done(device->WaitIdle());
}

void TestBonePaletteReuse(const DxcShaderCompiler& compiler)
{
	using namespace Ursa;
	using namespace Ursa::Rhi;
	DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = std::make_shared<CountingDevice>(Take(CreateDevice(description)));
	auto shader = std::make_shared<ShaderAsset>();
	shader->id = ResourceId::Generate();
	shader->name = "Tests/BonePalette";
	shader->sourcePath = "prj:Tests/BonePalette.ursashader";
	shader->sourceHash = "bone-palette-v1";
	shader->compilerVersion = Take(compiler.Version());
	const String vertex = "StructuredBuffer<float4x4> _UrsaBones : register(t0); "
	                      "float4 VSMain(float3 position : POSITION) : SV_Position "
	                      "{ return mul(_UrsaBones[0], float4(position, 1.0)); }";
	const String fragment = "float4 PSMain() : SV_Target0 { return float4(0.4, 0.7, 0.2, 1.0); }";
	ShaderSubShaderAsset subShader;
	subShader.tags.push_back({"RenderPipeline", "UrsaUniversal"});
	for (const char* lightMode : {"Forward", "SRPDefaultUnlit"})
	{
		ShaderPassAsset pass;
		pass.name = lightMode;
		pass.tags.push_back({"LightMode", lightMode});
		ShaderVariantAsset variant;
		variant.stages.push_back(CompileTestStage(compiler, ProgramStage::Vertex, "VSMain", vertex));
		variant.stages.push_back(CompileTestStage(compiler, ProgramStage::Fragment, "PSMain", fragment));
		pass.variants.push_back(std::move(variant));
		subShader.passes.push_back(std::move(pass));
	}
	shader->subShaders.push_back(std::move(subShader));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetShader(shader->id));
	Done(material->SetShaderAsset(shader));
	Test::MeshDraw box{Mat4::Identity(), {}, material};
	box.jointPalette.push_back(Mat4::Identity());
	Vector<Test::MeshDraw> boxes{box};
	auto renderer = Take(Test::SceneRenderer::Create(device));
	auto color = Take(device->CreateTexture(
	    {32, 32, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	Done(Take(renderer->DrawOffscreen(color, Mat4::Identity(), boxes))->Wait());
	Expect(device->storageBufferCreates == 1,
	       "Repeated passes uploaded the same skinned-object palette more than once");
	Done(device->WaitIdle());
}

void TestComputeAssetImport(const DxcShaderCompiler& compiler)
{
	using namespace Ursa;
	using namespace Ursa::Shader;
	VirtualFileSystem files;
	Done(files.MountProvider("prj", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	Done(files.MountProvider("lib", std::make_shared<MemoryFileProvider>(), MountAccess::ReadWrite));
	const auto sourcePath = Take(VirtualPath::Parse("prj:Compute/Fill.compute"));
	const auto metadataPath = Take(VirtualPath::Parse(sourcePath.ToString() + ".meta"));
	const ResourceId identity = ResourceId::Generate();
	const String source = "#include \"Shared.hlsli\"\n#pragma kernel Fill\n[numthreads(8, 4, 1)]\n"
	                      "void Fill(uint3 id : SV_DispatchThreadID) { SharedValue(); }\n";
	Done(files.WriteAtomic(sourcePath, Vector<UInt8>(source.begin(), source.end())));
	const auto includePath = Take(VirtualPath::Parse("prj:Compute/Shared.hlsli"));
	String includeText = "void SharedValue() {}\n";
	Done(files.WriteAtomic(includePath, Vector<UInt8>(includeText.begin(), includeText.end())));
	const String metadata = "{\"version\":1,\"folder\":false,\"guid\":\"" + identity.ToString() + "\"}";
	Done(files.WriteAtomic(metadataPath, Vector<UInt8>(metadata.begin(), metadata.end())));
	const auto cacheRoot = Take(VirtualPath::Parse("lib:ShaderCache"));
	auto imported = Take(ShaderImporter::ImportAll(files, compiler, Take(compiler.Version()), "prj", "prj", cacheRoot,
	                                               256, {}, sourcePath));
	Expect(imported.size() == 1 && imported[0].id == identity && imported[0].name == "Compute/Fill" &&
	           imported[0].subShaders.size() == 1 && imported[0].subShaders[0].passes.size() == 1,
	       ".compute source did not publish a stable ComputeShader identity and kernel");
	const auto& kernel = imported[0].subShaders[0].passes[0];
	Expect(kernel.computeKernel && kernel.name == "Fill" && kernel.threadGroupSize == Array<UInt32, 3>{8, 4, 1} &&
	           kernel.variants.size() == 1 && kernel.variants[0].stages.size() == 1 &&
	           kernel.variants[0].stages[0].stage == ProgramStage::Compute,
	       "Compute kernel metadata, thread-group size, or compiled stage was lost");
	const String sourceFingerprint = imported[0].sourceHash;
	includeText = "void SharedValue() { uint value = 1; }\n";
	Done(files.WriteAtomic(includePath, Vector<UInt8>(includeText.begin(), includeText.end())));
	auto reimported = Take(ShaderImporter::ImportAll(files, compiler, Take(compiler.Version()), "prj", "prj", cacheRoot,
	                                                 256, {}, sourcePath));
	Expect(reimported.size() == 1 && reimported[0].id == identity && reimported[0].sourceHash != sourceFingerprint,
	       "Changing a compute include did not invalidate the compiled ComputeShader artifact");
	imported = std::move(reimported);
	Expect(!ParseComputeSource("prj:Compute/Missing.compute", "void Missing() {}\n"),
	       "Compute source without a named #pragma kernel was accepted");
	Expect(!ParseComputeSource("prj:Compute/Invalid.compute",
	                           "#pragma kernel Invalid\n[numthreads(1024, 1024, 1)]\nvoid Invalid() {}\n"),
	       "Invalid compute thread-group dimensions were accepted");
	const auto invalidKernelPath = Take(VirtualPath::Parse("prj:Compute/InvalidBinding.compute"));
	const auto invalidKernelMetadata = Take(VirtualPath::Parse(invalidKernelPath.ToString() + ".meta"));
	const ResourceId invalidKernelId = ResourceId::Generate();
	const String invalidKernelSource =
	    "RWStructuredBuffer<uint> Values : register(u0, space1);\n#pragma kernel InvalidBinding\n"
	    "[numthreads(1, 1, 1)]\nvoid InvalidBinding(uint3 id : SV_DispatchThreadID) { Values[id.x] = 1; }\n";
	Done(files.WriteAtomic(invalidKernelPath, Vector<UInt8>(invalidKernelSource.begin(), invalidKernelSource.end())));
	const String invalidKernelMeta = "{\"version\":1,\"folder\":false,\"guid\":\"" + invalidKernelId.ToString() + "\"}";
	Done(files.WriteAtomic(invalidKernelMetadata, Vector<UInt8>(invalidKernelMeta.begin(), invalidKernelMeta.end())));
	auto invalidImport = ShaderImporter::ImportAll(files, compiler, Take(compiler.Version()), "prj", "prj", cacheRoot,
	                                               256, {}, invalidKernelPath);
	Expect(!invalidImport &&
	           !files.Exists(Take(VirtualPath::Parse("prj:Compute/InvalidBinding.ursashaderasset"))).value,
	       "Unsupported compute resource binding was published as a valid artifact");
	const auto compiledPath = Take(VirtualPath::Parse("prj:Compute/Fill.ursashaderasset"));
	auto compiledBytes = Take(files.Read(compiledPath));
	const auto compiledMetadataPath = Take(VirtualPath::Parse(compiledPath.ToString() + ".meta"));
	const String compiledMetadata = "{\"version\":1,\"folder\":false,\"guid\":\"" + identity.ToString() + "\"}";
	Done(files.WriteAtomic(compiledMetadataPath, Vector<UInt8>(compiledMetadata.begin(), compiledMetadata.end())));
	auto header = Take(ReadResourceHeader(files, compiledPath));
	Expect(header.id == identity && header.kind == ResourceKind::Shader,
	       "Compute shader artifact did not preserve its source GUID");
	auto looseOpen = ResourceSession::Open(files);
	if (!looseOpen)
	{
		throw std::runtime_error("Cannot open loose compute artifact index: " + ToStdString(looseOpen.error));
	}
	auto looseResources = std::move(looseOpen.value);
	auto looseResult = looseResources->LoadShader(identity);
	if (!looseResult)
	{
		throw std::runtime_error("Cannot load loose compute artifact: " + ToStdString(looseResult.error));
	}
	auto looseShader = std::move(looseResult.value);
	Expect(looseShader->subShaders[0].passes[0].computeKernel,
	       "Loose imported ComputeShader artifact was not loadable by stable GUID");
	Vector<PakEntryData> entries;
	entries.push_back({Take(VirtualPath::Parse("ursa:Compute/Fill.ursashaderasset")), std::move(compiledBytes)});
	entries.push_back({Take(VirtualPath::Parse("ursa:Compute/Fill.ursashaderasset.meta")),
	                   Vector<UInt8>(compiledMetadata.begin(), compiledMetadata.end())});
	auto package = Take(PakBuilder::Encode(std::move(entries)));
	const auto packagePath = std::filesystem::u8path(URSA_SHADER_STAGING_ROOT) / "ComputeKernel.pak";
	std::filesystem::create_directories(packagePath.parent_path());
	{
		std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
		Expect(static_cast<bool>(output), "Cannot create compute Shader Pak fixture");
		output.write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
		Expect(static_cast<bool>(output), "Cannot write compute Shader Pak fixture");
	}
	auto archive = Take(PakArchive::Open(packagePath));
	VirtualFileSystem packed;
	Done(archive->Mount(packed));
	auto packedOpen = ResourceSession::Open(packed);
	if (!packedOpen)
	{
		throw std::runtime_error("Cannot open compute Pak resource index: " + ToStdString(packedOpen.error));
	}
	auto resources = std::move(packedOpen.value);
	auto packedResult = resources->LoadShader(identity);
	if (!packedResult)
	{
		throw std::runtime_error("Cannot load compute Shader from Pak: " + ToStdString(packedResult.error));
	}
	auto loaded = std::move(packedResult.value);
	Expect(loaded->subShaders[0].passes[0].computeKernel &&
	           loaded->subShaders[0].passes[0].threadGroupSize == Array<UInt32, 3>{8, 4, 1},
	       "Pak-backed compute kernel metadata was not restored");
}

void RenderShaderPasses(const DxcShaderCompiler& compiler)
{
	using namespace Ursa;
	using namespace Ursa::Rhi;
	DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto nativeDevice = Take(CreateDevice(description));
	auto device = std::make_shared<CountingDevice>(nativeDevice);
	auto shader = std::make_shared<ShaderAsset>(BuildPassOrderShader(compiler));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetShaderAsset(shader));
	auto renderer = Take(Test::SceneRenderer::Create(device));
	Vector<Test::MeshDraw> boxes{{Mat4::Identity(), {}, material}};
	const auto renderCenterPixel = [&]()
	{
		auto color = Take(device->CreateTexture(
		    {64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		Done(Take(renderer->DrawOffscreen(color, Mat4::Identity(), boxes))->Wait());
		auto readback =
		    Take(device->CreateBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Rhi::Memory::Readback}));
		auto command = Take(device->CreateCommandList());
		Done(command->Begin());
		Done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
		Done(command->CopyTextureToBuffer(color, readback));
		Done(command->End());
		Done(Take(device->Submit(command))->Wait());
		Array<UInt8, 4> center{};
		Done(readback->Read((32 * 64 + 32) * 4, center.data(), center.size()));
		return center;
	};
	const auto blended = renderCenterPixel();
	Expect(device->graphicsPipelineCreates == 2,
	       "Shader pass pipelines were not created once per selected Forward Pass");
	Expect(blended[0] >= 120 && blended[0] <= 136 && blended[1] >= 120 && blended[1] <= 136 && blended[2] == 0,
	       "Forward Pass order, alpha blending, or RenderPipeline selection did not produce the expected pixel");
	const auto repeated = renderCenterPixel();
	Expect(device->graphicsPipelineCreates == 2 && repeated == blended,
	       "Equivalent Shader Pass inputs did not reuse their cached pipelines");
	auto& overlayStates = shader->subShaders[1].passes[2].renderStates;
	overlayStates = {{"Cull", "Off", {}}, {"ZTest", "Greater", {}}, {"ZWrite", "Off", {}}, {"Blend", "Off", {}}};
	const auto depthRejected = renderCenterPixel();
	Expect(device->graphicsPipelineCreates == 3,
	       "Changing a Shader Pass state did not create a distinct cached pipeline");
	Expect(depthRejected[0] >= 250 && depthRejected[1] == 0 && depthRejected[2] == 0,
	       "Pipeline cache reused a stale Pass state instead of applying the changed depth test");
	Done(material->SetKeywordEnabled("URSA_MISSING_VARIANT", true));
	auto missingColor = Take(device->CreateTexture(
	    {64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	auto missingVariant = renderer->DrawOffscreen(missingColor, Mat4::Identity(), boxes);
	Expect(!missingVariant &&
	           ToStdString(missingVariant.error).find("No compiled keyword variant") != std::string::npos,
	       "Missing compiled Shader keyword variant did not return a clear runtime diagnostic");
	Done(material->SetKeywordEnabled("URSA_MISSING_VARIANT", false));
	auto sharedShader = std::make_shared<ShaderAsset>();
	sharedShader->id = ResourceId::Generate();
	sharedShader->name = "Tests/SharedPass";
	sharedShader->sourcePath = "prj:Tests/SharedPass.ursashader";
	sharedShader->sourceHash = "shared-pass-v1";
	sharedShader->compilerVersion = "DXC test";
	ShaderSubShaderAsset sharedSubShader;
	sharedSubShader.tags.push_back({"RenderPipeline", "UrsaUniversal"});
	ShaderPassAsset sharedPass;
	sharedPass.name = "Blue";
	sharedPass.tags.push_back({"LightMode", "Forward"});
	ShaderVariantAsset sharedVariant;
	sharedVariant.stages.push_back(
	    CompileTestStage(compiler, ProgramStage::Vertex, "VSMain",
	                     "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }"));
	sharedVariant.stages.push_back(CompileTestStage(compiler, ProgramStage::Fragment, "PSMain",
	                                                "float4 PSMain() : SV_Target0 { return float4(0, 0, 1, 1); }"));
	sharedPass.variants.push_back(std::move(sharedVariant));
	sharedSubShader.passes.push_back(std::move(sharedPass));
	sharedShader->subShaders.push_back(std::move(sharedSubShader));
	auto usePassShader = std::make_shared<ShaderAsset>();
	usePassShader->id = ResourceId::Generate();
	usePassShader->name = "Tests/UsePass";
	usePassShader->sourceHash = "use-pass-v1";
	ShaderSubShaderAsset usePassSubShader;
	usePassSubShader.tags.push_back({"RenderPipeline", "UrsaUniversal"});
	usePassSubShader.passOrder.push_back({0, "Tests/SharedPass/Blue", true});
	usePassShader->subShaders.push_back(std::move(usePassSubShader));
	usePassShader->resolvedReferences.push_back({{sharedShader->name, sharedShader->id, "Blue", false}, sharedShader});
	Done(material->SetShader(usePassShader->id));
	Done(material->SetShaderAsset(usePassShader));
	const auto usePassPixel = renderCenterPixel();
	Expect(usePassPixel[2] >= 250 && usePassPixel[0] == 0 && usePassPixel[1] == 0,
	       "UsePass selection did not resolve and render the referenced pass");
	Expect(renderCenterPixel() == usePassPixel, "Cached UsePass selection changed the rendered output");
	sharedShader->sourceHash = "shared-pass-v2";
	Expect(renderCenterPixel() == usePassPixel,
	       "Changing a referenced Shader version broke UsePass cache invalidation");
	auto fallbackShader = std::make_shared<ShaderAsset>();
	fallbackShader->id = ResourceId::Generate();
	fallbackShader->name = "Tests/Fallback";
	fallbackShader->sourceHash = "fallback-v1";
	ShaderSubShaderAsset incompatibleFallbackSubShader;
	incompatibleFallbackSubShader.tags.push_back({"RenderPipeline", "UrsaOther"});
	fallbackShader->subShaders.push_back(std::move(incompatibleFallbackSubShader));
	fallbackShader->resolvedReferences.push_back({{sharedShader->name, sharedShader->id, {}, true}, sharedShader});
	Done(material->SetShader(fallbackShader->id));
	Done(material->SetShaderAsset(fallbackShader));
	const auto fallbackPixel = renderCenterPixel();
	Expect(fallbackPixel[2] >= 250 && fallbackPixel[0] == 0 && fallbackPixel[1] == 0,
	       "Fallback selection did not resolve the compatible referenced Shader");
	Done(device->WaitIdle());
}

void TestUnsupportedComputeStage(const DxcShaderCompiler& compiler)
{
	using namespace Ursa;
	using namespace Ursa::Rhi;
	DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = Take(CreateDevice(description));
	auto shader = std::make_shared<ShaderAsset>();
	shader->id = Take(ResourceId::Parse("5403c8ea-669d-4b64-8c30-6d63d128de02"));
	shader->name = "Tests/ComputePass";
	ShaderSubShaderAsset subShader;
	subShader.tags.push_back({"RenderPipeline", "UrsaUniversal"});
	ShaderPassAsset pass;
	pass.name = "ComputeOnly";
	pass.tags.push_back({"LightMode", "Forward"});
	ShaderVariantAsset variant;
	variant.stages.push_back(
	    CompileTestStage(compiler, ProgramStage::Compute, "CSMain", "[numthreads(1, 1, 1)] void CSMain() {}"));
	pass.variants.push_back(std::move(variant));
	subShader.passes.push_back(std::move(pass));
	shader->subShaders.push_back(std::move(subShader));
	auto material = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	Done(material->SetShaderAsset(shader));
	auto renderer = Take(Test::SceneRenderer::Create(device));
	auto color = Take(device->CreateTexture(
	    {16, 16, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	Vector<Test::MeshDraw> boxes{{Mat4::Identity(), {}, material}};
	auto rendered = renderer->DrawOffscreen(color, Mat4::Identity(), boxes);
	Expect(!rendered && ToStdString(rendered.error).find("Compute stage cannot execute") != std::string::npos,
	       "Unsupported compute-in-graphics Pass did not return a clear diagnostic");
	Done(device->WaitIdle());
}

Array<UInt8, 4> RenderImportedShader(const std::shared_ptr<const ShaderAsset>& shader,
                                     const std::shared_ptr<MaterialInstance>& material)
{
	using namespace Ursa;
	using namespace Ursa::Rhi;
	DeviceDesc description;
#ifndef NDEBUG
	description.validation = true;
#endif
	auto device = Take(CreateDevice(description));
	auto renderer = Take(Rendering::NativeForwardPipeline::Create(device));
	Scene scene;
	auto cameraObject = Take(scene.CreateGameObject("Imported Shader Camera"));
	LocalTransform cameraTransform;
	cameraTransform.position = {0, 0, 4};
	Done(scene.SetLocalTransform(cameraObject, cameraTransform));
	auto* camera = Take(cameraObject->AddComponent<CameraComponent>());
	Done(scene.SetActiveCamera(camera->Id()));
	auto object = Take(scene.CreateGameObject("Imported Shader Geometry"));
	Take(Test::AddRenderer(scene, object));
	Done(object->FindComponent<MeshRendererComponent>()->SetMaterial(material));
	auto color = Take(Rendering::CreateRenderTexture(*device, {64, 64, 0, 32}));
	Done(renderer->RenderOffscreen(scene, color, ResourceState::TransferSource));
	const auto pixels = Take(Rendering::ReadRenderTexture(*device, *color));
	const Size center = (32 * 64 + 32) * 4;
	Array<UInt8, 4> result{};
	std::copy_n(pixels.data() + center, result.size(), result.data());
	return result;
}
} // namespace

int main()
{
	try
	{
		ConfigureBuiltinShaderResources();
		VirtualFileSystem filesystem;
		Expect(static_cast<bool>(filesystem.Mount("ursa", std::filesystem::u8path(URSA_ENGINE_SHADER_ASSET_DIR),
		                                          MountAccess::ReadOnly)),
		       "Cannot mount Engine shader examples through VFS");
		auto library = ShaderSourceLibrary::Load(filesystem, "ursa");
		Expect(static_cast<bool>(library), "Documented shader examples did not import");
		auto dependencies = library.value.ResolveDependencies(filesystem, "Ursa/Examples/Unlit");
		const auto hasInclude = [&](const char* expected)
		{
			if (dependencies)
			{
				for (const auto& path : dependencies.value.includes)
				{
					if (path.ToString() == expected)
					{
						return true;
					}
				}
			}
			return false;
		};
		Expect(static_cast<bool>(dependencies) && hasInclude("ursa:Shaders/ShaderLibrary/Core.hlsl") &&
		           hasInclude("ursa:Shaders/ShaderLibrary/SpaceTransforms.hlsl") &&
		           hasInclude("ursa:Shaders/ShaderLibrary/Textures.hlsl"),
		       "Documented shader include did not resolve");
		const bool hasLitMaterialInclude =
		    dependencies && std::any_of(dependencies.value.includes.begin(), dependencies.value.includes.end(),
		                                [](const VirtualPath& path)
		                                {
			                                const String value = path.ToString();
			                                return value.find("/SurfaceData.hlsl") != String::npos ||
			                                       value.find("/SurfaceInput.hlsl") != String::npos ||
			                                       value.find("/InputData.hlsl") != String::npos;
		                                });
		Expect(!hasLitMaterialInclude,
		       "The standalone Unlit shader unexpectedly depends on Lit surface material declarations");
		const ShaderSourceAsset* asset = library.value.Find("Ursa/Examples/Unlit");
		Expect(asset != nullptr, "Documented Unlit Shader was not indexed");
		Vector<ProgramBlock> sharedBlocks;
		CollectSharedBlocks(asset->source, sharedBlocks);
		Vector<ShaderIncludeSource> includes;
		for (const VirtualPath& path : dependencies.value.includes)
		{
			auto text = filesystem.ReadText(path);
			Expect(static_cast<bool>(text), "Documented HLSL include could not be read through VFS");
			includes.push_back({path, std::move(text.value)});
		}
		DxcShaderCompiler compiler({std::filesystem::u8path(URSA_TEST_DXC_EXECUTABLE),
		                            std::filesystem::u8path(URSA_TEST_SPIRV_VAL_EXECUTABLE),
		                            std::filesystem::u8path(URSA_TEST_SPIRV_CROSS_EXECUTABLE),
		                            std::filesystem::u8path(URSA_SHADER_STAGING_ROOT)});
		auto compilerVersion = compiler.Version();
		Expect(static_cast<bool>(compilerVersion), "Shader toolchain version could not be queried");
		TestComputeAssetImport(compiler);
		TestMaterialBackingReuse(compiler);
		TestBonePaletteReuse(compiler);
		ShaderPassCompilation passCompilation;
		Ursa::Size compiledStages = 0;
		for (const SubShaderDeclaration& subShader : asset->source.subShaders)
		{
			for (const PassDeclaration& pass : subShader.passes)
			{
				for (const ProgramBlock& program : pass.programs)
				{
					if (program.kind != ProgramBlockKind::Program)
					{
						continue;
					}
					for (const ProgramEntryPoint& entry : program.entryPoints)
					{
						ShaderCompileRequest request;
						request.sourcePath = asset->path;
						request.entryPoint = entry;
						request.source = program.source;
						request.sharedBlocks = sharedBlocks;
						request.includes = includes;
						auto compiled = compiler.Compile(request);
						if (!compiled)
						{
							throw std::runtime_error("Documented HLSL sample did not compile for Vulkan: " +
							                         ToStdString(compiled.error));
						}
						passCompilation.stages.push_back(std::move(compiled.value));
						++compiledStages;
					}
				}
			}
		}
		Expect(compiledStages == 2, "Documented Unlit shader should compile vertex and fragment stages");
		auto shaderAsset = BuildShaderAsset(*asset, library.value, dependencies.value, "sample-source-hash",
		                                    compilerVersion.value, {passCompilation});
		Expect(static_cast<bool>(shaderAsset), "Documented shader could not build compiled asset metadata");
		const auto& variant = shaderAsset.value.subShaders[0].passes[0].variants[0];
		auto bindingLayout = BuildShaderBindingLayout(shaderAsset.value, variant);
		Expect(static_cast<bool>(bindingLayout), "Actual DXC reflection did not produce a material resource layout");
		Expect(bindingLayout.value.properties.find("_Tint") != bindingLayout.value.properties.end() &&
		           bindingLayout.value.properties.find("_BaseMap") != bindingLayout.value.properties.end(),
		       "Actual reflection did not map Shader properties to uniform and texture bindings");
		MaterialInstance material(MaterialType::Unlit);
		Expect(static_cast<bool>(material.SetShader(shaderAsset.value.id)) &&
		           static_cast<bool>(material.SetPropertyValue(
		               "_Tint", shaderAsset.value, MaterialPropertyValue::ColorValue({0.25f, 0.5f, 0.75f, 1.0f}))),
		       "Could not set reflected material properties");
		const auto tintBinding = bindingLayout.value.properties.find("_Tint");
		Expect(tintBinding != bindingLayout.value.properties.end() && tintBinding->second.uniformMember,
		       "Tint property did not retain its reflected material uniform binding");
		auto tintBuffer =
		    PackMaterialUniformBuffer(shaderAsset.value, bindingLayout.value, material, tintBinding->second.binding);
		Expect(static_cast<bool>(tintBuffer), "Material values could not be packed using actual reflection offsets");
		auto texture = MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {0, 255, 0, 255});
		Expect(static_cast<bool>(texture) &&
		           static_cast<bool>(material.SetPropertyValue(
		               "_BaseMap", shaderAsset.value,
		               MaterialPropertyValue::Texture2D(ResourceId::Generate(), texture.value))),
		       "Could not assign reflected material texture");
		auto textureBindings = BuildMaterialTextureBindings(shaderAsset.value, bindingLayout.value, material);
		Expect(textureBindings && textureBindings.value.size() == 1,
		       "Actual reflected texture resource could not be bound");
		RenderReflectedTexture(variant, bindingLayout.value, tintBuffer.value, texture.value);
		auto runtimeShader = std::make_shared<ShaderAsset>(shaderAsset.value);
		auto runtimeMaterial = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		Expect(static_cast<bool>(runtimeMaterial->SetShaderAsset(runtimeShader)) &&
		           static_cast<bool>(runtimeMaterial->SetPropertyValue(
		               "_Tint", *runtimeShader, MaterialPropertyValue::ColorValue({0.25f, 0.5f, 0.75f, 1.0f}))) &&
		           static_cast<bool>(runtimeMaterial->SetPropertyValue(
		               "_BaseMap", *runtimeShader,
		               MaterialPropertyValue::Texture2D(ResourceId::Generate(), texture.value))),
		       "Cannot create a Shader-backed material for Universal rendering");
		const auto renderedSample = RenderImportedShader(runtimeShader, runtimeMaterial);
		Expect(renderedSample[0] == 0 && renderedSample[1] >= 150 && renderedSample[1] <= 162 &&
		           renderedSample[2] == 0 && renderedSample[3] == 255,
		       "Imported Unlit Shader did not render through the runtime Vulkan renderer");
		RenderShaderPasses(compiler);
		TestUnsupportedComputeStage(compiler);
		std::cout << "Shader example import tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
