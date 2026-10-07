#include <Shader/ShaderCompiler.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Shader/ShaderSourceLibrary.hpp>
#include <Core/StringInterop.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Rhi/RHI.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
using namespace Ursa;
using namespace Ursa::Shader;

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
	for (const ShaderUniformBufferLayout& uniform : bindings.uniformBuffers)
	{
		if (uniform.binding == 0)
		{
			objectBinding = uniform.binding;
			objectBufferSize = uniform.byteSize;
		}
		else
		{
			materialBinding = uniform.binding;
		}
	}
	Expect(objectBufferSize != 0 && materialBinding != 0, "UrsaPerObject or material uniform block was not reflected");
	Array<Float32, 32> objectData{};
	for (Ursa::Size matrix = 0; matrix < 2; ++matrix)
	{
		for (Ursa::Size diagonal = 0; diagonal < 4; ++diagonal)
		{
			objectData[matrix * 16 + diagonal * 5] = 1.0f;
		}
	}
	auto objectBuffer = Take(device->CreateBuffer({objectBufferSize, Rhi::BufferUsage::Uniform, Rhi::Memory::Upload}));
	Done(objectBuffer->Write(0, objectData.data(), objectBufferSize));
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
	const auto textureBinding = bindings.properties.find("_MainTex");
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
	forward.tags.push_back({"RenderPipeline", "UrsaForward"});
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

	Result<Rhi::BufferPtr> CreateBuffer(const Rhi::BufferDesc& desc) override
	{
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

private:
	Rhi::DevicePtr device;
};

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
	auto renderer = Take(Renderer3D::Create(device));
	Vector<ModelBoxInstance> boxes{{Mat4::Identity(), {}, material}};
	const auto renderCenterPixel = [&]()
	{
		auto color = Take(device->CreateTexture(
		    {64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
		Done(Take(renderer->RenderOffscreenModels(color, Mat4::Identity(), boxes))->Wait());
		auto readback = Take(device->CreateBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Memory::Readback}));
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
	auto missingVariant = renderer->RenderOffscreenModels(missingColor, Mat4::Identity(), boxes);
	Expect(!missingVariant &&
	           ToStdString(missingVariant.error).find("No compiled keyword variant") != std::string::npos,
	       "Missing compiled Shader keyword variant did not return a clear runtime diagnostic");
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
	subShader.tags.push_back({"RenderPipeline", "UrsaForward"});
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
	auto renderer = Take(Renderer3D::Create(device));
	auto color = Take(device->CreateTexture(
	    {16, 16, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	Vector<ModelBoxInstance> boxes{{Mat4::Identity(), {}, material}};
	auto rendered = renderer->RenderOffscreenModels(color, Mat4::Identity(), boxes);
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
	auto renderer = Take(Renderer3D::Create(device));
	auto color = Take(device->CreateTexture(
	    {64, 64, Format::RgbA8Unorm, TextureUsage::ColorAttachment | TextureUsage::TransferSource}));
	Vector<ModelBoxInstance> boxes{{Mat4::Identity(), {}, material}};
	Done(Take(renderer->RenderOffscreenModels(color, Mat4::Identity(), boxes))->Wait());
	auto readback = Take(device->CreateBuffer({64 * 64 * 4, BufferUsage::TransferDestination, Memory::Readback}));
	auto command = Take(device->CreateCommandList());
	Done(command->Begin());
	Done(command->Barrier(readback, ResourceState::Undefined, ResourceState::TransferDestination));
	Done(command->CopyTextureToBuffer(color, readback));
	Done(command->End());
	Done(Take(device->Submit(command))->Wait());
	Array<UInt8, 4> center{};
	Done(readback->Read((32 * 64 + 32) * 4, center.data(), center.size()));
	Done(device->WaitIdle());
	return center;
}
} // namespace

int main()
{
	try
	{
		VirtualFileSystem filesystem;
		Expect(static_cast<bool>(filesystem.Mount("ursa", std::filesystem::u8path(URSA_ENGINE_SHADER_ASSET_DIR),
		                                          MountAccess::ReadOnly)),
		       "Cannot mount Engine shader examples through VFS");
		auto library = ShaderSourceLibrary::Load(filesystem, "ursa");
		Expect(static_cast<bool>(library), "Documented shader examples did not import");
		auto dependencies = library.value.ResolveDependencies(filesystem, "Ursa/Examples/Unlit");
		Expect(static_cast<bool>(dependencies) && dependencies.value.includes.size() == 1,
		       "Documented shader include did not resolve");
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
		                            std::filesystem::u8path(URSA_TEST_SPIRV_CROSS_EXECUTABLE)});
		auto compilerVersion = compiler.Version();
		Expect(static_cast<bool>(compilerVersion), "Shader toolchain version could not be queried");
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
						Expect(static_cast<bool>(compiled), "Documented HLSL sample did not compile for Vulkan");
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
		           bindingLayout.value.properties.find("_MainTex") != bindingLayout.value.properties.end(),
		       "Actual reflection did not map Shader properties to uniform and texture bindings");
		MaterialInstance material(MaterialType::Unlit);
		Expect(static_cast<bool>(material.SetShader(shaderAsset.value.id)) &&
		           static_cast<bool>(material.SetPropertyValue(
		               "_Tint", shaderAsset.value, MaterialPropertyValue::ColorValue({0.25f, 0.5f, 0.75f, 1.0f}))),
		       "Could not set reflected material properties");
		auto tintBuffer = PackMaterialUniformBuffer(shaderAsset.value, bindingLayout.value, material, 1);
		Expect(static_cast<bool>(tintBuffer), "Material values could not be packed using actual reflection offsets");
		auto texture = MaterialTexture::Create(1, 1, TextureColorSpace::Srgb, {0, 255, 0, 255});
		Expect(static_cast<bool>(texture) &&
		           static_cast<bool>(material.SetPropertyValue(
		               "_MainTex", shaderAsset.value,
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
		               "_MainTex", *runtimeShader,
		               MaterialPropertyValue::Texture2D(ResourceId::Generate(), texture.value))),
		       "Cannot create a Shader-backed material for Renderer3D");
		const auto renderedSample = RenderImportedShader(runtimeShader, runtimeMaterial);
		Expect(renderedSample[0] == 0 && renderedSample[1] >= 120 && renderedSample[1] <= 136 &&
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
