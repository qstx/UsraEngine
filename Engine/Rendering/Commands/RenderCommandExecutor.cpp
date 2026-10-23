#include <Rendering/Commands/RenderCommandExecutor.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Shader/ShaderRenderState.hpp>
#include <Rendering/Shader/ComputeKernel.hpp>
#include <Rendering/Commands/SceneDrawEncoder.hpp>
#include <Core/StringInterop.hpp>
#include <Core/Profiler.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>

namespace Ursa::Rendering
{
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

void Require(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

template <class F> Result<void> Guard(F&& function)
{
	try
	{
		function();
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

Rhi::TexturePtr SampledTexture(const std::shared_ptr<RenderTextureResource>& resource)
{
	return resource ? (resource->color ? resource->color : resource->depth) : Rhi::TexturePtr{};
}

UInt32 UniformKind(const String& type)
{
	if (type == "float" || type == "half")
	{
		return 1;
	}
	if (type == "vec4" || type == "float4" || type == "half4")
	{
		return 2;
	}
	if (type == "mat4" || type == "float4x4")
	{
		return 5;
	}
	if (type == "int" || type == "uint" || type == "bool")
	{
		return 6;
	}
	return 0;
}

UInt32 UniformKind(const Shader::ShaderUniformMember& member)
{
	if (member.arrayCount > 1)
	{
		if (member.type == "mat4" || member.type == "float4x4")
		{
			return 8;
		}
		if (member.type == "vec4" || member.type == "float4" || member.type == "half4")
		{
			return 9;
		}
	}
	return UniformKind(member.type);
}

String ComputeScope(const RenderCommand& operation)
{
	return operation.shader->id.ToString() + ":" + operation.shader->sourceHash + ":" +
	       operation.shader->compilerVersion + ":" + ToUrsaString(std::to_string(operation.kernel));
}

String CameraRegionName(const CameraData& camera)
{
	const char* type = camera.settings.type == CameraType::SceneView ? "SceneView"
	                   : camera.settings.type == CameraType::Preview ? "Preview"
	                                                                 : "Game";
	const String identity = camera.identity.ToString();
	const String prefix = String(type) + " / ";
	const String suffix = " [" + identity + "]";
	const String& displayName = camera.debugName.empty() ? identity : camera.debugName;
	constexpr Size maximumBytes = 1024;
	const Size maximumDisplayBytes = maximumBytes - prefix.size() - suffix.size();
	Size displayBytes = 0;
	while (displayBytes < displayName.size())
	{
		const auto lead = static_cast<UInt8>(displayName[displayBytes]);
		const Size characterBytes = (lead & 0x80u) == 0       ? 1
		                            : (lead & 0xe0u) == 0xc0u ? 2
		                            : (lead & 0xf0u) == 0xe0u ? 3
		                            : (lead & 0xf8u) == 0xf0u ? 4
		                                                      : 1;
		if (displayBytes + characterBytes > maximumDisplayBytes || displayBytes + characterBytes > displayName.size())
		{
			break;
		}
		displayBytes += characterBytes;
	}
	String result = prefix;
	result.append(displayName.data(), displayBytes);
	result += suffix;
	return result;
}
} // namespace

struct RenderCommandExecutor::Impl
{
	struct PoolEntry
	{
		std::shared_ptr<RenderTextureResource> resource;
		Rhi::FencePtr completion;
		bool leased = false;
	};

	struct Program
	{
		std::shared_ptr<const Shader::ShaderAsset> shader;
		Shader::ShaderBindingLayout reflection;
		Rhi::LayoutPtr layout;
		Rhi::PipelinePtr pipeline;
		bool depth = false;
	};

	struct TextureUpload
	{
		std::weak_ptr<const MaterialTexture> source;
		std::shared_ptr<RenderTextureResource> resource;
	};

	explicit Impl(Rhi::DevicePtr device) : device(std::move(device))
	{
		frames = Take(RenderFrameQueue::Create(this->device));
	}

	void RequireActive() const
	{
		Require(std::this_thread::get_id() == ownerThread, "Render command execution requires the owner thread");
		Require(active, "Render command context has ended");
	}

	std::shared_ptr<RenderTextureResource> Resolve(RenderTargetReference reference,
	                                               const Map<UInt32, std::shared_ptr<RenderTextureResource>>& targets)
	{
		if (reference.texture)
		{
			return reference.texture;
		}
		if (!reference.id)
		{
			Require(bool(cameraTarget), "The render context has no camera target");
			return cameraTarget;
		}
		const auto found = targets.find(reference.id);
		Require(found != targets.end(), "Temporary render target ID is absent or has been released in this context");
		return found->second;
	}

	std::shared_ptr<RenderTextureResource> Temporary(const RenderTextureDescription& description)
	{
		for (auto& entry : pool)
		{
			if (!entry.leased && (!entry.completion || entry.completion->Completed()) &&
			    entry.resource->description == description)
			{
				entry.leased = true;
				++statistics.temporaryTargetsReused;
				return entry.resource;
			}
		}
		auto resource = Take(CreateRenderTexture(*device, description));
		for (auto it = pool.begin();
		     it != pool.end() &&
		     (pool.size() >= 32 || statistics.temporaryBytes + description.ByteSize() > 512u * 1024u * 1024u);)
		{
			if (!it->leased && (!it->completion || it->completion->Completed()))
			{
				statistics.temporaryBytes -= it->resource->description.ByteSize();
				it = pool.erase(it);
			}
			else
			{
				++it;
			}
		}
		Require(pool.size() < 32 && statistics.temporaryBytes + description.ByteSize() <= 512u * 1024u * 1024u,
		        "Temporary target pool exceeds its 32-target or 512 MiB capacity");
		pool.push_back({resource, {}, true});
		statistics.temporaryBytes += description.ByteSize();
		++statistics.temporaryTargetsCreated;
		return resource;
	}

	Program& FullscreenProgram(const RenderCommand& operation)
	{
		Require(bool(operation.material) && bool(operation.material->Shader()),
		        "Fullscreen material has no imported Shader");
		const auto shader = operation.material->Shader();
		Require(operation.pass >= -1, "Fullscreen material Pass is invalid");
		auto ordered = Take(OrderedGraphicsPasses(shader, operation.activePipeline));
		const UInt32 passIndex = operation.pass < 0 ? 0u : UInt32(operation.pass);
		Require(passIndex < ordered.size(), "Fullscreen material Pass is out of range or has no compatible SubShader");
		const auto& selected = ordered[passIndex];
		const auto& pass = selected.programShader->subShaders[selected.subShaderIndex].passes[selected.passIndex];
		const auto variant =
		    std::find_if(pass.variants.begin(), pass.variants.end(), [&](const Shader::ShaderVariantAsset& value)
		                 { return value.keywords == operation.material->Keywords(); });
		Require(variant != pass.variants.end(), "Fullscreen Shader keyword variant is unavailable");
		const auto target = operation.target.texture;
		Require(target && target->color, "Fullscreen drawing needs a color attachment");
		String key = shader->id.ToString() + ":" + shader->sourceHash + ":" + shader->compilerVersion + ":" +
		             pass.identity + ":" + ToUrsaString(std::to_string(passIndex)) + ":" + variant->layoutIdentity +
		             ":" + ToUrsaString(std::to_string(UInt32(target->color->desc.format))) +
		             (operation.hasDepthTarget ? ":depth" : ":color");
		key += ":program=" + selected.programShader->id.ToString() + ":" + selected.programShader->sourceHash + ":" +
		       selected.programShader->compilerVersion;
		key += ":pipeline=" + operation.activePipeline;
		key += ":version=" + ToUrsaString(std::to_string(shader->sourceFormatVersion));
		key += ":alpha=" + ToUrsaString(std::to_string(UInt32(operation.material->Parameters().alphaMode)));
		for (const auto& attachment : operation.colors)
		{
			key += ":attachment=" + ToUrsaString(std::to_string(UInt32(attachment.texture->color->desc.format)));
		}
		if (operation.hasDepthTarget)
		{
			key += ":depthFormat=" +
			       ToUrsaString(std::to_string(UInt32(operation.depthTarget.texture->depth->desc.format)));
		}
		for (const auto& keyword : variant->keywords)
		{
			key += ":kw=" + keyword;
		}
		const auto found = programs.find(key);
		if (found != programs.end())
		{
			return found->second;
		}
		Program result;
		result.shader = shader;
		result.reflection = Take(Shader::BuildShaderBindingLayout(*shader, *variant));
		Require(result.reflection.vertexInputs.empty(),
		        "Fullscreen Pass must use a vertex ID without vertex attributes");
		result.layout = Take(Shader::CreateShaderBindGroupLayout(result.reflection, *device));
		Rhi::GraphicsPipelineDesc description;
		description.colorFormat = target->color->desc.format;
		for (const auto& attachment : operation.colors)
		{
			description.colorFormats.push_back(attachment.texture->color->desc.format);
		}
		description.depthWrite = false;
		description.layout = result.layout;
		Shader::ApplyGraphicsRenderStates(description, selected.renderStates);
		if (operation.material->Parameters().alphaMode == AlphaMode::Blend)
		{
			description.blendState.enabled = true;
			description.blendState.sourceColor = Rhi::BlendFactor::SourceAlpha;
			description.blendState.destinationColor = Rhi::BlendFactor::OneMinusSourceAlpha;
			description.depthWrite = false;
		}
		result.depth = description.depthTest || description.stencil.enabled;
		Require(!result.depth || operation.hasDepthTarget,
		        "Fullscreen Pass requires a missing depth/stencil attachment");
		if (operation.hasDepthTarget)
		{
			description.depthFormat = operation.depthTarget.texture->depth->desc.format;
		}
		for (const auto& stage : variant->stages)
		{
			Require(stage.target == Shader::ShaderTarget::VulkanSpirv && stage.stage != Shader::ProgramStage::Compute,
			        "Fullscreen Pass needs compiled Vulkan vertex and fragment programs");
			auto created = Take(device->CreateShader(
			    {stage.stage == Shader::ProgramStage::Vertex ? Rhi::ShaderStage::Vertex : Rhi::ShaderStage::Fragment,
			     Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint}));
			if (stage.stage == Shader::ProgramStage::Vertex)
			{
				Require(!description.vertex, "Fullscreen Pass has duplicate vertex stages");
				description.vertex = std::move(created);
			}
			else
			{
				Require(!description.fragment, "Fullscreen Pass has duplicate fragment stages");
				description.fragment = std::move(created);
			}
		}
		Require(description.vertex && description.fragment, "Fullscreen Pass is missing a vertex or fragment stage");
		result.pipeline = Take(device->CreateGraphicsPipeline(description));
		if (programs.size() >= 128)
		{
			programs.clear();
		}
		++statistics.pipelinesCreated;
		return programs.emplace(key, std::move(result)).first->second;
	}

	Program& ComputeProgram(const RenderCommand& operation)
	{
		Require(operation.shader && device->Capabilities().compute,
		        "Compute command requires an imported Shader and compute capability");
		const auto* kernel = Take(ComputeKernelAt(*operation.shader, operation.kernel));
		const auto capabilities = device->Capabilities();
		UInt64 invocations = 1;
		for (Size axis = 0; axis < 3; ++axis)
		{
			Require(kernel->threadGroupSize[axis] &&
			            kernel->threadGroupSize[axis] <= capabilities.maxComputeWorkGroupSize[axis],
			        "Compute kernel thread-group dimensions exceed device limits");
			invocations *= kernel->threadGroupSize[axis];
		}
		Require(invocations <= capabilities.maxComputeWorkGroupInvocations,
		        "Compute kernel thread-group invocation count exceeds device limits");
		const auto variant = std::find_if(kernel->variants.begin(), kernel->variants.end(),
		                                  [](const auto& value) { return value.keywords.empty(); });
		Require(variant != kernel->variants.end() && variant->stages.size() == 1 &&
		            variant->stages[0].stage == Shader::ProgramStage::Compute &&
		            variant->stages[0].target == Shader::ShaderTarget::VulkanSpirv,
		        "Compute kernel is missing its compiled default Vulkan program");
		const String key = "compute:" + ComputeScope(operation) + ":" + variant->layoutIdentity;
		const auto found = programs.find(key);
		if (found != programs.end())
		{
			return found->second;
		}
		Program result;
		result.shader = operation.shader;
		result.reflection = Take(Shader::BuildShaderBindingLayout(*operation.shader, *variant));
		result.layout = Take(Shader::CreateShaderBindGroupLayout(result.reflection, *device));
		const auto& stage = variant->stages[0];
		auto compute = Take(
		    device->CreateShader({Rhi::ShaderStage::Compute, Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint}));
		result.pipeline = Take(device->CreateComputePipeline({compute, result.layout}));
		if (programs.size() >= 128)
		{
			programs.clear();
		}
		++statistics.pipelinesCreated;
		return programs.emplace(key, std::move(result)).first->second;
	}

	void ValidateComputeParameter(const Program& program, const RenderGlobalParameter& parameter)
	{
		for (const auto& uniform : program.reflection.uniformBuffers)
		{
			for (const auto& member : uniform.members)
			{
				if (member.name == parameter.name)
				{
					Require(parameter.kind == UniformKind(member) && parameter.bytes.size() == member.byteSize,
					        "Compute uniform value kind or size does not match its reflected member");
					return;
				}
			}
		}
		for (const auto& descriptor : program.reflection.descriptors)
		{
			if (program.reflection.resourceNames.at(descriptor.binding) != parameter.name)
			{
				continue;
			}
			if (descriptor.type == Rhi::BindingType::StorageBuffer)
			{
				Require(parameter.kind == 7 && parameter.buffer,
				        "Compute storage buffer parameter has the wrong resource kind");
				return;
			}
			if (descriptor.type == Rhi::BindingType::StorageTexture ||
			    descriptor.type == Rhi::BindingType::SampledTexture ||
			    descriptor.type == Rhi::BindingType::CombinedImageSampler)
			{
				Require((parameter.kind == 3 || parameter.kind == 4) && parameter.texture.texture,
				        "Compute texture parameter has the wrong resource kind");
				const auto texture = SampledTexture(parameter.texture.texture);
				Require(texture && Rhi::Has(texture->desc.usage, descriptor.type == Rhi::BindingType::StorageTexture
				                                                     ? Rhi::TextureUsage::Storage
				                                                     : Rhi::TextureUsage::Sampled),
				        "Compute texture lacks its required sampling or storage usage");
				if (descriptor.type == Rhi::BindingType::StorageTexture)
				{
					const String& format = program.reflection.storageImageFormats.at(descriptor.binding);
					Require((format == "rgba8" && texture->desc.format == Rhi::Format::RgbA8Unorm) ||
					            (format == "rgba16f" && texture->desc.format == Rhi::Format::RgbA16Float) ||
					            (format == "r32ui" && texture->desc.format == Rhi::Format::R32Uint),
					        "Compute storage image format does not match its reflected kernel declaration");
				}
				return;
			}
		}
		throw std::runtime_error("Compute parameter is absent from kernel reflection: " + ToStdString(parameter.name));
	}

	Rhi::DevicePtr device;
	UniquePtr<RenderFrameQueue> frames;
	std::thread::id ownerThread = std::this_thread::get_id();
	bool active = false;
	std::shared_ptr<RenderTextureResource> cameraTarget;
	std::shared_ptr<RenderTextureResource> windowDepth;
	RenderFrameContext* activeFrame = nullptr;
	std::unique_ptr<SceneDrawEncoder> sceneRenderer;
	FrameCapture* capture = nullptr;
	Rhi::ResourceState finalState = Rhi::ResourceState::ShaderRead;
	std::shared_ptr<RenderTextureResource> currentTarget;
	Vector<RenderTargetReference> currentColors;
	std::shared_ptr<RenderTextureResource> currentDepth;
	Map<UInt32, std::shared_ptr<RenderTextureResource>> temporaryTargets;
	Vector<PoolEntry> pool;
	Vector<RenderCommand> pending;
	Vector<RenderCommand> committed;
	Map<String, Program> programs;
	Map<const MaterialTexture*, TextureUpload> materialTextures;
	Map<String, std::shared_ptr<RenderTextureResource>> defaultTextures;
	Map<UInt32, Rhi::SamplerPtr> samplers;
	RenderCommandStatistics statistics;

	Result<Rhi::FencePtr> Finish(bool captureFrame = true);
};

RenderCommandExecutor::RenderCommandExecutor(std::unique_ptr<Impl> implementation) : impl(std::move(implementation))
{
}

RenderCommandExecutor::~RenderCommandExecutor()
{
	Drain();
}

Result<std::unique_ptr<RenderCommandExecutor>> RenderCommandExecutor::Create(Rhi::DevicePtr device)
{
	try
	{
		Require(bool(device), "Render command executor requires a device");
		return Result<std::unique_ptr<RenderCommandExecutor>>::Success(std::unique_ptr<RenderCommandExecutor>(
		    new RenderCommandExecutor(std::make_unique<Impl>(std::move(device)))));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<RenderCommandExecutor>>::Failure(error.what());
	}
}

Result<void> RenderCommandExecutor::Begin(std::shared_ptr<RenderTextureResource> cameraTarget,
                                          Rhi::ResourceState finalState, const Rhi::SwapchainPtr& swapchain)
{
	RenderFrameContext* acquiredHere = nullptr;
	const auto result = Guard(
	    [&]
	    {
		    Require(std::this_thread::get_id() == impl->ownerThread && !impl->active,
		            "Render context begin requires an idle owner thread");
		    Require(!swapchain || !cameraTarget, "Choose either a window swapchain or an offscreen camera target");
		    if (impl->sceneRenderer)
		    {
			    impl->sceneRenderer->BeginFrame();
			    impl->statistics.meshCacheEntries = impl->sceneRenderer->MeshCacheSize();
		    }
		    if (swapchain)
		    {
			    impl->activeFrame = Take(impl->frames->Begin(swapchain));
			    acquiredHere = impl->activeFrame;
			    const auto& image = impl->activeFrame->Image();
			    Require(bool(image.texture), "Window render target is suspended while its framebuffer is zero sized");
			    const UInt32 width = image.texture->desc.width, height = image.texture->desc.height;
			    if (!impl->windowDepth || impl->windowDepth->depth->desc.width != width ||
			        impl->windowDepth->depth->desc.height != height)
			    {
				    impl->windowDepth = Take(CreateRenderTexture(*impl->device, {width, height, 2}));
			    }
			    cameraTarget = std::make_shared<RenderTextureResource>();
			    cameraTarget->description = {width, height, 0, 32};
			    cameraTarget->color = image.texture;
			    cameraTarget->colorState = image.initialState;
			    cameraTarget->depth = impl->windowDepth->depth;
			    cameraTarget->depthBacking = impl->windowDepth;
			    finalState = Rhi::ResourceState::Present;
		    }
		    impl->cameraTarget = std::move(cameraTarget);
		    impl->currentTarget = impl->cameraTarget;
		    impl->currentColors.clear();
		    impl->currentDepth.reset();
		    if (impl->cameraTarget)
		    {
			    if (impl->cameraTarget->color)
			    {
				    impl->currentColors.push_back({impl->cameraTarget, 0});
			    }
			    if (impl->cameraTarget->depth)
			    {
				    impl->currentDepth = impl->cameraTarget;
			    }
		    }
		    impl->finalState = finalState;
		    impl->active = true;
		    for (auto it = impl->materialTextures.begin(); it != impl->materialTextures.end();)
		    {
			    if (it->second.source.expired())
			    {
				    it = impl->materialTextures.erase(it);
			    }
			    else
			    {
				    ++it;
			    }
		    }
	    });
	if (!result && acquiredHere)
	{
		impl->frames->Abort();
		impl->activeFrame = nullptr;
		impl->cameraTarget.reset();
		impl->currentTarget.reset();
		impl->currentColors.clear();
		impl->currentDepth.reset();
		impl->active = false;
	}
	return result;
}

Result<void> RenderCommandExecutor::ResetCamera(std::optional<CameraData> camera)
{
	return Guard(
	    [&]
	    {
		    impl->RequireActive();
		    Require(impl->pending.empty(),
		            "Submit the previous camera's command segment before setting up another camera");
		    impl->currentTarget = impl->cameraTarget;
		    impl->currentColors.clear();
		    impl->currentDepth.reset();
		    if (impl->cameraTarget)
		    {
			    if (impl->cameraTarget->color)
			    {
				    impl->currentColors.push_back({impl->cameraTarget, 0});
			    }
			    if (impl->cameraTarget->depth)
			    {
				    impl->currentDepth = impl->cameraTarget;
			    }
		    }
		    RenderCommand boundary;
		    boundary.opcode = RenderCommandOpcode::ResetCameraState;
		    if (camera)
		    {
			    boundary.debugName = CameraRegionName(*camera);
		    }
		    impl->committed.push_back(std::move(boundary));
	    });
}

Result<void> RenderCommandExecutor::Execute(const Vector<RenderCommand>& commands)
{
	Vector<const RenderTextureResource*> leasedBefore;
	for (const auto& entry : impl->pool)
	{
		if (entry.leased)
		{
			leasedBefore.push_back(entry.resource.get());
		}
	}
	const auto result = Guard(
	    [&]
	    {
		    impl->RequireActive();
		    Require(impl->pending.size() + impl->committed.size() + commands.size() <= 65536,
		            "Render context command capacity exceeded");
		    auto targets = impl->temporaryTargets;
		    auto current = impl->currentTarget;
		    auto currentColors = impl->currentColors;
		    auto currentDepth = impl->currentDepth;
		    Vector<RenderCommand> accepted;
		    Vector<String> sampleStack;
		    for (auto operation : commands)
		    {
			    switch (operation.opcode)
			    {
				    case RenderCommandOpcode::BeginSample:
					    Require(!operation.debugName.empty() && operation.debugName.size() <= 1024 &&
					                operation.debugName.find('\0') == String::npos,
					            "BeginSample name is invalid");
					    sampleStack.push_back(operation.debugName);
					    break;
				    case RenderCommandOpcode::EndSample:
					    Require(!sampleStack.empty() && sampleStack.back() == operation.debugName,
					            "EndSample does not match the most recently opened sample");
					    sampleStack.pop_back();
					    break;
				    case RenderCommandOpcode::GetTemporaryTarget:
					    Require(operation.target.id && targets.find(operation.target.id) == targets.end(),
					            "Temporary target ID is zero or already active");
					    targets.emplace(operation.target.id, impl->Temporary(operation.description));
					    continue;
				    case RenderCommandOpcode::ReleaseTemporaryTarget:
				    {
					    const auto found = targets.find(operation.target.id);
					    Require(operation.target.id && found != targets.end(),
					            "Released temporary target ID is not active");
					    if (current == found->second || currentDepth == found->second ||
					        std::any_of(currentColors.begin(), currentColors.end(),
					                    [&](const auto& value) { return value.texture == found->second; }))
					    {
						    current.reset();
						    currentColors.clear();
						    currentDepth.reset();
					    }
					    targets.erase(found);
					    continue;
				    }
				    case RenderCommandOpcode::SetRenderTarget:
					    current = impl->Resolve(operation.target, targets);
					    currentColors.clear();
					    currentDepth.reset();
					    if (current->color)
					    {
						    currentColors.push_back({current, 0});
					    }
					    if (current->depth)
					    {
						    currentDepth = current;
					    }
					    continue;
				    case RenderCommandOpcode::SetRenderTargets:
				    {
					    Require(operation.colors.size() <= impl->device->Capabilities().maxColorAttachments &&
					                operation.colors.size() <= 8 &&
					                (!operation.colors.empty() || operation.hasDepthTarget),
					            "Render target attachment count exceeds capabilities");
					    currentColors = operation.colors;
					    currentDepth =
					        operation.hasDepthTarget ? impl->Resolve(operation.depthTarget, targets) : nullptr;
					    if (currentDepth)
					    {
						    Require(bool(currentDepth->depth), "Depth target has no depth attachment");
					    }
					    for (auto& attachment : currentColors)
					    {
						    attachment.texture = impl->Resolve(attachment, targets);
						    Require(bool(attachment.texture->color), "Color target has no color attachment");
					    }
					    current = currentColors.empty() ? currentDepth : currentColors.front().texture;
					    const auto reference = SampledTexture(current);
					    for (Size index = 0; index < currentColors.size(); ++index)
					    {
						    const auto color = currentColors[index].texture->color;
						    Require(color->desc.width == reference->desc.width &&
						                color->desc.height == reference->desc.height,
						            "Render target attachment dimensions must match");
						    for (Size previous = 0; previous < index; ++previous)
						    {
							    Require(color != currentColors[previous].texture->color,
							            "Render target contains a duplicate color attachment");
						    }
					    }
					    Require(!currentDepth || (currentDepth->depth->desc.width == reference->desc.width &&
					                              currentDepth->depth->desc.height == reference->desc.height),
					            "Render target depth dimensions must match its colors");
					    continue;
				    }
				    case RenderCommandOpcode::ClearTarget:
					    Require(current && operation.clearFlags && !(operation.clearFlags & ~7u),
					            "ClearTarget has no active target or aspects");
					    Require(!(operation.clearFlags & 1u) || !currentColors.empty(),
					            "Color clear requires a color attachment");
					    Require(!(operation.clearFlags & 2u) || bool(currentDepth),
					            "Depth clear requires a depth attachment");
					    Require(!(operation.clearFlags & 4u) ||
					                (currentDepth && currentDepth->depth->desc.format == Rhi::Format::D32FloatS8Uint),
					            "Stencil clear requires a stencil attachment");
					    Require(std::isfinite(operation.clearDepth) && operation.clearDepth >= 0 &&
					                operation.clearDepth <= 1 && std::isfinite(operation.color.red) &&
					                std::isfinite(operation.color.green) && std::isfinite(operation.color.blue) &&
					                std::isfinite(operation.color.alpha),
					            "ClearTarget values are invalid");
					    operation.target.texture = current;
					    operation.colors = currentColors;
					    operation.depthTarget.texture = currentDepth;
					    operation.hasDepthTarget = bool(currentDepth);
					    break;
				    case RenderCommandOpcode::CopyTexture:
				    case RenderCommandOpcode::Blit:
					    operation.source.texture = impl->Resolve(operation.source, targets);
					    operation.target.texture = impl->Resolve(operation.target, targets);
					    Require(SampledTexture(operation.source.texture) != SampledTexture(operation.target.texture),
					            "Texture operation cannot read and write the same target");
					    if (operation.opcode == RenderCommandOpcode::CopyTexture)
					    {
						    const auto source = SampledTexture(operation.source.texture);
						    const auto target = SampledTexture(operation.target.texture);
						    Require(source && target && source->desc.width == target->desc.width &&
						                source->desc.height == target->desc.height &&
						                source->desc.format == target->desc.format,
						            "CopyTexture format and dimensions must match");
					    }
					    break;
				    case RenderCommandOpcode::DrawFullscreen:
					    Require(current && current->color && !currentColors.empty(),
					            "DrawFullscreen requires a current color target");
					    operation.target.texture = current;
					    operation.colors = currentColors;
					    operation.depthTarget.texture = currentDepth;
					    operation.hasDepthTarget = bool(currentDepth);
					    break;
				    case RenderCommandOpcode::DrawRendererList:
				    case RenderCommandOpcode::DrawMesh:
					    Require(current && (!currentColors.empty() || currentDepth),
					            "Scene drawing requires a current attachment target");
					    operation.target.texture = current;
					    operation.colors = currentColors;
					    operation.depthTarget.texture = currentDepth;
					    operation.hasDepthTarget = bool(currentDepth);
					    for (Float32 value : operation.camera.viewProjection.values)
					    {
						    Require(std::isfinite(value), "Scene camera matrix contains a non-finite value");
					    }
					    for (auto& draw : operation.draws)
					    {
						    Require(draw.object.mesh && draw.object.meshSnapshot &&
						                draw.object.subMesh < draw.object.meshSnapshot->data.subMeshes.size(),
						            "Scene draw mesh snapshot or submesh is invalid");
						    for (Float32 value : draw.object.world.values)
						    {
							    Require(std::isfinite(value), "Scene draw transform contains a non-finite value");
						    }
						    if (draw.object.material)
						    {
							    draw.materialIdentity = draw.object.material->LocalIdentity();
							    draw.materialVersion = draw.object.material->Version();
							    draw.object.material = draw.object.material->Clone();
							    draw.object.shader = draw.object.material->Shader();
						    }
					    }
					    break;
				    case RenderCommandOpcode::CopyBuffer:
					    Require(operation.sourceBuffer && operation.targetBuffer &&
					                operation.sourceBuffer->buffer != operation.targetBuffer->buffer &&
					                operation.byteSize &&
					                operation.sourceOffset <= operation.sourceBuffer->buffer->desc.size &&
					                operation.byteSize <=
					                    operation.sourceBuffer->buffer->desc.size - operation.sourceOffset &&
					                operation.targetOffset <= operation.targetBuffer->buffer->desc.size &&
					                operation.byteSize <=
					                    operation.targetBuffer->buffer->desc.size - operation.targetOffset,
					            "CopyBuffer resource, offsets or size are invalid");
					    break;
				    case RenderCommandOpcode::SetGlobalParameters:
				    case RenderCommandOpcode::SetComputeParameters:
					    Require(!operation.global.name.empty() && operation.global.name.size() <= 1024 &&
					                operation.global.name.find('\0') == String::npos,
					            "Global parameter name is invalid");
					    if (operation.global.kind == 3 || operation.global.kind == 4)
					    {
						    operation.global.texture.texture = impl->Resolve(operation.global.texture, targets);
					    }
					    else if (operation.global.kind == 7)
					    {
						    Require(
						        operation.global.buffer && operation.global.buffer->buffer &&
						            Rhi::Has(operation.global.buffer->buffer->desc.usage, Rhi::BufferUsage::Storage),
						        "Global buffer must have storage usage");
					    }
					    else
					    {
						    const bool validMatrixArray =
						        operation.global.kind == 8 && operation.global.bytes.size() >= 64 &&
						        operation.global.bytes.size() <= 16 * 64 && operation.global.bytes.size() % 64 == 0;
						    const bool validVectorArray =
						        operation.global.kind == 9 && operation.global.bytes.size() >= 16 &&
						        operation.global.bytes.size() <= 16 * 16 && operation.global.bytes.size() % 16 == 0;
						    Require((operation.global.kind == 1 && operation.global.bytes.size() == 4) ||
						                (operation.global.kind == 2 && operation.global.bytes.size() == 16) ||
						                (operation.global.kind == 5 && operation.global.bytes.size() == 64) ||
						                (operation.global.kind == 6 && operation.global.bytes.size() == 4) ||
						                validMatrixArray || validVectorArray,
						            "Global parameter value size is invalid");
						    for (Size offset = 0; operation.global.kind != 6 && offset < operation.global.bytes.size();
						         offset += sizeof(Float32))
						    {
							    Float32 value = 0;
							    std::memcpy(&value, operation.global.bytes.data() + offset, sizeof(value));
							    Require(std::isfinite(value), "Global parameter contains a non-finite value");
						    }
					    }
					    if (operation.opcode == RenderCommandOpcode::SetComputeParameters)
					    {
						    const auto& program = impl->ComputeProgram(operation);
						    impl->ValidateComputeParameter(program, operation.global);
					    }
					    break;
				    case RenderCommandOpcode::SetViewport:
					    Require(std::isfinite(operation.viewport.x) && std::isfinite(operation.viewport.y) &&
					                std::isfinite(operation.viewport.width) &&
					                std::isfinite(operation.viewport.height) &&
					                std::isfinite(operation.viewport.minimumDepth) &&
					                std::isfinite(operation.viewport.maximumDepth) && operation.viewport.width > 0 &&
					                operation.viewport.height > 0 && operation.viewport.minimumDepth >= 0 &&
					                operation.viewport.maximumDepth <= 1 &&
					                operation.viewport.minimumDepth <= operation.viewport.maximumDepth,
					            "Viewport dimensions or depth range are invalid");
					    break;
				    case RenderCommandOpcode::SetScissor:
					    Require(operation.scissor.x >= 0 && operation.scissor.y >= 0 && operation.scissor.width &&
					                operation.scissor.height,
					            "Scissor position or size is invalid");
					    break;
				    case RenderCommandOpcode::DispatchCompute:
					    impl->ComputeProgram(operation);
					    for (Size axis = 0; axis < 3; ++axis)
					    {
						    Require(operation.groups[axis] &&
						                operation.groups[axis] <=
						                    impl->device->Capabilities().maxComputeWorkGroupCount[axis],
						            "Compute dispatch dimensions exceed device limits");
					    }
					    break;
				    default:
					    throw std::runtime_error("Render command has no executable encoding");
			    }
			    if (operation.opcode == RenderCommandOpcode::Blit ||
			        operation.opcode == RenderCommandOpcode::DrawFullscreen)
			    {
				    Require(bool(operation.material), "Fullscreen drawing requires a material");
				    if (operation.opcode == RenderCommandOpcode::Blit)
				    {
					    operation.colors = {{operation.target.texture, 0}};
					    operation.hasDepthTarget = bool(operation.target.texture->depth);
					    operation.depthTarget.texture = operation.hasDepthTarget ? operation.target.texture : nullptr;
				    }
				    operation.material = operation.material->Clone();
				    impl->FullscreenProgram(operation);
			    }
			    accepted.push_back(std::move(operation));
		    }
		    Require(sampleStack.empty(), "Render command batch contains an unclosed profiling sample");
		    impl->pending.insert(impl->pending.end(), accepted.begin(), accepted.end());
		    impl->temporaryTargets = std::move(targets);
		    impl->currentTarget = std::move(current);
		    impl->currentColors = std::move(currentColors);
		    impl->currentDepth = std::move(currentDepth);
	    });
	if (!result)
	{
		for (auto& entry : impl->pool)
		{
			if (std::find(leasedBefore.begin(), leasedBefore.end(), entry.resource.get()) == leasedBefore.end())
			{
				entry.leased = false;
			}
		}
	}
	return result;
}

Result<void> RenderCommandExecutor::Submit()
{
	return Guard(
	    [&]
	    {
		    impl->RequireActive();
		    impl->committed.insert(impl->committed.end(), impl->pending.begin(), impl->pending.end());
		    impl->pending.clear();
		    ++impl->statistics.logicalSubmissions;
	    });
}

Result<Rhi::FencePtr> RenderCommandExecutor::End()
{
	try
	{
		impl->RequireActive();
		Require(impl->pending.empty(), "Render context contains an unsubmitted command segment");
		Require(impl->temporaryTargets.empty(), "Render context contains unreleased temporary target IDs");
		auto finished = impl->Finish();
		if (finished)
		{
			const auto& frames = impl->frames->Statistics();
			Profiling::Plot("Rendering.FrameSlotWaits", Float64(frames.slotWaits));
			Profiling::Plot("Rendering.FrameSlotReuses", Float64(frames.slotReuses));
			Profiling::Plot("Rendering.UploadBuffersCreated", Float64(frames.uploadBuffersCreated));
			Profiling::Plot("Rendering.UploadCapacity", Float64(frames.uploadCapacity), Profiling::PlotUnit::Bytes);
			Profiling::Plot("Rendering.UploadedBytes", Float64(frames.uploadedBytes), Profiling::PlotUnit::Bytes);
			Profiling::Plot("Rendering.TemporaryTargetReuses", Float64(impl->statistics.temporaryTargetsReused));
			Profiling::Plot("Rendering.PhysicalSubmissions", Float64(impl->statistics.physicalSubmissions));
		}
		return finished;
	}
	catch (const std::exception& error)
	{
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<Rhi::FencePtr> RenderCommandExecutor::Abort()
{
	try
	{
		impl->RequireActive();
		impl->pending.clear();
		impl->temporaryTargets.clear();
		return impl->Finish(false);
	}
	catch (const std::exception& error)
	{
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<void> RenderCommandExecutor::Drain()
{
	if (impl->active)
	{
		auto ended = Abort();
		if (!ended)
		{
			return Result<void>::Failure(ended.error);
		}
	}
	return impl->frames->Drain();
}

const RenderCommandStatistics& RenderCommandExecutor::Statistics() const
{
	return impl->statistics;
}

bool RenderCommandExecutor::IsActive() const
{
	return impl->active;
}

std::shared_ptr<RenderTextureResource> RenderCommandExecutor::CameraTarget() const
{
	return impl->cameraTarget;
}

void RenderCommandExecutor::SetFrameCapture(FrameCapture* capture)
{
	impl->capture = capture;
}

Result<Rhi::FencePtr> RenderCommandExecutor::Impl::Finish(bool captureFrame)
{
	struct TextureState
	{
		Rhi::TexturePtr resource;
		Rhi::ResourceState value;
		Rhi::ResourceState* output;
	};

	struct BufferState
	{
		std::shared_ptr<GraphicsBufferResource> resource;
		Rhi::ResourceState value;
	};

	std::map<const Rhi::Texture*, TextureState> textures;
	std::map<const Rhi::Buffer*, BufferState> buffers;
	Rhi::FencePtr completion;
	try
	{
		if (!committed.empty())
		{
			auto* frame = activeFrame ? activeFrame : Take(frames->Begin());
			activeFrame = frame;
			const auto command = frame->Command();
			auto transitionTexture =
			    [&](const std::shared_ptr<RenderTextureResource>& resource, bool depth, Rhi::ResourceState after)
			{
				const auto texture = depth ? resource->depth : resource->color;
				Require(bool(texture), "Texture operation requested a missing attachment");
				auto* output = depth ? &resource->DepthState() : &resource->colorState;
				auto [state, inserted] = textures.try_emplace(texture.get(), TextureState{texture, *output, output});
				(void)inserted;
				Done(command->Barrier(texture, state->second.value, after));
				state->second.value = after;
				Done(frames->Retain(resource));
			};
			auto transitionBuffer =
			    [&](const std::shared_ptr<GraphicsBufferResource>& resource, Rhi::ResourceState after)
			{
				auto [state, inserted] =
				    buffers.try_emplace(resource->buffer.get(), BufferState{resource, resource->state});
				(void)inserted;
				Done(command->Barrier(resource->buffer, state->second.value, after));
				state->second.value = after;
			};
			auto materialTexture = [&](const MaterialPropertyValue& property) -> std::shared_ptr<RenderTextureResource>
			{
				std::shared_ptr<RenderTextureResource> resource;
				Vector<UInt8> pixels;
				if (property.texture)
				{
					auto found = materialTextures.find(property.texture.get());
					if (found != materialTextures.end())
					{
						resource = found->second.resource;
					}
					else
					{
						resource = std::make_shared<RenderTextureResource>();
						resource->color = Take(device->CreateTexture(
						    {property.texture->GetWidth(), property.texture->GetHeight(),
						     property.texture->GetColorSpace() == TextureColorSpace::Srgb ? Rhi::Format::RgbA8Srgb
						                                                                  : Rhi::Format::RgbA8Unorm,
						     Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination,
						     property.texture->GetMipLevels()}));
						materialTextures.emplace(property.texture.get(), TextureUpload{property.texture, resource});
					}
					pixels = property.texture->GetPixels();
					for (const auto& mip : property.texture->GetMipmaps())
					{
						pixels.insert(pixels.end(), mip.pixels.begin(), mip.pixels.end());
					}
				}
				else
				{
					auto [found, inserted] = defaultTextures.try_emplace(property.textureDefault);
					if (inserted)
					{
						found->second = std::make_shared<RenderTextureResource>();
						found->second->color = Take(device->CreateTexture(
						    {1, 1, Rhi::Format::RgbA8Unorm,
						     Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination}));
					}
					resource = found->second;
					const UInt8 value = property.textureDefault == "black" ? 0 : 255;
					pixels = {value, value, value, 255};
				}
				const auto state = textures.find(resource->color.get());
				if ((state == textures.end() ? resource->colorState : state->second.value) ==
				    Rhi::ResourceState::Undefined)
				{
					auto upload = Take(frames->Upload(pixels.data(), pixels.size(), 4));
					transitionTexture(resource, false, Rhi::ResourceState::TransferDestination);
					Size sourceOffset = upload.offset;
					for (UInt32 mip = 0; mip < resource->color->desc.mipLevels; ++mip)
					{
						Done(command->CopyBufferToTexture(upload.buffer, resource->color, mip, sourceOffset));
						sourceOffset += Size((std::max)(1u, resource->color->desc.width >> mip)) *
						                (std::max)(1u, resource->color->desc.height >> mip) * 4;
					}
				}
				return resource;
			};
			Map<String, RenderGlobalParameter> globals;
			Map<String, Map<String, RenderGlobalParameter>> computeParameters;
			std::optional<Rhi::ViewportRect> viewport;
			std::optional<Rhi::ScissorRect> scissor;
			String pendingCameraName;
			bool cameraRegionOpen = false;
			auto closeCameraRegion = [&]
			{
				if (cameraRegionOpen)
				{
					Done(command->EndDebugRegion());
					cameraRegionOpen = false;
				}
			};
			auto openCameraRegion = [&]
			{
				if (!cameraRegionOpen && !pendingCameraName.empty())
				{
					Done(command->BeginDebugRegion(pendingCameraName));
					cameraRegionOpen = true;
				}
			};
			for (const auto& operation : committed)
			{
				const auto target = operation.target.texture;
				switch (operation.opcode)
				{
					case RenderCommandOpcode::ResetCameraState:
						closeCameraRegion();
						pendingCameraName = operation.debugName;
						globals.clear();
						computeParameters.clear();
						viewport.reset();
						scissor.reset();
						break;
					case RenderCommandOpcode::BeginSample:
						openCameraRegion();
						Done(command->BeginDebugRegion(operation.debugName));
						break;
					case RenderCommandOpcode::EndSample:
						Done(command->EndDebugRegion());
						break;
					case RenderCommandOpcode::SetGlobalParameters:
						globals[operation.global.name] = operation.global;
						break;
					case RenderCommandOpcode::SetComputeParameters:
						computeParameters[ComputeScope(operation)][operation.global.name] = operation.global;
						break;
					case RenderCommandOpcode::DrawRendererList:
					case RenderCommandOpcode::DrawMesh:
					{
						if (operation.draws.empty())
						{
							break;
						}
						if (!sceneRenderer)
						{
							sceneRenderer = Take(SceneDrawEncoder::Create(device));
						}
						openCameraRegion();
						SceneDrawEncoding encoding;
						encoding.frames = frames.get();
						encoding.globals = &globals;
						encoding.viewport = viewport;
						encoding.scissor = scissor;
						for (const auto& attachment : operation.colors)
						{
							transitionTexture(attachment.texture, false, Rhi::ResourceState::ColorAttachment);
							encoding.target.colors.push_back({attachment.texture->color, Rhi::AttachmentLoadOp::Load});
						}
						if (operation.hasDepthTarget)
						{
							transitionTexture(operation.depthTarget.texture, true, Rhi::ResourceState::DepthAttachment);
							encoding.target.depthAttachment.texture = operation.depthTarget.texture->depth;
							encoding.target.depthAttachment.load = Rhi::AttachmentLoadOp::Load;
							encoding.target.depthAttachment.stencilLoad = Rhi::AttachmentLoadOp::Load;
						}
						encoding.uploadTexture = [&](const MaterialTexturePtr& source)
						{
							auto resource = materialTexture(MaterialPropertyValue::Texture2D({}, source));
							transitionTexture(resource, false, Rhi::ResourceState::ShaderRead);
							return Result<Rhi::TexturePtr>::Success(resource->color);
						};
						encoding.textureBarrier =
						    [&](const std::shared_ptr<RenderTextureResource>& resource, Rhi::ResourceState state)
						{
							transitionTexture(resource, !resource->color, state);
							return Result<void>{};
						};
						encoding.bufferBarrier =
						    [&](const std::shared_ptr<GraphicsBufferResource>& resource, Rhi::ResourceState state)
						{
							transitionBuffer(resource, state);
							return Result<void>{};
						};
						Done(sceneRenderer->RecordSceneDraws(command, operation.draws, operation.camera,
						                                     operation.lighting, encoding));
						statistics.meshCacheEntries = sceneRenderer->MeshCacheSize();
						for (const auto& draw : operation.draws)
						{
							if (draw.object.material)
							{
								Done(frames->Retain(draw.object.material));
							}
							Done(frames->Retain(draw.object.mesh));
						}
						break;
					}
					case RenderCommandOpcode::DispatchCompute:
					{
						openCameraRegion();
						auto& program = ComputeProgram(operation);
						const auto& parameters = computeParameters[ComputeScope(operation)];
						Vector<Rhi::BindingResource> bindings;
						Map<const Rhi::Texture*, bool> boundTextures;
						for (const auto& descriptor : program.reflection.descriptors)
						{
							Rhi::BindingResource binding;
							binding.binding = descriptor.binding;
							if (descriptor.type == Rhi::BindingType::UniformBuffer)
							{
								const auto uniform = std::find_if(
								    program.reflection.uniformBuffers.begin(), program.reflection.uniformBuffers.end(),
								    [&](const auto& value) { return value.binding == descriptor.binding; });
								Require(uniform != program.reflection.uniformBuffers.end(),
								        "Compute uniform buffer reflection is missing");
								Vector<UInt8> bytes(uniform->byteSize, 0);
								for (const auto& member : uniform->members)
								{
									const auto parameter = parameters.find(member.name);
									if (parameter != parameters.end())
									{
										Require(parameter->second.kind == UniformKind(member) &&
										            parameter->second.bytes.size() == member.byteSize &&
										            member.byteOffset <= bytes.size() &&
										            member.byteSize <= bytes.size() - member.byteOffset,
										        "Compute uniform value kind, size or reflected offset is invalid");
										Done(Shader::WriteShaderUniformValue(
										    bytes, member, parameter->second.bytes.data(), member.byteSize));
									}
								}
								auto upload =
								    Take(frames->Upload(bytes.data(), bytes.size(),
								                        Size(device->Capabilities().minUniformBufferOffsetAlignment)));
								binding.buffer = upload.buffer;
								binding.offset = upload.offset;
								binding.size = upload.size;
							}
							else if (descriptor.type == Rhi::BindingType::Sampler)
							{
								auto [sampler, inserted] = samplers.try_emplace(1);
								if (inserted)
								{
									sampler->second = Take(device->CreateSampler({true, false}));
								}
								binding.sampler = sampler->second;
							}
							else
							{
								const String& name = program.reflection.resourceNames.at(descriptor.binding);
								const auto parameter = parameters.find(name);
								Require(parameter != parameters.end(),
								        "Compute kernel is missing a required resource parameter");
								ValidateComputeParameter(program, parameter->second);
								if (descriptor.type == Rhi::BindingType::StorageBuffer)
								{
									transitionBuffer(parameter->second.buffer, Rhi::ResourceState::StorageReadWrite);
									binding.buffer = parameter->second.buffer->buffer;
								}
								else
								{
									const auto resource = parameter->second.texture.texture;
									binding.texture = SampledTexture(resource);
									const bool writable = descriptor.type == Rhi::BindingType::StorageTexture;
									const auto previous = boundTextures.find(binding.texture.get());
									Require(previous == boundTextures.end() || previous->second == writable,
									        "Compute kernel cannot sample and write the same texture through separate "
									        "bindings");
									boundTextures[binding.texture.get()] = writable;
									transitionTexture(resource, !resource->color,
									                  writable ? Rhi::ResourceState::ShaderWrite
									                           : Rhi::ResourceState::ShaderRead);
									if (descriptor.type == Rhi::BindingType::CombinedImageSampler)
									{
										auto [sampler, inserted] = samplers.try_emplace(1);
										if (inserted)
										{
											sampler->second = Take(device->CreateSampler({true, false}));
										}
										binding.sampler = sampler->second;
									}
								}
							}
							bindings.push_back(std::move(binding));
						}
						Done(command->BindPipeline(program.pipeline));
						if (!bindings.empty())
						{
							Done(command->BindGroup(
							    Take(command->CreateTransientBindGroup({program.layout, bindings}))));
						}
						Done(command->Dispatch(operation.groups[0], operation.groups[1], operation.groups[2]));
						Done(frames->Retain(std::const_pointer_cast<Shader::ShaderAsset>(operation.shader)));
						break;
					}
					case RenderCommandOpcode::SetViewport:
						viewport = operation.viewport;
						break;
					case RenderCommandOpcode::SetScissor:
						scissor = operation.scissor;
						break;
					case RenderCommandOpcode::ClearTarget:
					{
						openCameraRegion();
						Rhi::RenderTarget pass;
						pass.renderArea = scissor;
						for (const auto& attachment : operation.colors)
						{
							transitionTexture(attachment.texture, false, Rhi::ResourceState::ColorAttachment);
							pass.colors.push_back(
							    {attachment.texture->color,
							     operation.clearFlags & 1u ? Rhi::AttachmentLoadOp::Clear : Rhi::AttachmentLoadOp::Load,
							     Rhi::AttachmentStoreOp::Store,
							     {operation.color.red, operation.color.green, operation.color.blue,
							      operation.color.alpha}});
						}
						if (operation.hasDepthTarget)
						{
							transitionTexture(operation.depthTarget.texture, true, Rhi::ResourceState::DepthAttachment);
							pass.depthAttachment.texture = operation.depthTarget.texture->depth;
							pass.depthAttachment.load =
							    operation.clearFlags & 2u ? Rhi::AttachmentLoadOp::Clear : Rhi::AttachmentLoadOp::Load;
							pass.depthAttachment.stencilLoad = operation.clearFlags & 4u
							                                       ? Rhi::AttachmentLoadOp::Clear
							                                       : Rhi::AttachmentLoadOp::DontCare;
							pass.depthAttachment.clearDepth = operation.clearDepth;
							pass.depthAttachment.clearStencil = operation.clearStencil;
						}
						Done(command->BeginRendering(pass));
						Done(command->EndRendering());
						break;
					}
					case RenderCommandOpcode::CopyTexture:
						openCameraRegion();
						transitionTexture(operation.source.texture, !operation.source.texture->color,
						                  Rhi::ResourceState::TransferSource);
						transitionTexture(target, !target->color, Rhi::ResourceState::TransferDestination);
						Done(command->CopyTexture(SampledTexture(operation.source.texture), SampledTexture(target)));
						break;
					case RenderCommandOpcode::CopyBuffer:
						openCameraRegion();
						transitionBuffer(operation.sourceBuffer, Rhi::ResourceState::TransferSource);
						transitionBuffer(operation.targetBuffer, Rhi::ResourceState::TransferDestination);
						Done(command->CopyBuffer(operation.sourceBuffer->buffer, operation.targetBuffer->buffer,
						                         Size(operation.byteSize), Size(operation.sourceOffset),
						                         Size(operation.targetOffset)));
						break;
					case RenderCommandOpcode::Blit:
					case RenderCommandOpcode::DrawFullscreen:
					{
						openCameraRegion();
						auto& program = FullscreenProgram(operation);
						Vector<Rhi::BindingResource> bindings;
						const auto samplerSettings = operation.material->Parameters().sampler;
						const UInt32 samplerKey =
						    (samplerSettings.linear ? 1u : 0u) | (samplerSettings.repeat ? 2u : 0u);
						auto [sampler, inserted] = samplers.try_emplace(samplerKey);
						if (inserted)
						{
							sampler->second =
							    Take(device->CreateSampler({samplerSettings.linear, samplerSettings.repeat}));
						}
						for (const auto& descriptor : program.reflection.descriptors)
						{
							Rhi::BindingResource binding;
							binding.binding = descriptor.binding;
							if (descriptor.type == Rhi::BindingType::UniformBuffer)
							{
								auto bytes = Take(Shader::PackMaterialUniformBuffer(
								    *program.shader, program.reflection, *operation.material, descriptor.binding));
								const auto uniform = std::find_if(
								    program.reflection.uniformBuffers.begin(), program.reflection.uniformBuffers.end(),
								    [&](const auto& value) { return value.binding == descriptor.binding; });
								for (const auto& member : uniform->members)
								{
									const auto parameter = globals.find(member.name);
									if (parameter != globals.end() && !parameter->second.bytes.empty())
									{
										if (parameter->second.kind != UniformKind(member) ||
										    parameter->second.bytes.size() != member.byteSize ||
										    member.byteOffset > bytes.size() ||
										    member.byteSize > bytes.size() - member.byteOffset)
										{
											throw std::runtime_error(
											    "Fullscreen global parameter " + ToStdString(member.name) +
											    " does not match reflected type " + ToStdString(member.type) +
											    " (kind " + std::to_string(parameter->second.kind) + ", size " +
											    std::to_string(parameter->second.bytes.size()) + ", expected " +
											    std::to_string(member.byteSize) + ")");
										}
										Done(Shader::WriteShaderUniformValue(
										    bytes, member, parameter->second.bytes.data(), member.byteSize));
									}
								}
								auto upload =
								    Take(frames->Upload(bytes.data(), bytes.size(),
								                        Size(device->Capabilities().minUniformBufferOffsetAlignment)));
								binding.buffer = upload.buffer;
								binding.offset = upload.offset;
								binding.size = upload.size;
							}
							else if (descriptor.type == Rhi::BindingType::Sampler)
							{
								binding.sampler = sampler->second;
							}
							else if (descriptor.type == Rhi::BindingType::StorageBuffer)
							{
								const String& name = program.reflection.resourceNames.at(descriptor.binding);
								const auto parameter = globals.find(name);
								Require(parameter != globals.end() && parameter->second.buffer,
								        "Fullscreen Shader storage buffer is missing");
								transitionBuffer(parameter->second.buffer, Rhi::ResourceState::StorageReadWrite);
								binding.buffer = parameter->second.buffer->buffer;
							}
							else if (descriptor.type == Rhi::BindingType::SampledTexture ||
							         descriptor.type == Rhi::BindingType::CombinedImageSampler)
							{
								const String& name = program.reflection.resourceNames.at(descriptor.binding);
								std::shared_ptr<RenderTextureResource> texture;
								const auto parameter = globals.find(name);
								if (parameter != globals.end() && parameter->second.texture.texture)
								{
									texture = parameter->second.texture.texture;
								}
								else if (operation.source.texture &&
								         (name == "_MainTex" || name == "_Source" || name == "_BlitTexture"))
								{
									texture = operation.source.texture;
								}
								else
								{
									texture =
									    materialTexture(Take(operation.material->PropertyValue(name, *program.shader)));
								}
								binding.texture = SampledTexture(texture);
								Require(binding.texture &&
								            (!operation.hasDepthTarget ||
								             binding.texture != operation.depthTarget.texture->depth) &&
								            std::none_of(operation.colors.begin(), operation.colors.end(),
								                         [&](const auto& attachment)
								                         { return binding.texture == attachment.texture->color; }),
								        "Fullscreen Pass cannot sample one of its writable attachments");
								transitionTexture(texture, !texture->color, Rhi::ResourceState::ShaderRead);
								if (descriptor.type == Rhi::BindingType::CombinedImageSampler)
								{
									binding.sampler = sampler->second;
								}
							}
							else
							{
								throw std::runtime_error("Fullscreen Shader requests an unsupported resource binding");
							}
							bindings.push_back(std::move(binding));
						}
						Rhi::BindGroupPtr group;
						if (!bindings.empty())
						{
							group = Take(command->CreateTransientBindGroup({program.layout, bindings}));
						}
						Rhi::RenderTarget pass;
						for (const auto& attachment : operation.colors)
						{
							transitionTexture(attachment.texture, false, Rhi::ResourceState::ColorAttachment);
							pass.colors.push_back({attachment.texture->color, Rhi::AttachmentLoadOp::Load});
						}
						if (program.depth)
						{
							transitionTexture(operation.depthTarget.texture, true, Rhi::ResourceState::DepthAttachment);
							pass.depthAttachment.texture = operation.depthTarget.texture->depth;
							pass.depthAttachment.load = Rhi::AttachmentLoadOp::Load;
						}
						Done(command->BeginRendering(pass));
						Done(command->BindPipeline(program.pipeline));
						if (group)
						{
							Done(command->BindGroup(group));
						}
						if (viewport)
						{
							Done(command->SetViewport(*viewport));
						}
						if (scissor)
						{
							Done(command->SetScissor(*scissor));
						}
						Done(command->Draw(3));
						Done(command->EndRendering());
						Done(frames->Retain(operation.material));
						break;
					}
					default:
						throw std::runtime_error("Render command has no GPU encoding");
				}
			}
			if (cameraTarget && cameraTarget->color)
			{
				transitionTexture(cameraTarget, false, finalState);
			}
			closeCameraRegion();
			completion = Take(frames->Complete(captureFrame ? capture : nullptr));
			activeFrame = nullptr;
			for (const auto& state : textures)
			{
				*state.second.output = state.second.value;
			}
			for (const auto& state : buffers)
			{
				state.second.resource->state = state.second.value;
			}
			++statistics.physicalSubmissions;
		}
		else if (activeFrame)
		{
			Done(frames->Abort());
			completion = activeFrame->Completion();
			activeFrame = nullptr;
			if (completion)
			{
				++statistics.physicalSubmissions;
				if (cameraTarget)
				{
					cameraTarget->colorState = Rhi::ResourceState::Present;
				}
			}
		}
		for (auto& entry : pool)
		{
			if (entry.leased)
			{
				entry.completion = completion;
				entry.leased = false;
			}
		}
		committed.clear();
		pending.clear();
		temporaryTargets.clear();
		cameraTarget.reset();
		currentTarget.reset();
		currentColors.clear();
		currentDepth.reset();
		active = false;
		return Result<Rhi::FencePtr>::Success(std::move(completion));
	}
	catch (const std::exception& error)
	{
		frames->Abort();
		activeFrame = nullptr;
		for (auto& entry : pool)
		{
			entry.leased = false;
		}
		committed.clear();
		pending.clear();
		temporaryTargets.clear();
		cameraTarget.reset();
		currentTarget.reset();
		currentColors.clear();
		currentDepth.reset();
		active = false;
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<std::shared_ptr<const Shader::ShaderAsset>> RenderCommandExecutor::ResolveBuiltinShader(ResourceId id)
{
	try
	{
		impl->RequireActive();
		if (!impl->sceneRenderer)
		{
			impl->sceneRenderer = Take(SceneDrawEncoder::Create(impl->device));
		}
		return impl->sceneRenderer->ResolveBuiltinShader(id);
	}
	catch (const std::exception& error)
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure(error.what());
	}
}
} // namespace Ursa::Rendering
