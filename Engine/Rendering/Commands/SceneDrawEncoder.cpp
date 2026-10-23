#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Rendering/Commands/SceneDrawEncoder.hpp>
#include <Core/Profiler.hpp>
#include <Core/Memory/MemoryAlgorithms.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Shader/ShaderRenderState.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderSource.hpp>
#include <Shader/ShaderBuiltins.hpp>
#include <Rendering/Commands/SceneDrawRecord.hpp>
#include <Vfs/AssetRoots.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Ursa::Rendering
{
namespace
{
using namespace Shader;

template <class T> T Take(Result<T> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
	return std::move(result.value);
}

void done(Result<void> result)
{
	if (!result)
	{
		throw std::runtime_error(ToStdString(result.error));
	}
}

struct Vertex
{
	Ursa::Float32 position[3];
	Ursa::Float32 normal[3];
	Ursa::Float32 uv[2];
	Ursa::Float32 tangent[4];
	Ursa::Float32 color[4];
	Ursa::UInt32 jointIndices[4]{};
	Ursa::Float32 jointWeights[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(offsetof(Vertex, jointIndices) == 64, "Unexpected model vertex skin index offset");
static_assert(sizeof(Vertex) == 96, "Unexpected model vertex stride");

struct GpuDirectionalLight
{
	Ursa::Float32 directionIntensity[4]{};
	Ursa::Float32 color[4]{};
};

struct GpuPointLight
{
	Ursa::Float32 positionRange[4]{};
	Ursa::Float32 colorIntensity[4]{};
};

struct GpuFrameHeader
{
	Ursa::Float32 viewProjection[16]{};
	Ursa::Float32 view[16]{};
	Ursa::Float32 cameraPosition[4]{};
	Ursa::Float32 ambient[4]{};
	Ursa::Int32 counts[4]{};
	GpuDirectionalLight directional[SceneLighting::maxDirectionalLights]{};
	GpuPointLight point[SceneLighting::maxPointLights]{};
};

static_assert(sizeof(GpuFrameHeader) == 560, "Unexpected frame layout");

void WriteColor(Ursa::Float32 (&out)[4], Color color)
{
	out[0] = color.red;
	out[1] = color.green;
	out[2] = color.blue;
	out[3] = color.alpha;
}

GpuFrameHeader BuildFrameHeader(const Mat4& viewProjection, const Mat4& view, const SceneLighting& lighting,
                                Vec3 cameraPosition)
{
	GpuFrameHeader frame{};
	std::memcpy(frame.viewProjection, viewProjection.values.data(), sizeof(frame.viewProjection));
	std::memcpy(frame.view, view.values.data(), sizeof(frame.view));
	frame.cameraPosition[0] = cameraPosition.x;
	frame.cameraPosition[1] = cameraPosition.y;
	frame.cameraPosition[2] = cameraPosition.z;
	WriteColor(frame.ambient, lighting.ambientColor);
	frame.ambient[3] = lighting.ambientIntensity;
	frame.counts[0] = static_cast<Ursa::Int32>(lighting.directional.size());
	frame.counts[1] = static_cast<Ursa::Int32>(lighting.point.size());
	for (Ursa::Size index = 0; index < lighting.directional.size(); ++index)
	{
		const auto& light = lighting.directional[index];
		frame.directional[index].directionIntensity[0] = light.direction.x;
		frame.directional[index].directionIntensity[1] = light.direction.y;
		frame.directional[index].directionIntensity[2] = light.direction.z;
		frame.directional[index].directionIntensity[3] = light.intensity;
		WriteColor(frame.directional[index].color, light.color);
	}
	for (Ursa::Size index = 0; index < lighting.point.size(); ++index)
	{
		const auto& light = lighting.point[index];
		frame.point[index].positionRange[0] = light.position.x;
		frame.point[index].positionRange[1] = light.position.y;
		frame.point[index].positionRange[2] = light.position.z;
		frame.point[index].positionRange[3] = light.range;
		WriteColor(frame.point[index].colorIntensity, light.color);
		frame.point[index].colorIntensity[3] = light.intensity;
	}
	return frame;
}

struct DrawItem
{
	const RenderObject* object = nullptr;
	std::shared_ptr<MaterialInstance> material;
	Size ordinal = 0;
};

struct GpuModelMesh
{
	std::weak_ptr<Mesh> source;
	std::shared_ptr<const MeshSnapshot> snapshot;
	Rhi::BufferPtr vertexBuffer;
	Rhi::BufferPtr indexBuffer;
	Ursa::UInt32 indexCount = 0;
};

struct ResolvedMaterialCacheEntry
{
	std::weak_ptr<MaterialInstance> source;
	Ursa::UInt64 sourceVersion = 0;
	std::shared_ptr<MaterialInstance> resolved;
};

struct ShaderPassPlan
{
	std::shared_ptr<const ShaderAsset> programShader;
	const ShaderPassAsset* pass = nullptr;
	Vector<RenderStateDeclaration> renderStates;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
};

struct ShaderPipelineKey
{
	String propertyShaderId;
	String programShaderId;
	UInt32 propertySourceFormatVersion = 0;
	UInt32 programSourceFormatVersion = 0;
	String propertySourceHash;
	String programSourceHash;
	String compilerVersion;
	String passIdentity;
	String layoutIdentity;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
	String activePipeline;
	Rhi::Format colorFormat = Rhi::Format::RgbA8Unorm;
	Rhi::Format depthFormat = Rhi::Format::D32Float;
	AlphaMode alphaMode = AlphaMode::Opaque;
	Vector<String> keywords;
	Vector<String> renderStates;
	Vector<Rhi::Format> colorFormats;
	bool hasDepth = true;
	bool depthBiasOverride = false;
	Int32 depthBiasConstant = 0;
	Float32 depthBiasSlope = 0;

	bool operator<(const ShaderPipelineKey& other) const
	{
		const auto left =
		    std::tie(propertyShaderId, programShaderId, propertySourceFormatVersion, programSourceFormatVersion,
		             propertySourceHash, programSourceHash, compilerVersion, passIdentity, layoutIdentity,
		             subShaderIndex, passIndex, activePipeline, colorFormat, depthFormat, alphaMode, hasDepth,
		             depthBiasOverride, depthBiasConstant, depthBiasSlope);
		const auto right =
		    std::tie(other.propertyShaderId, other.programShaderId, other.propertySourceFormatVersion,
		             other.programSourceFormatVersion, other.propertySourceHash, other.programSourceHash,
		             other.compilerVersion, other.passIdentity, other.layoutIdentity, other.subShaderIndex,
		             other.passIndex, other.activePipeline, other.colorFormat, other.depthFormat, other.alphaMode,
		             other.hasDepth, other.depthBiasOverride, other.depthBiasConstant, other.depthBiasSlope);
		if (left < right)
		{
			return true;
		}
		if (right < left)
		{
			return false;
		}
		if (colorFormats != other.colorFormats)
		{
			return std::lexicographical_compare(colorFormats.begin(), colorFormats.end(), other.colorFormats.begin(),
			                                    other.colorFormats.end());
		}
		if (std::lexicographical_compare(keywords.begin(), keywords.end(), other.keywords.begin(),
		                                 other.keywords.end()))
		{
			return true;
		}
		if (std::lexicographical_compare(other.keywords.begin(), other.keywords.end(), keywords.begin(),
		                                 keywords.end()))
		{
			return false;
		}
		return std::lexicographical_compare(renderStates.begin(), renderStates.end(), other.renderStates.begin(),
		                                    other.renderStates.end());
	}
};

Rhi::ShaderStage ShaderStageFor(ProgramStage stage)
{
	switch (stage)
	{
		case ProgramStage::Vertex:
			return Rhi::ShaderStage::Vertex;
		case ProgramStage::Fragment:
			return Rhi::ShaderStage::Fragment;
		case ProgramStage::Compute:
			return Rhi::ShaderStage::Compute;
	}
	throw std::runtime_error("Unknown Shader stage");
}

Rhi::VertexAttribute VertexAttributeFor(const ShaderVertexInput& input)
{
	Rhi::VertexAttribute attribute;
	attribute.location = input.location;
	attribute.format = input.format;
	if (input.semantic == "POSITION")
	{
		attribute.offset = offsetof(Vertex, position);
	}
	else if (input.semantic == "NORMAL")
	{
		attribute.offset = offsetof(Vertex, normal);
	}
	else if (input.semantic == "TEXCOORD" || input.semantic == "TEXCOORD0")
	{
		attribute.offset = offsetof(Vertex, uv);
	}
	else if (input.semantic == "TANGENT")
	{
		attribute.offset = offsetof(Vertex, tangent);
	}
	else if (input.semantic == "COLOR" || input.semantic == "COLOR0")
	{
		attribute.offset = offsetof(Vertex, color);
	}
	else if (input.semantic == "BLENDINDICES" || input.semantic == "BLENDINDICES0")
	{
		attribute.offset = offsetof(Vertex, jointIndices);
	}
	else if (input.semantic == "BLENDWEIGHT" || input.semantic == "BLENDWEIGHT0")
	{
		attribute.offset = offsetof(Vertex, jointWeights);
	}
	else
	{
		throw std::runtime_error("Unsupported Ursa cube vertex semantic " + ToStdString(input.semantic));
	}
	return attribute;
}

const ShaderVariantAsset* SelectVariant(const ShaderPassAsset& pass, const Vector<String>& requestedKeywords)
{
	for (const ShaderVariantAsset& variant : pass.variants)
	{
		if (variant.keywords == requestedKeywords)
		{
			return &variant;
		}
	}
	return nullptr;
}

MaterialTexturePtr ShaderDefaultTexture(const String& name)
{
	static Map<String, MaterialTexturePtr> defaults;
	auto found = defaults.find(name);
	if (found != defaults.end())
	{
		return found->second;
	}
	TextureColorSpace colorSpace = TextureColorSpace::Srgb;
	Vector<Ursa::UInt8> pixel{255, 255, 255, 255};
	if (name == "black")
	{
		pixel = {0, 0, 0, 255};
	}
	else if (name == "gray")
	{
		pixel = {128, 128, 128, 255};
	}
	else if (name == "red")
	{
		pixel = {255, 0, 0, 255};
	}
	else if (name == "bump")
	{
		colorSpace = TextureColorSpace::Linear;
		pixel = {128, 128, 255, 255};
	}
	else if (name != "white")
	{
		return {};
	}
	auto texture = MaterialTexture::Create(1, 1, colorSpace, std::move(pixel));
	if (!texture)
	{
		throw std::runtime_error(ToStdString(texture.error));
	}
	defaults.emplace(name, texture.value);
	return texture.value;
}
} // namespace

struct SceneDrawEncoder::Impl
{
	const Rendering::SceneDrawEncoding* activeEncoding = nullptr;
	const Vector<Rendering::SceneDrawRecord>* activeDraws = nullptr;
	Size currentDrawOrdinal = 0;

	String MaterialKey(const std::shared_ptr<MaterialInstance>& material) const
	{
		if (activeDraws && currentDrawOrdinal < activeDraws->size() &&
		    (*activeDraws)[currentDrawOrdinal].materialIdentity)
		{
			const auto& draw = (*activeDraws)[currentDrawOrdinal];
			return draw.materialIdentity.ToString() + ":" + ToUrsaString(std::to_string(draw.materialVersion));
		}
		return material->LocalIdentity().ToString() + ":" + ToUrsaString(std::to_string(material->Version()));
	}

	Result<Vector<ShaderPassPlan>> PlansFor(const std::shared_ptr<const ShaderAsset>& shader, Size ordinal)
	{
		const auto& draw = (*activeDraws)[ordinal];
		if (draw.resolvedPass)
		{
			const auto& selected = *draw.resolvedPass;
			const auto& pass = selected.programShader->subShaders[selected.subShaderIndex].passes[selected.passIndex];
			return Result<Vector<ShaderPassPlan>>::Success(
			    {{selected.programShader, &pass, selected.renderStates, selected.subShaderIndex, selected.passIndex}});
		}
		if (draw.selectedPass)
		{
			if (draw.subShaderIndex >= shader->subShaders.size() ||
			    draw.passIndex >= shader->subShaders[draw.subShaderIndex].passes.size())
			{
				return Result<Vector<ShaderPassPlan>>::Failure("Selected scene draw Shader Pass is out of range");
			}
			const auto& subShader = shader->subShaders[draw.subShaderIndex];
			const auto& pass = subShader.passes[draw.passIndex];
			if (pass.computeKernel)
			{
				return Result<Vector<ShaderPassPlan>>::Failure(
				    "A compute kernel cannot be used as a scene drawing Pass");
			}
			Vector<RenderStateDeclaration> states = subShader.renderStates;
			states.insert(states.end(), pass.renderStates.begin(), pass.renderStates.end());
			return Result<Vector<ShaderPassPlan>>::Success(
			    {{shader, &pass, std::move(states), draw.subShaderIndex, draw.passIndex}});
		}
		return Result<Vector<ShaderPassPlan>>::Failure("Scene draw requires an explicitly resolved Shader Pass");
	}

	struct ShaderPipelineCache
	{
		ShaderBindingLayout reflection;
		Rhi::LayoutPtr layout;
		Rhi::PipelinePtr pipeline;
	};

	Rhi::SamplerPtr SamplerFor(const MaterialSampler& samplerSettings)
	{
		const String key =
		    String(samplerSettings.linear ? "linear:" : "nearest:") + (samplerSettings.repeat ? "repeat" : "clamp");
		if (const auto found = samplerCache.find(key); found != samplerCache.end())
		{
			return found->second;
		}
		auto sampler = Take(device->CreateSampler({samplerSettings.linear, samplerSettings.repeat}));
		samplerCache.emplace(key, sampler);
		return sampler;
	}

	explicit Impl(Rhi::DevicePtr selectedDevice) : device(std::move(selectedDevice))
	{
	}

	void Initialize()
	{
		auto engineFiles = OpenEngineFileSystem();
		if (!engineFiles)
		{
			throw std::runtime_error(ToStdString(engineFiles.error));
		}
		builtinUnlit = std::make_shared<ShaderAsset>(Take(LoadShaderAssetResource(
		    engineFiles.value,
		    Take(VirtualPath::Parse(Shader::BuiltinShaderAssetPath(Shader::BuiltinUnlitShaderId()))))));
		builtinPbr = std::make_shared<ShaderAsset>(Take(LoadShaderAssetResource(
		    engineFiles.value,
		    Take(VirtualPath::Parse(Shader::BuiltinShaderAssetPath(Shader::BuiltinPbrShaderId()))))));
		builtinFinalPost = std::make_shared<ShaderAsset>(Take(LoadShaderAssetResource(
		    engineFiles.value,
		    Take(VirtualPath::Parse(Shader::BuiltinShaderAssetPath(Shader::BuiltinFinalPostShaderId()))))));
	}

	GpuModelMesh& MeshFor(const MeshPtr& mesh, const std::shared_ptr<const MeshSnapshot>& selected)
	{
		if (!mesh || (!mesh->IsValid() && !selected))
		{
			throw std::runtime_error("Renderer received a destroyed Mesh");
		}
		const auto snapshot = selected ? selected : mesh->Snapshot();
		if (!snapshot || snapshot->data.positions.empty() || snapshot->data.indices.empty())
		{
			throw std::runtime_error("Renderer received an empty Mesh draw");
		}
		const String key =
		    ToUrsaString(std::to_string(mesh->Identity())) + ":" + ToUrsaString(std::to_string(snapshot->version));
		auto cached = modelMeshes.find(key);
		if (cached != modelMeshes.end())
		{
			return cached->second;
		}
		done(Mesh::Validate(snapshot->data));
		const auto& data = snapshot->data;
		GpuModelMesh gpu;
		gpu.source = mesh;
		gpu.snapshot = snapshot;
		Vector<Vertex> vertices(data.positions.size());
		for (Size i = 0; i < vertices.size(); ++i)
		{
			const Vec3 position = data.positions[i];
			const Vec3 normal = data.normals.empty() ? Vec3{0, 1, 0} : data.normals[i];
			const Vec2 uv = data.uv0.empty() ? Vec2{} : data.uv0[i];
			const Vec4 tangent = data.tangents.empty() ? Vec4{1, 0, 0, 1} : data.tangents[i];
			const Color color = data.colors.empty() ? Color{} : data.colors[i];
			vertices[i] = {{position.x, position.y, position.z},
			               {normal.x, normal.y, normal.z},
			               {uv.x, uv.y},
			               {tangent.x, tangent.y, tangent.z, tangent.w},
			               {color.red, color.green, color.blue, color.alpha}};
			if (!data.skinInfluences.empty())
			{
				for (Size influence = 0; influence < 4; ++influence)
				{
					vertices[i].jointIndices[influence] = data.skinInfluences[i].jointIndices[influence];
					vertices[i].jointWeights[influence] = data.skinInfluences[i].weights[influence];
				}
			}
		}
		gpu.vertexBuffer = Take(
		    device->CreateBuffer({vertices.size() * sizeof(Vertex), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload}));
		gpu.indexBuffer = Take(
		    device->CreateBuffer({data.indices.size() * sizeof(UInt32), Rhi::BufferUsage::Index, Rhi::Memory::Upload}));
		done(gpu.vertexBuffer->Write(0, vertices.data(), vertices.size() * sizeof(Vertex)));
		done(gpu.indexBuffer->Write(0, data.indices.data(), data.indices.size() * sizeof(UInt32)));
		gpu.indexCount = UInt32(data.indices.size());
		Profiling::Plot("Rendering.MeshUploads", Float64(++meshUploads));
		return modelMeshes.emplace(key, std::move(gpu)).first->second;
	}

	void MaintainCaches()
	{
		URSA_PROFILE_SCOPE("SceneDrawEncoder.CacheMaintenance");
		for (auto it = modelMeshes.begin(); it != modelMeshes.end();)
		{
			auto owner = it->second.source.lock();
			if (!owner || !owner->IsValid() || owner->Version() != it->second.snapshot->version)
			{
				it = modelMeshes.erase(it);
			}
			else
			{
				++it;
			}
		}
		for (auto it = resolvedMaterialCache.begin(); it != resolvedMaterialCache.end();)
		{
			auto source = it->second.source.lock();
			if (!source || !source->IsValid() || source->Version() != it->second.sourceVersion ||
			    !it->second.resolved || !it->second.resolved->IsValid())
			{
				it = resolvedMaterialCache.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	ShaderPipelineCache& ShaderPipelineFor(const std::shared_ptr<const ShaderAsset>& propertyShader,
	                                       const ShaderPassPlan& plan, const ShaderVariantAsset& variant,
	                                       Rhi::Format colorFormat, Rhi::Format depthFormat,
	                                       const MaterialInstance& material)
	{
		ShaderPipelineKey key;
		key.propertyShaderId = propertyShader->id.ToString();
		key.programShaderId = plan.programShader->id.ToString();
		key.propertySourceFormatVersion = propertyShader->sourceFormatVersion;
		key.programSourceFormatVersion = plan.programShader->sourceFormatVersion;
		key.propertySourceHash = propertyShader->sourceHash;
		key.programSourceHash = plan.programShader->sourceHash;
		key.compilerVersion = plan.programShader->compilerVersion;
		key.passIdentity = plan.pass->identity;
		key.layoutIdentity = variant.layoutIdentity;
		key.subShaderIndex = plan.subShaderIndex;
		key.passIndex = plan.passIndex;
		key.activePipeline = "UrsaUniversal";
		if (activeDraws)
		{
			key.activePipeline = (*activeDraws)[currentDrawOrdinal].activePipeline;
		}
		if (activeEncoding)
		{
			key.hasDepth = bool(activeEncoding->target.depthAttachment.texture || activeEncoding->target.depth);
			for (const auto& attachment : activeEncoding->target.colors)
			{
				key.colorFormats.push_back(attachment.texture->desc.format);
			}
			if (activeEncoding->globals)
			{
				const auto overrideValue = activeEncoding->globals->find("_UrsaDepthBiasOverride");
				if (overrideValue != activeEncoding->globals->end())
				{
					if (overrideValue->second.kind != 6 || overrideValue->second.bytes.size() != sizeof(Int32))
					{
						throw std::runtime_error("Depth bias override flag has an invalid type or size");
					}
					Int32 enabled = 0;
					std::memcpy(&enabled, overrideValue->second.bytes.data(), sizeof(enabled));
					if (enabled != 0)
					{
						const auto constantValue = activeEncoding->globals->find("_UrsaDepthBiasConstant");
						const auto slopeValue = activeEncoding->globals->find("_UrsaDepthBiasSlope");
						if (constantValue == activeEncoding->globals->end() || constantValue->second.kind != 6 ||
						    constantValue->second.bytes.size() != sizeof(Int32) ||
						    slopeValue == activeEncoding->globals->end() || slopeValue->second.kind != 1 ||
						    slopeValue->second.bytes.size() != sizeof(Float32))
						{
							throw std::runtime_error("Depth bias override requires constant and slope values");
						}
						key.depthBiasOverride = true;
						std::memcpy(&key.depthBiasConstant, constantValue->second.bytes.data(), sizeof(Int32));
						std::memcpy(&key.depthBiasSlope, slopeValue->second.bytes.data(), sizeof(Float32));
					}
				}
			}
		}
		const bool depthOnly = activeEncoding && key.colorFormats.empty() && !activeEncoding->target.color &&
		                       (activeEncoding->target.depthAttachment.texture || activeEncoding->target.depth);
		key.colorFormat = colorFormat;
		key.depthFormat = depthFormat;
		key.alphaMode = material.Parameters().alphaMode;
		key.keywords = variant.keywords;
		for (const RenderStateDeclaration& state : plan.renderStates)
		{
			key.renderStates.push_back(state.name + "=" + state.value);
		}
		if (auto found = shaderPipelines.find(key); found != shaderPipelines.end())
		{
			return found->second;
		}
		auto builtReflection = BuildShaderBindingLayout(*propertyShader, variant);
		Profiling::Plot("Rendering.ReflectionBuilds", Float64(++reflectionBuilds));
		if (!builtReflection)
		{
			throw std::runtime_error(ToStdString(builtReflection.error));
		}
		Rhi::GraphicsPipelineDesc description;
		description.colorFormat = colorFormat;
		description.depthFormat = depthFormat;
		description.depthTest = true;
		description.depthWrite = true;
		description.depthCompare = Rhi::Compare::LessEqual;
		description.cullMode = Rhi::CullMode::Back;
		if (activeEncoding)
		{
			description.colorFormats = key.colorFormats;
			description.depthOnly = key.colorFormats.empty() && !activeEncoding->target.color;
			description.depthTest = key.hasDepth;
			description.depthWrite = key.hasDepth;
		}
		if (!builtReflection.value.vertexInputs.empty())
		{
			description.vertexStride = sizeof(Vertex);
			for (const ShaderVertexInput& input : builtReflection.value.vertexInputs)
			{
				description.attributes.push_back(VertexAttributeFor(input));
			}
		}
		ApplyGraphicsRenderStates(description, plan.renderStates);
		if (key.depthBiasOverride)
		{
			description.depthBiasConstant = key.depthBiasConstant;
			description.depthBiasSlope = key.depthBiasSlope;
		}
		if (activeEncoding && (description.depthTest || description.stencil.enabled) && !key.hasDepth)
		{
			throw std::runtime_error("Scene drawing Pass requires a missing depth/stencil attachment");
		}
		if (material.Parameters().alphaMode == AlphaMode::Blend)
		{
			description.blend = true;
			description.blendState.enabled = true;
			description.blendState.sourceColor = Rhi::BlendFactor::SourceAlpha;
			description.blendState.destinationColor = Rhi::BlendFactor::OneMinusSourceAlpha;
			description.depthWrite = false;
		}
		ShaderPipelineCache cache;
		cache.reflection = std::move(builtReflection.value);
		cache.layout = Take(CreateShaderBindGroupLayout(cache.reflection, *device));
		Rhi::ShaderPtr vertex;
		Rhi::ShaderPtr fragment;
		for (const ShaderStageAsset& stage : variant.stages)
		{
			if (stage.target != ShaderTarget::VulkanSpirv)
			{
				throw std::runtime_error("Selected Shader pass has no Vulkan output");
			}
			if (stage.stage == ProgramStage::Compute)
			{
				throw std::runtime_error("Compute stage cannot execute in a graphics Pass");
			}
			auto created = Take(device->CreateShader(
			    {ShaderStageFor(stage.stage), Rhi::ShaderFormat::Spirv, stage.spirv, stage.entryPoint}));
			if (stage.stage == ProgramStage::Vertex)
			{
				if (vertex)
				{
					throw std::runtime_error("Shader Pass declares more than one vertex stage");
				}
				vertex = std::move(created);
			}
			else
			{
				if (fragment)
				{
					throw std::runtime_error("Shader Pass declares more than one fragment stage");
				}
				fragment = std::move(created);
			}
		}
		if (!vertex || (!fragment && (!depthOnly || material.Parameters().alphaMode == AlphaMode::Mask)))
		{
			throw std::runtime_error(
			    depthOnly ? "Depth-only Shader Pass requires a vertex stage and an alpha-test fragment when masked"
			              : "Forward Shader Pass requires vertex and fragment stages");
		}
		description.vertex = vertex;
		description.fragment = fragment;
		description.layout = cache.layout;
		cache.pipeline = Take(device->CreateGraphicsPipeline(description));
		Profiling::Plot("Rendering.ScenePipelineCreates", Float64(++pipelineCreates));
		auto inserted = shaderPipelines.emplace(std::move(key), std::move(cache));
		return inserted.first->second;
	}

	Rhi::BindGroupPtr ShaderGroupFor(const std::shared_ptr<const ShaderAsset>& propertyShader,
	                                 const ShaderPipelineCache& cache, const ShaderPassPlan& plan,
	                                 const ShaderVariantAsset& variant,
	                                 const std::shared_ptr<MaterialInstance>& material, const Mat4& viewProjection,
	                                 const Mat4& view, const RenderObject& item, const GpuFrameHeader& frame,
	                                 const Rhi::CommandListPtr& command)
	{
		const Mat4& model = item.world;
		bool cacheableMaterialGroup = true;
		if (activeEncoding && activeEncoding->globals && !activeEncoding->globals->empty())
		{
			for (const Rhi::LayoutBinding& descriptor : cache.reflection.descriptors)
			{
				if (descriptor.type == Rhi::BindingType::UniformBuffer)
				{
					auto uniform = std::find_if(
					    cache.reflection.uniformBuffers.begin(), cache.reflection.uniformBuffers.end(),
					    [&](const ShaderUniformBufferLayout& value) { return value.binding == descriptor.binding; });
					if (uniform != cache.reflection.uniformBuffers.end() &&
					    std::any_of(uniform->members.begin(), uniform->members.end(),
					                [&](const ShaderUniformMember& member)
					                {
						                const auto global = activeEncoding->globals->find(member.name);
						                return global != activeEncoding->globals->end() &&
						                       !global->second.bytes.empty();
					                }))
					{
						cacheableMaterialGroup = false;
						break;
					}
				}
				else if (descriptor.type == Rhi::BindingType::SampledTexture ||
				         descriptor.type == Rhi::BindingType::CombinedImageSampler)
				{
					const auto name = cache.reflection.resourceNames.find(descriptor.binding);
					if (name != cache.reflection.resourceNames.end())
					{
						const auto global = activeEncoding->globals->find(name->second);
						if (global != activeEncoding->globals->end() && global->second.texture.texture)
						{
							cacheableMaterialGroup = false;
							break;
						}
					}
				}
			}
		}
		for (const Rhi::LayoutBinding& descriptor : cache.reflection.descriptors)
		{
			if (descriptor.type == Rhi::BindingType::StorageBuffer)
			{
				cacheableMaterialGroup = false;
				break;
			}
		}
		for (const ShaderUniformBufferLayout& buffer : cache.reflection.uniformBuffers)
		{
			for (const ShaderUniformMember& member : buffer.members)
			{
				if (member.name.rfind("_Ursa", 0) == 0)
				{
					cacheableMaterialGroup = false;
					break;
				}
			}
		}
		String materialGroupKey;
		if (cacheableMaterialGroup)
		{
			materialGroupKey = "material-group-v1:" + propertyShader->id.ToString() + ":" + propertyShader->sourceHash +
			                   ":" + plan.programShader->sourceHash + ":" + variant.layoutIdentity + ":" +
			                   MaterialKey(material);
			for (const String& keyword : variant.keywords)
			{
				materialGroupKey += ":kw=" + keyword;
			}
			if (const auto found = materialGroups.find(materialGroupKey); found != materialGroups.end())
			{
				Profiling::Plot("Rendering.MaterialGroupReuses", Float64(++materialGroupReuses));
				return found->second;
			}
		}
		Vector<Rhi::BindingResource> resources;
		Vector<Rhi::BufferPtr> buffers;
		Vector<Rhi::SamplerPtr> samplers;
		Map<UInt32, MaterialTexturePtr> texturesByBinding;
		for (const auto& property : cache.reflection.properties)
		{
			if (activeEncoding && activeEncoding->globals)
			{
				const auto global = activeEncoding->globals->find(property.first);
				if (global != activeEncoding->globals->end() && global->second.texture.texture)
				{
					continue;
				}
			}
			if (property.second.bindingType != Rhi::BindingType::SampledTexture &&
			    property.second.bindingType != Rhi::BindingType::CombinedImageSampler)
			{
				continue;
			}
			auto value = material->PropertyValue(property.first, *propertyShader);
			if (!value)
			{
				throw std::runtime_error(ToStdString(value.error));
			}
			MaterialTexturePtr texture = value.value.texture;
			if (!texture && !value.value.textureDefault.empty())
			{
				texture = ShaderDefaultTexture(value.value.textureDefault);
			}
			if (!texture)
			{
				throw std::runtime_error("Shader texture property has no resolved image: " +
				                         ToStdString(property.first));
			}
			texturesByBinding[property.second.binding] = std::move(texture);
		}
		for (const Rhi::LayoutBinding& descriptor : cache.reflection.descriptors)
		{
			if (descriptor.type == Rhi::BindingType::StorageBuffer)
			{
				if (activeEncoding && activeEncoding->globals)
				{
					const String& name = cache.reflection.resourceNames.at(descriptor.binding);
					const auto global = activeEncoding->globals->find(name);
					if (global != activeEncoding->globals->end() && global->second.buffer)
					{
						done(
						    activeEncoding->bufferBarrier(global->second.buffer, Rhi::ResourceState::StorageReadWrite));
						resources.push_back({descriptor.binding, global->second.buffer->buffer, {}, {}});
						continue;
					}
					if (name.rfind("_Ursa", 0) != 0)
					{
						throw std::runtime_error("Scene Shader storage buffer is missing: " + ToStdString(name));
					}
				}
				Vector<Mat4> identityPalette;
				const Vector<Mat4>* palette = &item.jointPalette;
				if (palette->empty())
				{
					identityPalette.push_back(Mat4::Identity());
					palette = &identityPalette;
				}
				const Ursa::Size byteSize = palette->size() * sizeof(Mat4);
				const Ursa::UInt32 maxStorageRange = device->Capabilities().maxStorageBufferRange;
				if (maxStorageRange == 0 || byteSize > maxStorageRange)
				{
					throw std::runtime_error("Model bone palette exceeds the RHI storage buffer limit");
				}
				const auto upload = Take(activeEncoding->frames->Upload(
				    palette->data(), byteSize, Size(device->Capabilities().minStorageBufferOffsetAlignment)));
				resources.push_back({descriptor.binding, upload.buffer, {}, {}, upload.offset, upload.size});
				continue;
			}
			if (descriptor.type == Rhi::BindingType::UniformBuffer)
			{
				auto uniform = std::find_if(
				    cache.reflection.uniformBuffers.begin(), cache.reflection.uniformBuffers.end(),
				    [&](const ShaderUniformBufferLayout& value) { return value.binding == descriptor.binding; });
				if (uniform == cache.reflection.uniformBuffers.end())
				{
					throw std::runtime_error("Reflected uniform descriptor has no member layout");
				}
				Vector<UInt8> bytes(uniform->byteSize, 0);
				String materialUniformKey = "material-uniform-v1:" + propertyShader->id.ToString() + ":" +
				                            propertyShader->sourceHash + ":" + variant.layoutIdentity + ":" +
				                            ToUrsaString(std::to_string(descriptor.binding)) + ":" +
				                            MaterialKey(material);
				if (const auto materialBacking = materialUniformBacking.find(materialUniformKey);
				    materialBacking != materialUniformBacking.end())
				{
					bytes = materialBacking->second;
				}
				else
				{
					auto packed =
					    PackMaterialUniformBuffer(*propertyShader, cache.reflection, *material, descriptor.binding);
					if (!packed)
					{
						throw std::runtime_error(ToStdString(packed.error));
					}
					bytes = packed.value;
					if (materialUniformBacking.size() >= 512)
					{
						materialUniformBacking.erase(materialUniformBacking.begin());
					}
					materialUniformBacking.emplace(std::move(materialUniformKey), std::move(packed.value));
				}
				bool perCamera = false;
				bool perFrame = false;
				Mat4 inverseModel = model.InverseAffine();
				const Ursa::Float32 objectData[4] = {item.castShadows ? 1.0f : 0.0f, item.receiveShadows ? 1.0f : 0.0f,
				                                     0.0f, 0.0f};
				Mat4 normalMatrix;
				for (int row = 0; row < 4; ++row)
				{
					for (int column = 0; column < 4; ++column)
					{
						normalMatrix.At(row, column) = inverseModel.At(column, row);
					}
				}
				for (const ShaderUniformMember& member : uniform->members)
				{
					const Ursa::Float32* source = nullptr;
					if (member.name == "_UrsaViewProjection")
					{
						source = viewProjection.values.data();
						perCamera = true;
					}
					else if (member.name == "_UrsaView")
					{
						source = view.values.data();
						perCamera = true;
					}
					else if (member.name == "_UrsaModel")
					{
						source = model.values.data();
					}
					else if (member.name == "_UrsaNormal")
					{
						source = normalMatrix.values.data();
					}
					else if (member.name == "_UrsaObjectData")
					{
						source = objectData;
					}
					else if (member.name == "_UrsaCameraPosition")
					{
						source = frame.cameraPosition;
						perFrame = true;
					}
					else if (member.name == "_UrsaAmbient")
					{
						source = frame.ambient;
						perFrame = true;
					}
					else if (member.name == "_UrsaCounts")
					{
						source = reinterpret_cast<const Ursa::Float32*>(frame.counts);
						perFrame = true;
					}
					else if (member.name == "_UrsaDirectional")
					{
						source = reinterpret_cast<const Ursa::Float32*>(frame.directional);
						perFrame = true;
					}
					else if (member.name == "_UrsaPoint")
					{
						source = reinterpret_cast<const Ursa::Float32*>(frame.point);
						perFrame = true;
					}
					if (source)
					{
						const Ursa::Size copySize = (perFrame || perCamera) && member.byteSize != 0 ? member.byteSize
						                            : member.name == "_UrsaObjectData"              ? sizeof(objectData)
						                            : member.name == "_UrsaCounts"      ? sizeof(frame.counts)
						                            : member.name == "_UrsaDirectional" ? sizeof(frame.directional)
						                            : member.name == "_UrsaPoint"       ? sizeof(frame.point)
						                                                                : sizeof(Mat4);
						if (member.byteOffset > bytes.size() || copySize > bytes.size() - member.byteOffset)
						{
							throw std::runtime_error("Renderer uniform member exceeds its reflected block");
						}
						if (member.type == "mat4" || member.type == "float4x4")
						{
							done(Shader::WriteShaderUniformValue(bytes, member, source, copySize));
						}
						else
						{
							std::memcpy(bytes.data() + member.byteOffset, source, copySize);
						}
					}
				}
				if (activeEncoding && activeEncoding->globals)
				{
					for (const auto& member : uniform->members)
					{
						const auto global = activeEncoding->globals->find(member.name);
						if (global == activeEncoding->globals->end() || global->second.bytes.empty())
						{
							continue;
						}
						if (global->second.bytes.size() != member.byteSize || member.byteOffset > bytes.size() ||
						    member.byteSize > bytes.size() - member.byteOffset)
						{
							throw std::runtime_error("Scene Shader global uniform size does not match reflection: " +
							                         ToStdString(member.name));
						}
						done(Shader::WriteShaderUniformValue(bytes, member, global->second.bytes.data(),
						                                     member.byteSize));
					}
				}
				if (activeEncoding && activeEncoding->frames && !cacheableMaterialGroup)
				{
					const auto upload = Take(activeEncoding->frames->Upload(
					    bytes.data(), bytes.size(), Size(device->Capabilities().minUniformBufferOffsetAlignment)));
					resources.push_back({descriptor.binding, upload.buffer, {}, {}, upload.offset, upload.size});
					continue;
				}
				String uniformKey =
				    variant.layoutIdentity + ":" + ToUrsaString(std::to_string(descriptor.binding)) + ":";
				uniformKey.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				auto cachedUniform = uniformUploadCache.find(uniformKey);
				Rhi::BufferPtr buffer;
				if (cachedUniform != uniformUploadCache.end())
				{
					buffer = cachedUniform->second;
				}
				else
				{
					buffer = Take(device->CreateBuffer({bytes.size(), Rhi::BufferUsage::Uniform, Rhi::Memory::Upload}));
					done(buffer->Write(0, bytes.data(), bytes.size()));
					done(command->Barrier(buffer, Rhi::ResourceState::Undefined, Rhi::ResourceState::UniformRead));
					if (uniformUploadCache.size() >= 512)
					{
						uniformUploadCache.erase(uniformUploadCache.begin());
					}
					uniformUploadCache.emplace(std::move(uniformKey), buffer);
				}
				resources.push_back({descriptor.binding, buffer, {}, {}});
				buffers.push_back(std::move(buffer));
			}
			else if (descriptor.type == Rhi::BindingType::SampledTexture ||
			         descriptor.type == Rhi::BindingType::CombinedImageSampler)
			{
				if (activeEncoding && activeEncoding->globals)
				{
					const String& name = cache.reflection.resourceNames.at(descriptor.binding);
					const auto global = activeEncoding->globals->find(name);
					if (global != activeEncoding->globals->end() && global->second.texture.texture)
					{
						const auto resource = global->second.texture.texture;
						const auto texture = resource->color ? resource->color : resource->depth;
						for (const auto& attachment : activeEncoding->target.colors)
						{
							if (texture == attachment.texture)
							{
								throw std::runtime_error("Scene Shader cannot sample a writable attachment");
							}
						}
						if (texture == activeEncoding->target.depth ||
						    texture == activeEncoding->target.depthAttachment.texture)
						{
							throw std::runtime_error("Scene Shader cannot sample a writable depth attachment");
						}
						done(activeEncoding->textureBarrier(resource, Rhi::ResourceState::ShaderRead));
						resources.push_back({descriptor.binding,
						                     {},
						                     texture,
						                     descriptor.type == Rhi::BindingType::CombinedImageSampler
						                         ? SamplerFor(material->Parameters().sampler)
						                         : Rhi::SamplerPtr{}});
						continue;
					}
				}
				auto texture = texturesByBinding.find(descriptor.binding);
				if (texture == texturesByBinding.end())
				{
					throw std::runtime_error("Shader sampled-image descriptor has no matching material property");
				}
				auto gpuTexture = TextureFor(texture->second);
				Rhi::SamplerPtr sampler;
				if (descriptor.type == Rhi::BindingType::CombinedImageSampler)
				{
					const MaterialSampler samplerSettings = material->SamplerOverridden()
					                                            ? material->Parameters().sampler
					                                            : texture->second->DefaultSampler();
					sampler = SamplerFor(samplerSettings);
					samplers.push_back(sampler);
				}
				resources.push_back({descriptor.binding, {}, gpuTexture, sampler});
			}
			else if (descriptor.type == Rhi::BindingType::Sampler)
			{
				MaterialSampler samplerSettings = material->Parameters().sampler;
				if (!material->SamplerOverridden() && !texturesByBinding.empty())
				{
					samplerSettings = texturesByBinding.begin()->second->DefaultSampler();
				}
				auto sampler = SamplerFor(samplerSettings);
				resources.push_back({descriptor.binding, {}, {}, sampler});
				samplers.push_back(std::move(sampler));
			}
			else
			{
				throw std::runtime_error("Selected Shader Pass uses an unsupported storage resource");
			}
		}
		if (resources.empty())
		{
			return {};
		}
		auto group = activeEncoding && !cacheableMaterialGroup
		                 ? Take(command->CreateTransientBindGroup({cache.layout, std::move(resources)}))
		                 : Take(device->CreateBindGroup({cache.layout, std::move(resources)}));
		if (cacheableMaterialGroup)
		{
			if (materialGroups.size() >= 256)
			{
				materialGroups.erase(materialGroups.begin());
			}
			materialGroups.emplace(std::move(materialGroupKey), group);
			Profiling::Plot("Rendering.MaterialGroupCreates", Float64(++materialGroupCreates));
		}
		return group;
	}

	std::shared_ptr<MaterialInstance> ResolveMaterial(const std::shared_ptr<MaterialInstance>& source, Color color)
	{
		if (!source)
		{
			auto material = MakeShared<MaterialInstance>(Memory::ThreadTemporaryAllocator(),
			                                             Memory::MemoryTag::Renderer, MaterialType::Unlit);
			done(material->SetShaderAsset(builtinUnlit));
			const Color linearColor{std::pow(std::max(color.red, 0.0f), 2.2f),
			                        std::pow(std::max(color.green, 0.0f), 2.2f),
			                        std::pow(std::max(color.blue, 0.0f), 2.2f), color.alpha};
			done(material->SetPropertyValue("_BaseColor", *builtinUnlit,
			                                MaterialPropertyValue::ColorValue(linearColor)));
			return material;
		}
		const auto sourceShader = source->Shader();
		bool compiledBuiltin = false;
		if (sourceShader)
		{
			for (const auto& subShader : sourceShader->subShaders)
			{
				for (const auto& pass : subShader.passes)
				{
					for (const auto& variant : pass.variants)
					{
						compiledBuiltin = compiledBuiltin || !variant.stages.empty();
					}
				}
			}
		}
		if (sourceShader &&
		    (compiledBuiltin || (source->ShaderId() != builtinUnlit->id && source->ShaderId() != builtinPbr->id)))
		{
			return source;
		}
		const Ursa::UInt64 sourceVersion = source->Version();
		if (const auto found = resolvedMaterialCache.find(source.get()); found != resolvedMaterialCache.end())
		{
			auto owner = found->second.source.lock();
			if (owner == source && found->second.sourceVersion == sourceVersion && found->second.resolved &&
			    found->second.resolved->IsValid())
			{
				return found->second.resolved;
			}
		}
		const auto cacheResolved = [&](std::shared_ptr<MaterialInstance> resolved)
		{
			if (resolvedMaterialCache.size() >= 256)
			{
				resolvedMaterialCache.erase(resolvedMaterialCache.begin());
			}
			resolvedMaterialCache[source.get()] = {source, sourceVersion, resolved};
			return resolved;
		};
		auto material = source->Clone();
		if (material->IsShaderBacked())
		{
			std::shared_ptr<const ShaderAsset> asset;
			if (material->ShaderId() == builtinUnlit->id)
			{
				asset = builtinUnlit;
			}
			else if (material->ShaderId() == builtinPbr->id)
			{
				asset = builtinPbr;
			}
			else
			{
				throw std::runtime_error("Material references an unresolved Shader asset " +
				                         ToStdString(material->ShaderId().ToString()));
			}
			done(material->SetShaderAsset(std::move(asset)));
			return cacheResolved(std::move(material));
		}
		const auto& shader = material->Type() == MaterialType::PbrMetallicRoughness ? builtinPbr : builtinUnlit;
		for (Ursa::UInt32 slotIndex = 0; slotIndex < 5; ++slotIndex)
		{
			const auto slot = static_cast<MaterialTextureSlot>(slotIndex);
			if (material->Texture(slot) && !material->TextureResourceId(slot))
			{
				done(material->SetTextureResourceId(slot, ResourceId::Generate()));
			}
		}
		done(material->MigrateLegacyToShader(shader->id));
		done(material->SetShaderAsset(shader));
		return cacheResolved(std::move(material));
	}

	Rhi::TexturePtr TextureFor(const MaterialTexturePtr& source)
	{
		return Take(activeEncoding->uploadTexture(source));
	}

	void Record(const Rhi::CommandListPtr& command, const Vector<SceneDrawRecord>& draws, const CameraData& camera,
	            const SceneLighting& lighting)
	{
		URSA_PROFILE_SCOPE("SceneDrawEncoder.Record");
		Memory::TemporaryScope recordingMemory;
		if (!command || lighting.directional.size() > SceneLighting::maxDirectionalLights ||
		    lighting.point.size() > SceneLighting::maxPointLights)
		{
			throw std::runtime_error("Scene drawing requires a command list and a valid lighting budget");
		}
		TemporaryVector<DrawItem> items{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer)};
		items.reserve(draws.size());
		for (Size index = 0; index < draws.size(); ++index)
		{
			const auto& object = draws[index].object;
			items.push_back({&object, ResolveMaterial(object.material, object.parameters.baseColor), index});
		}
		const auto& viewProjection = camera.viewProjection;
		const auto color = activeEncoding->target.colors.empty() ? activeEncoding->target.color
		                                                         : activeEncoding->target.colors.front().texture;
		const GpuFrameHeader frame = BuildFrameHeader(viewProjection, camera.view, lighting, camera.position);

		struct RecordedPass
		{
			Ursa::Size itemIndex = 0;
			Rhi::PipelinePtr pipeline;
			Rhi::BindGroupPtr group;
		};

		TemporaryVector<RecordedPass> recordedPasses{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer)};
		{
			URSA_PROFILE_SCOPE("SceneDrawEncoder.Record.BuildPasses");
			for (Ursa::Size itemIndex = 0; itemIndex < items.size(); ++itemIndex)
			{
				const auto& item = items[itemIndex];
				if (item.object->mesh)
				{
					MeshFor(item.object->mesh, item.object->meshSnapshot);
				}
				const auto shader = item.material->Shader();
				if (!item.material->IsShaderBacked() || !shader)
				{
					throw std::runtime_error("Renderer material has no Shader asset");
				}
				auto plans = PlansFor(shader, item.ordinal);
				if (!plans)
				{
					throw std::runtime_error(ToStdString(plans.error));
				}
				for (const ShaderPassPlan& plan : plans.value)
				{
					currentDrawOrdinal = item.ordinal;
					const ShaderVariantAsset* variant = SelectVariant(*plan.pass, item.material->Keywords());
					if (!variant)
					{
						throw std::runtime_error("No compiled keyword variant for Shader " + ToStdString(shader->name) +
						                         " Pass " + ToStdString(plan.pass->name));
					}
					const auto externalDepth = activeEncoding ? (activeEncoding->target.depthAttachment.texture
					                                                 ? activeEncoding->target.depthAttachment.texture
					                                                 : activeEncoding->target.depth)
					                                          : Rhi::TexturePtr{};
					ShaderPipelineCache& cache = ShaderPipelineFor(
					    shader, plan, *variant, color ? color->desc.format : Rhi::Format::RgbA8Unorm,
					    externalDepth ? externalDepth->desc.format : Rhi::Format::D32Float, *item.material);
					Rhi::BindGroupPtr group;
					{
						URSA_PROFILE_SCOPE("SceneDrawEncoder.Record.BuildBindGroups");
						group = ShaderGroupFor(shader, cache, plan, *variant, item.material, viewProjection,
						                       camera.view, *item.object, frame, command);
					}
					recordedPasses.push_back({itemIndex, cache.pipeline, std::move(group)});
				}
			}
		}
		{
			URSA_PROFILE_SCOPE("SceneDrawEncoder.Record.EncodeCommands");
			const Rhi::RenderTarget& target = activeEncoding->target;
			done(command->BeginRendering(target));
			if (activeEncoding)
			{
				if (activeEncoding->viewport)
				{
					done(command->SetViewport(*activeEncoding->viewport));
				}
				if (activeEncoding->scissor)
				{
					done(command->SetScissor(*activeEncoding->scissor));
				}
			}
			const MeshSnapshot* boundMesh = nullptr;
			for (const RecordedPass& pass : recordedPasses)
			{
				const auto& draw = *items[pass.itemIndex].object;
				const MeshPtr& mesh = draw.mesh;
				const auto snapshot = mesh ? (draw.meshSnapshot ? draw.meshSnapshot : mesh->Snapshot()) : nullptr;
				if (snapshot.get() != boundMesh)
				{
					if (mesh)
					{
						GpuModelMesh& gpuMesh = MeshFor(mesh, snapshot);
						done(command->BindVertexBuffer(gpuMesh.vertexBuffer));
						done(command->BindIndexBuffer(gpuMesh.indexBuffer));
					}
					boundMesh = snapshot.get();
				}
				done(command->BindPipeline(pass.pipeline));
				if (pass.group)
				{
					done(command->BindGroup(pass.group));
				}
				SubMesh range{0, 36, 0};
				if (mesh)
				{
					if (!snapshot || draw.subMesh >= snapshot->data.subMeshes.size())
					{
						throw std::runtime_error("Renderer submesh index is invalid");
					}
					range = snapshot->data.subMeshes[draw.subMesh];
				}
				done(command->DrawIndexed(range.indexCount, 1, UInt32(pass.itemIndex), range.firstIndex,
				                          range.baseVertex));
			}
			done(command->EndRendering());
		}
	}

	Rhi::DevicePtr device;
	UInt64 meshUploads = 0;
	UInt64 reflectionBuilds = 0;
	UInt64 pipelineCreates = 0;
	UInt64 materialGroupReuses = 0;
	UInt64 materialGroupCreates = 0;
	std::shared_ptr<const ShaderAsset> builtinUnlit;
	std::shared_ptr<const ShaderAsset> builtinPbr;
	std::shared_ptr<const ShaderAsset> builtinFinalPost;
	Map<ShaderPipelineKey, ShaderPipelineCache> shaderPipelines;
	Map<String, Rhi::BindGroupPtr> materialGroups;
	Map<String, Vector<UInt8>> materialUniformBacking;
	Map<String, Rhi::SamplerPtr> samplerCache;
	Map<String, Rhi::BufferPtr> uniformUploadCache;
	UnorderedMap<const MaterialInstance*, ResolvedMaterialCacheEntry> resolvedMaterialCache;
	Map<String, GpuModelMesh> modelMeshes;
};

SceneDrawEncoder::SceneDrawEncoder(std::unique_ptr<Impl> implementation) : impl(std::move(implementation))
{
}

SceneDrawEncoder::~SceneDrawEncoder() = default;

void SceneDrawEncoder::BeginFrame()
{
	impl->MaintainCaches();
}

Result<std::unique_ptr<SceneDrawEncoder>> SceneDrawEncoder::Create(Rhi::DevicePtr selectedDevice)
{
	try
	{
		if (!selectedDevice)
		{
			throw std::runtime_error("3D renderer requires a device");
		}
		auto implementation = std::make_unique<Impl>(std::move(selectedDevice));
		implementation->Initialize();
		return Result<std::unique_ptr<SceneDrawEncoder>>::Success(
		    std::unique_ptr<SceneDrawEncoder>(new SceneDrawEncoder(std::move(implementation))));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<SceneDrawEncoder>>::Failure(error.what());
	}
}

Result<void> SceneDrawEncoder::RecordSceneDraws(const Rhi::CommandListPtr& command,
                                                const Vector<Rendering::SceneDrawRecord>& draws,
                                                const Rendering::CameraData& camera, const SceneLighting& lighting,
                                                const Rendering::SceneDrawEncoding& encoding)
{
	try
	{
		if (!encoding.frames || !encoding.uploadTexture || !encoding.textureBarrier || !encoding.bufferBarrier)
		{
			throw std::runtime_error("Scene drawing requires an active render frame upload queue");
		}
		impl->activeEncoding = &encoding;
		impl->activeDraws = &draws;
		impl->Record(command, draws, camera, lighting);
		impl->activeEncoding = nullptr;
		impl->activeDraws = nullptr;
		return {};
	}
	catch (const std::exception& error)
	{
		impl->activeEncoding = nullptr;
		impl->activeDraws = nullptr;
		return Result<void>::Failure(error.what());
	}
}

Size SceneDrawEncoder::MeshCacheSize() const
{
	return impl->modelMeshes.size();
}

Result<std::shared_ptr<const Shader::ShaderAsset>> SceneDrawEncoder::ResolveBuiltinShader(ResourceId id) const
{
	if (id == Shader::BuiltinUnlitShaderId())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(impl->builtinUnlit);
	}
	if (id == Shader::BuiltinPbrShaderId())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(impl->builtinPbr);
	}
	if (id == Shader::BuiltinFinalPostShaderId())
	{
		return Result<std::shared_ptr<const Shader::ShaderAsset>>::Success(impl->builtinFinalPost);
	}
	return Result<std::shared_ptr<const Shader::ShaderAsset>>::Failure("Unknown builtin Shader " + id.ToString());
}
} // namespace Ursa::Rendering
