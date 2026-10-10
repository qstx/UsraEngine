#include <Core/Containers.hpp>
#include <Core/Types.hpp>
#include <Renderer3D/Renderer3D.hpp>
#include <Core/Profiler.hpp>
#include <Core/Memory/MemoryAlgorithms.hpp>
#include <Core/Memory/MemoryFactories.hpp>
#include <Core/StringInterop.hpp>
#include <Shader/ShaderReflection.hpp>
#include <Shader/ShaderAssetResource.hpp>
#include <Shader/ShaderSource.hpp>
#include <Vfs/AssetRoots.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace Ursa
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
	Ursa::Float32 tangent[3];
	Ursa::Float32 color[4];
	Ursa::UInt32 jointIndices[4]{};
	Ursa::Float32 jointWeights[4]{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(offsetof(Vertex, jointIndices) == 60, "Unexpected model vertex skin index offset");
static_assert(sizeof(Vertex) == 92, "Unexpected model vertex stride");

struct CubeMesh
{
	Array<Vertex, 24> vertices{};
	Array<Ursa::UInt32, 36> indices{};
};

CubeMesh MakeCube()
{
	CubeMesh mesh;
	Ursa::Size vertex = 0;
	Ursa::Size index = 0;
	const auto face = [&](Vec3 normal, const Array<Vec3, 4>& corners)
	{
		const Ursa::UInt32 first = static_cast<Ursa::UInt32>(vertex);
		const Vec3 tangent = Normalized(corners[1] - corners[0]);
		constexpr Ursa::Float32 uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
		for (Ursa::Size corner = 0; corner < corners.size(); ++corner)
		{
			const Vec3 p = corners[corner];
			mesh.vertices[vertex++] = {{p.x, p.y, p.z},
			                           {normal.x, normal.y, normal.z},
			                           {uvs[corner][0], uvs[corner][1]},
			                           {tangent.x, tangent.y, tangent.z},
			                           {1.0f, 1.0f, 1.0f, 1.0f}};
		}
		for (Ursa::UInt32 corner : {0u, 2u, 1u, 0u, 3u, 2u})
		{
			mesh.indices[index++] = first + corner;
		}
	};
	constexpr Ursa::Float32 h = 0.5f;
	face({0, 0, 1}, {{{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}});
	face({0, 0, -1}, {{{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}});
	face({1, 0, 0}, {{{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}});
	face({-1, 0, 0}, {{{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}});
	face({0, 1, 0}, {{{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}});
	face({0, -1, 0}, {{{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}});
	return mesh;
}

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
	Ursa::Float32 cameraPosition[4]{};
	Ursa::Float32 ambient[4]{};
	Ursa::Int32 counts[4]{};
	GpuDirectionalLight directional[SceneLighting::maxDirectionalLights]{};
	GpuPointLight point[SceneLighting::maxPointLights]{};
};

static_assert(sizeof(GpuFrameHeader) == 496, "Unexpected frame layout");

void WriteColor(Ursa::Float32 (&out)[4], Color color)
{
	out[0] = color.red;
	out[1] = color.green;
	out[2] = color.blue;
	out[3] = color.alpha;
}

GpuFrameHeader BuildFrameHeader(const Mat4& viewProjection, const SceneLighting& lighting, Vec3 cameraPosition)
{
	GpuFrameHeader frame{};
	std::memcpy(frame.viewProjection, viewProjection.values.data(), sizeof(frame.viewProjection));
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

Vector<ModelBoxInstance> ModelsFromBoxes(const Vector<BoxInstance>& boxes)
{
	Vector<ModelBoxInstance> models;
	models.reserve(boxes.size());
	for (const auto& box : boxes)
	{
		models.push_back({box.transform.Matrix(), box.color, box.material});
	}
	return models;
}

struct DrawItem
{
	const ModelBoxInstance* box = nullptr;
	std::shared_ptr<MaterialInstance> material;
	AlphaMode mode = AlphaMode::Opaque;
	Ursa::Float32 distanceSquared = 0.0f;
	Ursa::Size ordinal = 0;
};

struct GpuModelMesh
{
	ModelMeshPtr source;
	Rhi::BufferPtr vertexBuffer;
	Rhi::BufferPtr indexBuffer;
	Ursa::UInt32 indexCount = 0;
};

AlphaMode ShaderAlphaMode(const std::shared_ptr<MaterialInstance>& material);

TemporaryVector<DrawItem> OrderItems(const TemporaryVector<ModelBoxInstance>& boxes,
                                     const std::shared_ptr<MaterialInstance>& fallback, Vec3 cameraPosition)
{
	TemporaryVector<DrawItem> items{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer)};
	items.reserve(boxes.size());
	for (Ursa::Size i = 0; i < boxes.size(); ++i)
	{
		const auto& box = boxes[i];
		const Vec3 delta{box.model.At(0, 3) - cameraPosition.x, box.model.At(1, 3) - cameraPosition.y,
		                 box.model.At(2, 3) - cameraPosition.z};
		auto material = box.material ? box.material : fallback;
		items.push_back({&box, material, ShaderAlphaMode(material), Dot(delta, delta), i});
	}
	StableSort(
	    items.begin(), items.end(),
	    [](const DrawItem& a, const DrawItem& b)
	    {
		    if (a.mode != b.mode)
		    {
			    return static_cast<int>(a.mode) < static_cast<int>(b.mode);
		    }
		    if (a.mode == AlphaMode::Blend)
		    {
			    return a.distanceSquared == b.distanceSquared ? a.ordinal < b.ordinal
			                                                  : a.distanceSquared > b.distanceSquared;
		    }
		    return std::less<const MaterialInstance*>{}(a.material.get(), b.material.get());
	    },
	    Memory::MemoryTag::Renderer);
	return items;
}

Vector<String> SplitStateValue(const String& value)
{
	std::string text = ToStdString(value);
	for (char& character : text)
	{
		if (character == ',')
		{
			character = ' ';
		}
	}
	std::istringstream stream(text);
	Vector<String> tokens;
	std::string token;
	while (stream >> token)
	{
		tokens.push_back(ToUrsaString(token));
	}
	return tokens;
}

Rhi::Compare ShaderCompare(const String& token)
{
	if (token == "Never")
	{
		return Rhi::Compare::Never;
	}
	if (token == "Less")
	{
		return Rhi::Compare::Less;
	}
	if (token == "LEqual")
	{
		return Rhi::Compare::LessEqual;
	}
	if (token == "Equal")
	{
		return Rhi::Compare::Equal;
	}
	if (token == "NotEqual")
	{
		return Rhi::Compare::NotEqual;
	}
	if (token == "Greater")
	{
		return Rhi::Compare::Greater;
	}
	if (token == "GEqual")
	{
		return Rhi::Compare::GreaterEqual;
	}
	if (token == "Always")
	{
		return Rhi::Compare::Always;
	}
	throw std::runtime_error("Unsupported ShaderLab comparison value " + ToStdString(token));
}

Rhi::BlendFactor ShaderBlendFactor(const String& token)
{
	if (token == "Zero")
	{
		return Rhi::BlendFactor::Zero;
	}
	if (token == "One")
	{
		return Rhi::BlendFactor::One;
	}
	if (token == "SrcColor")
	{
		return Rhi::BlendFactor::SourceColor;
	}
	if (token == "OneMinusSrcColor")
	{
		return Rhi::BlendFactor::OneMinusSourceColor;
	}
	if (token == "DstColor")
	{
		return Rhi::BlendFactor::DestinationColor;
	}
	if (token == "OneMinusDstColor")
	{
		return Rhi::BlendFactor::OneMinusDestinationColor;
	}
	if (token == "SrcAlpha")
	{
		return Rhi::BlendFactor::SourceAlpha;
	}
	if (token == "OneMinusSrcAlpha")
	{
		return Rhi::BlendFactor::OneMinusSourceAlpha;
	}
	if (token == "DstAlpha")
	{
		return Rhi::BlendFactor::DestinationAlpha;
	}
	if (token == "OneMinusDstAlpha")
	{
		return Rhi::BlendFactor::OneMinusDestinationAlpha;
	}
	throw std::runtime_error("Unsupported ShaderLab blend factor " + ToStdString(token));
}

Rhi::BlendOperation ShaderBlendOperation(const String& token)
{
	if (token == "Add")
	{
		return Rhi::BlendOperation::Add;
	}
	if (token == "Sub")
	{
		return Rhi::BlendOperation::Subtract;
	}
	if (token == "RevSub")
	{
		return Rhi::BlendOperation::ReverseSubtract;
	}
	if (token == "Min")
	{
		return Rhi::BlendOperation::Minimum;
	}
	if (token == "Max")
	{
		return Rhi::BlendOperation::Maximum;
	}
	throw std::runtime_error("Unsupported ShaderLab blend operation " + ToStdString(token));
}

Rhi::StencilOperation ShaderStencilOperation(const String& token)
{
	if (token == "Keep")
	{
		return Rhi::StencilOperation::Keep;
	}
	if (token == "Zero")
	{
		return Rhi::StencilOperation::Zero;
	}
	if (token == "Replace")
	{
		return Rhi::StencilOperation::Replace;
	}
	if (token == "IncrSat")
	{
		return Rhi::StencilOperation::IncrementClamp;
	}
	if (token == "DecrSat")
	{
		return Rhi::StencilOperation::DecrementClamp;
	}
	if (token == "Invert")
	{
		return Rhi::StencilOperation::Invert;
	}
	if (token == "IncrWrap")
	{
		return Rhi::StencilOperation::IncrementWrap;
	}
	if (token == "DecrWrap")
	{
		return Rhi::StencilOperation::DecrementWrap;
	}
	throw std::runtime_error("Unsupported ShaderLab stencil operation " + ToStdString(token));
}

void ApplyStencilState(Rhi::StencilState& stencil, const String& name, const String& value)
{
	const auto tokens = SplitStateValue(value);
	if (tokens.empty())
	{
		throw std::runtime_error("Missing value for ShaderLab state " + ToStdString(name));
	}
	stencil.enabled = true;
	if (name == "Stencil.Ref" || name == "Stencil.ReadMask" || name == "Stencil.WriteMask")
	{
		char* end = nullptr;
		const std::string token = ToStdString(tokens[0]);
		const unsigned long parsed = std::strtoul(token.c_str(), &end, 0);
		if (end != token.c_str() + token.size() || parsed > std::numeric_limits<Ursa::UInt32>::max())
		{
			throw std::runtime_error("Invalid numeric ShaderLab stencil value " + token);
		}
		const Ursa::UInt32 result = static_cast<Ursa::UInt32>(parsed);
		if (name == "Stencil.Ref")
		{
			stencil.reference = result;
		}
		else if (name == "Stencil.ReadMask")
		{
			stencil.readMask = result;
		}
		else
		{
			stencil.writeMask = result;
		}
		return;
	}
	const Ursa::Size frontStart = name.find("Stencil.Front.") == 0 ? 1 : 0;
	const Ursa::Size backStart = name.find("Stencil.Back.") == 0 ? 1 : 0;
	if (frontStart && backStart)
	{
		throw std::runtime_error("Invalid ShaderLab stencil face state " + ToStdString(name));
	}
	Rhi::StencilFaceState* faces[2] = {&stencil.front, &stencil.back};
	const Ursa::Size faceBegin = frontStart ? 0 : backStart ? 1 : 0;
	const Ursa::Size faceEnd = frontStart || backStart ? faceBegin + 1 : 2;
	String field = name;
	const auto lastDot = field.rfind('.');
	field = lastDot == String::npos ? field : field.substr(lastDot + 1);
	for (Ursa::Size faceIndex = faceBegin; faceIndex < faceEnd; ++faceIndex)
	{
		if (field == "Comp")
		{
			faces[faceIndex]->compare = ShaderCompare(tokens[0]);
		}
		else if (field == "Pass")
		{
			faces[faceIndex]->passOperation = ShaderStencilOperation(tokens[0]);
		}
		else if (field == "Fail")
		{
			faces[faceIndex]->failOperation = ShaderStencilOperation(tokens[0]);
		}
		else if (field == "ZFail")
		{
			faces[faceIndex]->depthFailOperation = ShaderStencilOperation(tokens[0]);
		}
		else
		{
			throw std::runtime_error("Unsupported ShaderLab stencil field " + ToStdString(name));
		}
	}
}

void ApplyRenderStates(Rhi::GraphicsPipelineDesc& desc, const Vector<RenderStateDeclaration>& states)
{
	for (const RenderStateDeclaration& state : states)
	{
		const auto tokens = SplitStateValue(state.value);
		if (state.name.find("Stencil.") == 0)
		{
			ApplyStencilState(desc.stencil, state.name, state.value);
			continue;
		}
		if (state.name == "Cull")
		{
			if (tokens.size() != 1)
			{
				throw std::runtime_error("Cull requires one value");
			}
			if (tokens[0] == "Off")
			{
				desc.cullMode = Rhi::CullMode::None;
			}
			else if (tokens[0] == "Front")
			{
				desc.cullMode = Rhi::CullMode::Front;
			}
			else if (tokens[0] == "Back")
			{
				desc.cullMode = Rhi::CullMode::Back;
			}
			else
			{
				throw std::runtime_error("Unsupported Cull value " + ToStdString(tokens[0]));
			}
		}
		else if (state.name == "ZWrite")
		{
			desc.depthWrite = tokens.size() == 1 && tokens[0] == "On";
			if (tokens.size() != 1 || (!desc.depthWrite && tokens[0] != "Off"))
			{
				throw std::runtime_error("ZWrite must be On or Off");
			}
		}
		else if (state.name == "ZTest")
		{
			if (tokens.size() != 1)
			{
				throw std::runtime_error("ZTest requires one value");
			}
			if (tokens[0] == "Off" || tokens[0] == "Disabled")
			{
				desc.depthTest = false;
			}
			else
			{
				desc.depthTest = true;
				desc.depthCompare = ShaderCompare(tokens[0]);
			}
		}
		else if (state.name == "Blend")
		{
			if (tokens.size() == 1 && tokens[0] == "Off")
			{
				desc.blendState.enabled = false;
				desc.blend = false;
				continue;
			}
			if (tokens.size() != 2 && tokens.size() != 4)
			{
				throw std::runtime_error("Blend requires two or four factors");
			}
			desc.blendState.enabled = true;
			desc.blendState.sourceColor = ShaderBlendFactor(tokens[0]);
			desc.blendState.destinationColor = ShaderBlendFactor(tokens[1]);
			desc.blendState.sourceAlpha =
			    tokens.size() == 4 ? ShaderBlendFactor(tokens[2]) : desc.blendState.sourceColor;
			desc.blendState.destinationAlpha =
			    tokens.size() == 4 ? ShaderBlendFactor(tokens[3]) : desc.blendState.destinationColor;
		}
		else if (state.name == "BlendOp")
		{
			if (tokens.empty() || tokens.size() > 2)
			{
				throw std::runtime_error("BlendOp requires one or two operations");
			}
			desc.blendState.colorOperation = ShaderBlendOperation(tokens[0]);
			desc.blendState.alphaOperation =
			    tokens.size() == 2 ? ShaderBlendOperation(tokens[1]) : desc.blendState.colorOperation;
		}
		else if (state.name == "ColorMask")
		{
			if (tokens.size() != 1)
			{
				throw std::runtime_error("ColorMask requires one value");
			}
			const String& mask = tokens[0];
			Rhi::ColorWriteMask result = Rhi::ColorWriteMask::None;
			if (mask == "All" || mask == "RGBA")
			{
				result = Rhi::ColorWriteMask::All;
			}
			else if (mask != "0")
			{
				for (char channel : mask)
				{
					if (channel == 'R')
					{
						result = result | Rhi::ColorWriteMask::Red;
					}
					else if (channel == 'G')
					{
						result = result | Rhi::ColorWriteMask::Green;
					}
					else if (channel == 'B')
					{
						result = result | Rhi::ColorWriteMask::Blue;
					}
					else if (channel == 'A')
					{
						result = result | Rhi::ColorWriteMask::Alpha;
					}
					else
					{
						throw std::runtime_error("Invalid ColorMask channel");
					}
				}
			}
			desc.colorWriteMask = result;
		}
		else if (state.name == "Offset")
		{
			if (tokens.size() != 2)
			{
				throw std::runtime_error("Offset requires factor and units");
			}
			const Float32 factor = std::strtof(ToStdString(tokens[0]).c_str(), nullptr);
			const Float32 units = std::strtof(ToStdString(tokens[1]).c_str(), nullptr);
			if (!std::isfinite(factor) || !std::isfinite(units) ||
			    units < static_cast<Float32>(std::numeric_limits<Int32>::min()) ||
			    units > static_cast<Float32>(std::numeric_limits<Int32>::max()))
			{
				throw std::runtime_error("Offset contains invalid numeric values");
			}
			desc.depthBiasSlope = factor;
			desc.depthBiasConstant = static_cast<Int32>(std::lround(units));
		}
		else
		{
			throw std::runtime_error("Unsupported ShaderLab render state " + ToStdString(state.name));
		}
	}
}

bool HasTag(const Vector<TagDeclaration>& tags, const String& key, const String& value)
{
	for (const TagDeclaration& tag : tags)
	{
		if (tag.name == key)
		{
			return tag.value == value;
		}
	}
	return false;
}

struct ShaderPassPlan
{
	std::shared_ptr<const ShaderAsset> programShader;
	const ShaderPassAsset* pass = nullptr;
	Vector<RenderStateDeclaration> renderStates;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
};

const ShaderPassAsset* FindNamedPass(const ShaderAsset& shader, const String& name, UInt32& subShaderIndex,
                                     UInt32& passIndex)
{
	for (Ursa::Size sub = 0; sub < shader.subShaders.size(); ++sub)
	{
		for (Ursa::Size pass = 0; pass < shader.subShaders[sub].passes.size(); ++pass)
		{
			if (shader.subShaders[sub].passes[pass].name == name)
			{
				subShaderIndex = static_cast<UInt32>(sub);
				passIndex = static_cast<UInt32>(pass);
				return &shader.subShaders[sub].passes[pass];
			}
		}
	}
	return nullptr;
}

Result<Vector<ShaderPassPlan>> SelectForwardPasses(const std::shared_ptr<const ShaderAsset>& rootShader)
{
	Set<String> active;
	auto visit = [&](auto&& self, const std::shared_ptr<const ShaderAsset>& shader) -> Result<Vector<ShaderPassPlan>>
	{
		const String shaderKey = shader->id.ToString();
		if (active.find(shaderKey) != active.end())
		{
			return Result<Vector<ShaderPassPlan>>::Failure("Shader fallback cycle while selecting " + shader->name);
		}
		active.insert(shaderKey);
		for (Ursa::Size subIndex = 0; subIndex < shader->subShaders.size(); ++subIndex)
		{
			const ShaderSubShaderAsset& subShader = shader->subShaders[subIndex];
			bool pipelineMatches = true;
			for (const TagDeclaration& tag : subShader.tags)
			{
				if (tag.name == "RenderPipeline" && tag.value != "UrsaForward")
				{
					pipelineMatches = false;
				}
			}
			if (!pipelineMatches)
			{
				continue;
			}
			Vector<ShaderPassPlan> selected;
			Vector<SubShaderPassOrderEntry> order = subShader.passOrder;
			if (order.empty())
			{
				for (Ursa::Size index = 0; index < subShader.passes.size(); ++index)
				{
					order.push_back({static_cast<UInt32>(index), {}, false});
				}
			}
			for (const SubShaderPassOrderEntry& entry : order)
			{
				std::shared_ptr<const ShaderAsset> passShader = shader;
				const ShaderPassAsset* pass = nullptr;
				UInt32 passSubShaderIndex = static_cast<UInt32>(subIndex);
				UInt32 passIndex = entry.localPassIndex;
				Vector<RenderStateDeclaration> states = subShader.renderStates;
				if (entry.isReused)
				{
					const Ursa::Size slash = entry.reusedPass.rfind('/');
					if (slash == String::npos)
					{
						return Result<Vector<ShaderPassPlan>>::Failure("Invalid UsePass reference " + entry.reusedPass);
					}
					const String targetShaderName = entry.reusedPass.substr(0, slash);
					const String targetPassName = entry.reusedPass.substr(slash + 1);
					const auto resolved =
					    std::find_if(shader->resolvedReferences.begin(), shader->resolvedReferences.end(),
					                 [&](const ResolvedShaderReference& reference)
					                 {
						                 return !reference.reference.fallback &&
						                        reference.reference.shaderName == targetShaderName &&
						                        reference.reference.passName == targetPassName;
					                 });
					if (resolved == shader->resolvedReferences.end())
					{
						return Result<Vector<ShaderPassPlan>>::Failure("Unresolved UsePass " + entry.reusedPass +
						                                               " in Shader " + shader->name);
					}
					passShader = resolved->asset;
					pass = FindNamedPass(*passShader, targetPassName, passSubShaderIndex, passIndex);
					if (!pass)
					{
						return Result<Vector<ShaderPassPlan>>::Failure("Resolved UsePass target is missing " +
						                                               entry.reusedPass);
					}
					states.insert(states.end(), passShader->subShaders[passSubShaderIndex].renderStates.begin(),
					              passShader->subShaders[passSubShaderIndex].renderStates.end());
				}
				else
				{
					if (entry.localPassIndex >= subShader.passes.size())
					{
						return Result<Vector<ShaderPassPlan>>::Failure("Shader pass order is out of range");
					}
					pass = &subShader.passes[entry.localPassIndex];
				}
				bool forward = true;
				for (const TagDeclaration& tag : pass->tags)
				{
					if (tag.name == "LightMode" && tag.value != "Forward" && tag.value != "SRPDefaultUnlit")
					{
						forward = false;
					}
				}
				if (!forward)
				{
					continue;
				}
				states.insert(states.end(), pass->renderStates.begin(), pass->renderStates.end());
				selected.push_back({std::move(passShader), pass, std::move(states), passSubShaderIndex, passIndex});
			}
			if (!selected.empty())
			{
				active.erase(shaderKey);
				return Result<Vector<ShaderPassPlan>>::Success(std::move(selected));
			}
		}
		const auto fallback =
		    std::find_if(shader->resolvedReferences.begin(), shader->resolvedReferences.end(),
		                 [](const ResolvedShaderReference& reference) { return reference.reference.fallback; });
		if (fallback != shader->resolvedReferences.end())
		{
			auto selected = self(self, fallback->asset);
			active.erase(shaderKey);
			return selected;
		}
		active.erase(shaderKey);
		return Result<Vector<ShaderPassPlan>>::Failure("No compatible Forward SubShader or fallback for " +
		                                               shader->name);
	};
	return visit(visit, rootShader);
}

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

AlphaMode ShaderAlphaMode(const std::shared_ptr<MaterialInstance>& material)
{
	if (!material->IsShaderBacked() || !material->Shader())
	{
		return material->Parameters().alphaMode;
	}
	for (const ShaderSubShaderAsset& subShader : material->Shader()->subShaders)
	{
		for (const ShaderPassAsset& pass : subShader.passes)
		{
			if (!HasTag(pass.tags, "LightMode", "Forward") && !HasTag(pass.tags, "LightMode", "SRPDefaultUnlit"))
			{
				continue;
			}
			for (const RenderStateDeclaration& state : pass.renderStates)
			{
				if (state.name == "Blend" && state.value.find("Off") != 0)
				{
					return AlphaMode::Blend;
				}
			}
		}
	}
	return material->Parameters().alphaMode;
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

struct Renderer3D::Impl
{
	struct ShaderPipelineCache
	{
		ShaderBindingLayout reflection;
		Rhi::LayoutPtr layout;
		Rhi::PipelinePtr pipeline;
	};

	explicit Impl(Rhi::DevicePtr selectedDevice) : device(std::move(selectedDevice))
	{
		fallback = std::make_shared<MaterialInstance>(MaterialType::Unlit);
	}

	~Impl()
	{
		if (lastFence)
		{
			lastFence->Wait();
		}
	}

	void Initialize()
	{
		const CubeMesh cube = MakeCube();
		vertexBuffer =
		    Take(device->CreateBuffer({sizeof(cube.vertices), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload}));
		indexBuffer = Take(device->CreateBuffer({sizeof(cube.indices), Rhi::BufferUsage::Index, Rhi::Memory::Upload}));
		done(vertexBuffer->Write(0, cube.vertices.data(), sizeof(cube.vertices)));
		done(indexBuffer->Write(0, cube.indices.data(), sizeof(cube.indices)));
		auto engineFiles = OpenEngineFileSystem();
		if (!engineFiles)
		{
			throw std::runtime_error(ToStdString(engineFiles.error));
		}
		builtinUnlit = std::make_shared<ShaderAsset>(Take(LoadShaderAssetResource(
		    engineFiles.value, Take(VirtualPath::Parse("ursa:Shaders/Renderer3D/Unlit.ursashaderasset")))));
		builtinPbr = std::make_shared<ShaderAsset>(Take(LoadShaderAssetResource(
		    engineFiles.value, Take(VirtualPath::Parse("ursa:Shaders/Renderer3D/Pbr.ursashaderasset")))));
		fallback = std::make_shared<MaterialInstance>(MaterialType::Unlit);
		done(fallback->SetShaderAsset(builtinUnlit));
	}

	GpuModelMesh& MeshFor(const ModelMeshPtr& mesh)
	{
		if (!mesh || !mesh->modelGuid || mesh->localFileId == 0 ||
		    mesh->vertexStride != offsetof(Vertex, jointIndices) || mesh->vertexBytes.empty() ||
		    mesh->indices.empty() || mesh->indices.size() > UINT32_MAX)
		{
			throw std::runtime_error("Renderer received an invalid imported model mesh");
		}
		const String key = mesh->modelGuid.ToString() + ":" + ToUrsaString(std::to_string(mesh->localFileId));
		const auto cached = modelMeshes.find(key);
		if (cached != modelMeshes.end())
		{
			return cached->second;
		}
		GpuModelMesh gpuMesh;
		gpuMesh.source = mesh;
		const Ursa::Size vertexCount = mesh->vertexBytes.size() / mesh->vertexStride;
		if (mesh->vertexBytes.size() % mesh->vertexStride != 0 ||
		    (!mesh->skinInfluences.empty() && mesh->skinInfluences.size() != vertexCount))
		{
			throw std::runtime_error("Imported mesh vertex skin data does not match its vertex count");
		}
		Vector<Ursa::UInt8> packedVertices(vertexCount * sizeof(Vertex));
		for (Ursa::Size vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
		{
			Vertex vertex{};
			std::memcpy(&vertex, mesh->vertexBytes.data() + vertexIndex * mesh->vertexStride, mesh->vertexStride);
			if (!mesh->skinInfluences.empty())
			{
				for (Ursa::Size influence = 0; influence < 4; ++influence)
				{
					vertex.jointIndices[influence] = mesh->skinInfluences[vertexIndex].jointIndices[influence];
					vertex.jointWeights[influence] = mesh->skinInfluences[vertexIndex].weights[influence];
				}
			}
			std::memcpy(packedVertices.data() + vertexIndex * sizeof(Vertex), &vertex, sizeof(Vertex));
		}
		gpuMesh.vertexBuffer =
		    Take(device->CreateBuffer({packedVertices.size(), Rhi::BufferUsage::Vertex, Rhi::Memory::Upload}));
		gpuMesh.indexBuffer = Take(device->CreateBuffer(
		    {mesh->indices.size() * sizeof(Ursa::UInt32), Rhi::BufferUsage::Index, Rhi::Memory::Upload}));
		done(gpuMesh.vertexBuffer->Write(0, packedVertices.data(), packedVertices.size()));
		done(gpuMesh.indexBuffer->Write(0, mesh->indices.data(), mesh->indices.size() * sizeof(Ursa::UInt32)));
		gpuMesh.indexCount = static_cast<Ursa::UInt32>(mesh->indices.size());
		return modelMeshes.emplace(key, std::move(gpuMesh)).first->second;
	}

	void WaitPrevious()
	{
		if (lastFence)
		{
			done(lastFence->Wait());
			lastFence.reset();
		}
		for (auto it = textureCache.begin(); it != textureCache.end();)
		{
			if (it->second.source.use_count() == 1)
			{
				it = textureCache.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void PrepareTarget(const Rhi::TexturePtr& color, bool needsStencil = false)
	{
		if (!color)
		{
			throw std::runtime_error("3D renderer requires a color target");
		}
		const Rhi::Format depthFormat = needsStencil ? Rhi::Format::D32FloatS8Uint : Rhi::Format::D32Float;
		if (!depth || depth->desc.width != color->desc.width || depth->desc.height != color->desc.height ||
		    depth->desc.format != depthFormat)
		{
			depth = Take(device->CreateTexture(
			    {color->desc.width, color->desc.height, depthFormat, Rhi::TextureUsage::DepthAttachment}));
		}
	}

	ShaderPipelineCache& ShaderPipelineFor(const std::shared_ptr<const ShaderAsset>& propertyShader,
	                                       const ShaderPassPlan& plan, const ShaderVariantAsset& variant,
	                                       Rhi::Format colorFormat, Rhi::Format depthFormat,
	                                       const MaterialInstance& material)
	{
		auto builtReflection = BuildShaderBindingLayout(*propertyShader, variant);
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
		if (!builtReflection.value.vertexInputs.empty())
		{
			description.vertexStride = sizeof(Vertex);
			for (const ShaderVertexInput& input : builtReflection.value.vertexInputs)
			{
				description.attributes.push_back(VertexAttributeFor(input));
			}
		}
		ApplyRenderStates(description, plan.renderStates);
		if (material.Parameters().alphaMode == AlphaMode::Blend)
		{
			description.blend = true;
			description.blendState.enabled = true;
			description.blendState.sourceColor = Rhi::BlendFactor::SourceAlpha;
			description.blendState.destinationColor = Rhi::BlendFactor::OneMinusSourceAlpha;
			description.depthWrite = false;
		}
		String key =
		    propertyShader->id.ToString() + ":" + plan.programShader->id.ToString() + ":" +
		    ToUrsaString(std::to_string(propertyShader->sourceFormatVersion)) + ":" + propertyShader->sourceHash + ":" +
		    ToUrsaString(std::to_string(plan.programShader->sourceFormatVersion)) + ":" +
		    plan.programShader->sourceHash + ":" + plan.programShader->compilerVersion + ":" +
		    ToUrsaString(std::to_string(plan.subShaderIndex)) + ":" + ToUrsaString(std::to_string(plan.passIndex)) +
		    ":" + ToUrsaString(std::to_string(static_cast<int>(colorFormat))) + ":" +
		    ToUrsaString(std::to_string(static_cast<int>(depthFormat)));
		const auto append = [&](Ursa::UInt64 value) { key += ":" + ToUrsaString(std::to_string(value)); };
		for (const String& keyword : variant.keywords)
		{
			key += ":kw=" + keyword;
		}
		append(description.vertexStride);
		for (const Rhi::LayoutBinding& binding : builtReflection.value.descriptors)
		{
			append(binding.binding);
			append(static_cast<Ursa::UInt32>(binding.type));
			append(binding.count);
			append(binding.stages);
		}
		for (const Rhi::VertexAttribute& attribute : description.attributes)
		{
			append(attribute.location);
			append(attribute.offset);
			append(static_cast<Ursa::UInt32>(attribute.format));
		}
		append(static_cast<Ursa::UInt32>(description.cullMode));
		append(description.depthTest);
		append(description.depthWrite);
		append(static_cast<Ursa::UInt32>(description.depthCompare));
		append(description.blend);
		append(description.blendState.enabled);
		append(static_cast<Ursa::UInt32>(description.blendState.sourceColor));
		append(static_cast<Ursa::UInt32>(description.blendState.destinationColor));
		append(static_cast<Ursa::UInt32>(description.blendState.colorOperation));
		append(static_cast<Ursa::UInt32>(description.blendState.sourceAlpha));
		append(static_cast<Ursa::UInt32>(description.blendState.destinationAlpha));
		append(static_cast<Ursa::UInt32>(description.blendState.alphaOperation));
		append(static_cast<Ursa::UInt32>(description.colorWriteMask));
		append(description.stencil.enabled);
		append(description.stencil.reference);
		append(description.stencil.readMask);
		append(description.stencil.writeMask);
		const auto appendStencilFace = [&](const Rhi::StencilFaceState& face)
		{
			append(static_cast<Ursa::UInt32>(face.compare));
			append(static_cast<Ursa::UInt32>(face.failOperation));
			append(static_cast<Ursa::UInt32>(face.passOperation));
			append(static_cast<Ursa::UInt32>(face.depthFailOperation));
		};
		appendStencilFace(description.stencil.front);
		appendStencilFace(description.stencil.back);
		append(static_cast<Ursa::UInt32>(description.depthBiasConstant));
		Ursa::UInt32 biasSlopeBits = 0;
		static_assert(sizeof(biasSlopeBits) == sizeof(description.depthBiasSlope), "Unexpected float size");
		std::memcpy(&biasSlopeBits, &description.depthBiasSlope, sizeof(biasSlopeBits));
		append(biasSlopeBits);
		auto found = shaderPipelines.find(key);
		if (found != shaderPipelines.end())
		{
			return found->second;
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
		if (!vertex || !fragment)
		{
			throw std::runtime_error("Forward Shader Pass requires vertex and fragment stages");
		}
		description.vertex = vertex;
		description.fragment = fragment;
		description.layout = cache.layout;
		cache.pipeline = Take(device->CreateGraphicsPipeline(description));
		auto inserted = shaderPipelines.emplace(std::move(key), std::move(cache));
		return inserted.first->second;
	}

	Rhi::BindGroupPtr ShaderGroupFor(const std::shared_ptr<const ShaderAsset>& propertyShader,
	                                 const ShaderPipelineCache& cache,
	                                 const std::shared_ptr<MaterialInstance>& material, const Mat4& viewProjection,
	                                 const ModelBoxInstance& item, const GpuFrameHeader& frame,
	                                 const Rhi::CommandListPtr& command)
	{
		const Mat4& model = item.model;
		Vector<Rhi::BindingResource> resources;
		Vector<Rhi::BufferPtr> buffers;
		Vector<Rhi::SamplerPtr> samplers;
		Map<UInt32, MaterialTexturePtr> texturesByBinding;
		for (const auto& property : cache.reflection.properties)
		{
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
				Vector<Mat4> palette = item.jointPalette;
				if (palette.empty())
				{
					palette.push_back(Mat4::Identity());
				}
				const Ursa::Size byteSize = palette.size() * sizeof(Mat4);
				const Ursa::UInt32 maxStorageRange = device->Capabilities().maxStorageBufferRange;
				if (maxStorageRange == 0 || byteSize > maxStorageRange)
				{
					throw std::runtime_error("Model bone palette exceeds the RHI storage buffer limit");
				}
				auto buffer = Take(device->CreateBuffer({byteSize, Rhi::BufferUsage::Storage, Rhi::Memory::Upload}));
				done(buffer->Write(0, palette.data(), byteSize));
				done(command->Barrier(buffer, Rhi::ResourceState::Undefined, Rhi::ResourceState::StorageReadWrite));
				resources.push_back({descriptor.binding, buffer, {}, {}});
				buffers.push_back(std::move(buffer));
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
				bool perObject = false;
				bool perFrame = false;
				Mat4 inverseModel = model.InverseAffine();
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
						perObject = true;
					}
					else if (member.name == "_UrsaModel")
					{
						source = model.values.data();
						perObject = true;
					}
					else if (member.name == "_UrsaNormal")
					{
						source = normalMatrix.values.data();
						perObject = true;
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
						const Ursa::Size copySize = perFrame && member.byteSize != 0    ? member.byteSize
						                            : member.name == "_UrsaCounts"      ? sizeof(frame.counts)
						                            : member.name == "_UrsaDirectional" ? sizeof(frame.directional)
						                            : member.name == "_UrsaPoint"       ? sizeof(frame.point)
						                                                                : sizeof(Mat4);
						if (member.byteOffset > bytes.size() || copySize > bytes.size() - member.byteOffset)
						{
							throw std::runtime_error("Renderer uniform member exceeds its reflected block");
						}
						std::memcpy(bytes.data() + member.byteOffset, source, copySize);
					}
				}
				if (!perObject && !perFrame)
				{
					auto packed =
					    PackMaterialUniformBuffer(*propertyShader, cache.reflection, *material, descriptor.binding);
					if (!packed)
					{
						throw std::runtime_error(ToStdString(packed.error));
					}
					bytes = std::move(packed.value);
				}
				auto buffer =
				    Take(device->CreateBuffer({bytes.size(), Rhi::BufferUsage::Uniform, Rhi::Memory::Upload}));
				done(buffer->Write(0, bytes.data(), bytes.size()));
				done(command->Barrier(buffer, Rhi::ResourceState::Undefined, Rhi::ResourceState::UniformRead));
				resources.push_back({descriptor.binding, buffer, {}, {}});
				buffers.push_back(std::move(buffer));
			}
			else if (descriptor.type == Rhi::BindingType::SampledTexture ||
			         descriptor.type == Rhi::BindingType::CombinedImageSampler)
			{
				auto texture = texturesByBinding.find(descriptor.binding);
				if (texture == texturesByBinding.end())
				{
					throw std::runtime_error("Shader sampled-image descriptor has no matching material property");
				}
				auto gpuTexture = TextureFor(texture->second, command);
				Rhi::SamplerPtr sampler;
				if (descriptor.type == Rhi::BindingType::CombinedImageSampler)
				{
					const MaterialSampler samplerSettings = material->SamplerOverridden()
					                                            ? material->Parameters().sampler
					                                            : texture->second->DefaultSampler();
					sampler = Take(device->CreateSampler({samplerSettings.linear, samplerSettings.repeat}));
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
				auto sampler = Take(device->CreateSampler({samplerSettings.linear, samplerSettings.repeat}));
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
		return Take(device->CreateBindGroup({cache.layout, std::move(resources)}));
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
			return material;
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
		return material;
	}

	Rhi::TexturePtr TextureFor(const MaterialTexturePtr& source, const Rhi::CommandListPtr& command)
	{
		const auto found = textureCache.find(source.get());
		if (found != textureCache.end())
		{
			return found->second.texture;
		}
		Ursa::Size bytes = source->GetPixels().size();
		for (const TextureMip& mipmap : source->GetMipmaps())
		{
			bytes += mipmap.pixels.size();
		}
		auto upload = Take(device->CreateBuffer({bytes, Rhi::BufferUsage::TransferSource, Rhi::Memory::Upload}));
		Ursa::Size sourceOffset = 0;
		done(upload->Write(sourceOffset, source->GetPixels().data(), source->GetPixels().size()));
		sourceOffset += source->GetPixels().size();
		for (const TextureMip& mipmap : source->GetMipmaps())
		{
			done(upload->Write(sourceOffset, mipmap.pixels.data(), mipmap.pixels.size()));
			sourceOffset += mipmap.pixels.size();
		}
		const auto format =
		    source->GetColorSpace() == TextureColorSpace::Srgb ? Rhi::Format::RgbA8Srgb : Rhi::Format::RgbA8Unorm;
		auto texture = Take(device->CreateTexture({source->GetWidth(), source->GetHeight(), format,
		                                           Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination,
		                                           source->GetMipLevels()}));
		done(command->Barrier(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferDestination));
		sourceOffset = 0;
		for (Ursa::UInt32 mipLevel = 0; mipLevel < source->GetMipLevels(); ++mipLevel)
		{
			const Ursa::Size levelBytes =
			    mipLevel == 0 ? source->GetPixels().size() : source->GetMipmaps()[mipLevel - 1].pixels.size();
			done(command->CopyBufferToTexture(upload, texture, mipLevel, sourceOffset));
			sourceOffset += levelBytes;
		}
		done(command->Barrier(texture, Rhi::ResourceState::TransferDestination, Rhi::ResourceState::ShaderRead));
		textureCache.emplace(source.get(), GpuTexture{source, texture});
		return texture;
	}

	void record(const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color, Rhi::ResourceState before,
	            Rhi::ResourceState after, const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes,
	            const SceneLighting& lighting, Vec3 cameraPosition)
	{
		Memory::TemporaryScope recordingMemory;
		if (!command)
		{
			throw std::runtime_error("3D renderer requires a command list");
		}
		if (lighting.directional.size() > SceneLighting::maxDirectionalLights ||
		    lighting.point.size() > SceneLighting::maxPointLights)
		{
			throw std::runtime_error("Renderer light count exceeds directional or point capacity");
		}
		TemporaryVector<ModelBoxInstance> resolvedBoxes(
		    boxes.begin(), boxes.end(), Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer));
		for (ModelBoxInstance& box : resolvedBoxes)
		{
			box.material = ResolveMaterial(box.material, box.color);
		}
		const auto items = OrderItems(resolvedBoxes, fallback, cameraPosition);
		const GpuFrameHeader frame = BuildFrameHeader(viewProjection, lighting, cameraPosition);
		bool needsStencil = false;
		for (const auto& item : items)
		{
			const auto shader = item.material->Shader();
			if (!item.material->IsShaderBacked() || !shader)
			{
				continue;
			}
			bool compiled = false;
			for (const ShaderSubShaderAsset& subShader : shader->subShaders)
			{
				for (const ShaderPassAsset& pass : subShader.passes)
				{
					compiled = compiled || !pass.variants.empty();
				}
			}
			if (!compiled)
			{
				continue;
			}
			auto plans = SelectForwardPasses(shader);
			if (!plans)
			{
				throw std::runtime_error(ToStdString(plans.error));
			}
			for (const ShaderPassPlan& plan : plans.value)
			{
				for (const RenderStateDeclaration& state : plan.renderStates)
				{
					needsStencil = needsStencil || state.name.rfind("Stencil", 0) == 0;
				}
			}
		}
		PrepareTarget(color, needsStencil);

		struct RecordedPass
		{
			Ursa::Size itemIndex = 0;
			Rhi::PipelinePtr pipeline;
			Rhi::BindGroupPtr group;
		};

		TemporaryVector<RecordedPass> recordedPasses{Memory::TemporaryContainerAllocator(Memory::MemoryTag::Renderer)};
		for (Ursa::Size itemIndex = 0; itemIndex < items.size(); ++itemIndex)
		{
			const auto& item = items[itemIndex];
			if (item.box->mesh)
			{
				MeshFor(item.box->mesh);
			}
			const auto shader = item.material->Shader();
			bool hasCompiledShader = false;
			if (item.material->IsShaderBacked() && shader)
			{
				for (const ShaderSubShaderAsset& subShader : shader->subShaders)
				{
					for (const ShaderPassAsset& pass : subShader.passes)
					{
						hasCompiledShader = hasCompiledShader || !pass.variants.empty();
					}
				}
			}
			if (!hasCompiledShader)
			{
				throw std::runtime_error("Shader asset has no compiled graphics Pass: " + ToStdString(shader->name));
			}
			auto plans = SelectForwardPasses(shader);
			if (!plans)
			{
				throw std::runtime_error(ToStdString(plans.error));
			}
			for (const ShaderPassPlan& plan : plans.value)
			{
				const ShaderVariantAsset* variant = SelectVariant(*plan.pass, item.material->Keywords());
				if (!variant)
				{
					throw std::runtime_error("No compiled keyword variant for Shader " + ToStdString(shader->name) +
					                         " Pass " + ToStdString(plan.pass->name));
				}
				ShaderPipelineCache& cache = ShaderPipelineFor(
				    shader, plan, *variant, color->desc.format,
				    needsStencil ? Rhi::Format::D32FloatS8Uint : Rhi::Format::D32Float, *item.material);
				auto group = ShaderGroupFor(shader, cache, item.material, viewProjection, *item.box, frame, command);
				recordedPasses.push_back({itemIndex, cache.pipeline, std::move(group)});
			}
		}
		done(command->Barrier(color, before, Rhi::ResourceState::ColorAttachment));
		done(command->Barrier(depth, Rhi::ResourceState::Undefined, Rhi::ResourceState::DepthAttachment));
		Rhi::RenderTarget target;
		target.color = color;
		target.depth = depth;
		target.colorValue[0] = 0.035f;
		target.colorValue[1] = 0.055f;
		target.colorValue[2] = 0.09f;
		done(command->BeginRendering(target));
		const ModelMesh* boundMesh = nullptr;
		if (items.empty() || !items.front().box->mesh)
		{
			done(command->BindVertexBuffer(vertexBuffer));
			done(command->BindIndexBuffer(indexBuffer));
		}
		for (const RecordedPass& pass : recordedPasses)
		{
			const ModelMeshPtr& mesh = items[pass.itemIndex].box->mesh;
			if (mesh.get() != boundMesh)
			{
				if (mesh)
				{
					GpuModelMesh& gpuMesh = MeshFor(mesh);
					done(command->BindVertexBuffer(gpuMesh.vertexBuffer));
					done(command->BindIndexBuffer(gpuMesh.indexBuffer));
				}
				else
				{
					done(command->BindVertexBuffer(vertexBuffer));
					done(command->BindIndexBuffer(indexBuffer));
				}
				boundMesh = mesh.get();
			}
			done(command->BindPipeline(pass.pipeline));
			if (pass.group)
			{
				done(command->BindGroup(pass.group));
			}
			const Ursa::UInt32 indexCount = mesh ? MeshFor(mesh).indexCount : 36;
			done(command->DrawIndexed(indexCount, 1, static_cast<Ursa::UInt32>(pass.itemIndex)));
		}
		done(command->EndRendering());
		done(command->Barrier(color, Rhi::ResourceState::ColorAttachment, after));
	}

	Rhi::FencePtr Draw(const Rhi::TexturePtr& color, Rhi::ResourceState before, Rhi::ResourceState after,
	                   const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes, const SceneLighting& lighting,
	                   Vec3 cameraPosition, const Rhi::SwapchainPtr& swapchain = {})
	{
		auto command = Take(device->CreateCommandList());
		done(command->Begin());
		record(command, color, before, after, viewProjection, boxes, lighting, cameraPosition);
		done(command->End());
		lastFence = Take(device->Submit(command, swapchain));
		return lastFence;
	}

	struct GpuTexture
	{
		MaterialTexturePtr source;
		Rhi::TexturePtr texture;
	};

	Rhi::DevicePtr device;
	std::shared_ptr<MaterialInstance> fallback;
	std::shared_ptr<const ShaderAsset> builtinUnlit;
	std::shared_ptr<const ShaderAsset> builtinPbr;
	Rhi::BufferPtr vertexBuffer;
	Rhi::BufferPtr indexBuffer;
	Rhi::TexturePtr depth;
	Rhi::FencePtr lastFence;
	Map<String, ShaderPipelineCache> shaderPipelines;
	UnorderedMap<const MaterialTexture*, GpuTexture> textureCache;
	Map<String, GpuModelMesh> modelMeshes;
};

Renderer3D::Renderer3D(std::unique_ptr<Impl> implementation) : impl(std::move(implementation))
{
}

Renderer3D::~Renderer3D() = default;

Result<std::unique_ptr<Renderer3D>> Renderer3D::Create(Rhi::DevicePtr selectedDevice)
{
	try
	{
		if (!selectedDevice)
		{
			throw std::runtime_error("3D renderer requires a device");
		}
		auto implementation = std::make_unique<Impl>(std::move(selectedDevice));
		implementation->Initialize();
		return Result<std::unique_ptr<Renderer3D>>::Success(
		    std::unique_ptr<Renderer3D>(new Renderer3D(std::move(implementation))));
	}
	catch (const std::exception& error)
	{
		return Result<std::unique_ptr<Renderer3D>>::Failure(error.what());
	}
}

Result<void> Renderer3D::Render(const Rhi::SwapchainPtr& swapchain, const Camera& camera,
                                const Vector<BoxInstance>& boxes)
{
	URSA_PROFILE_SCOPE("Renderer3D.Render");
	try
	{
		if (!swapchain)
		{
			throw std::runtime_error("3D renderer requires a swapchain");
		}
		impl->WaitPrevious();
		const auto acquired = Take(swapchain->Acquire());
		if (!acquired.texture)
		{
			return {};
		}
		const Ursa::Float32 aspect = static_cast<Ursa::Float32>(acquired.texture->desc.width) /
		                             static_cast<Ursa::Float32>(acquired.texture->desc.height);
		impl->Draw(acquired.texture, acquired.initialState, Rhi::ResourceState::Present, camera.ViewProjection(aspect),
		           ModelsFromBoxes(boxes), {}, camera.position, swapchain);
		done(swapchain->Present());
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

Result<void> Renderer3D::RenderModels(const Rhi::SwapchainPtr& swapchain, const Mat4& viewProjection,
                                      const Vector<ModelBoxInstance>& boxes, const SceneLighting& lighting,
                                      Vec3 cameraPosition)
{
	URSA_PROFILE_SCOPE("Renderer3D.RenderModels");
	try
	{
		if (!swapchain)
		{
			throw std::runtime_error("3D renderer requires a swapchain");
		}
		impl->WaitPrevious();
		const auto acquired = Take(swapchain->Acquire());
		if (!acquired.texture)
		{
			return {};
		}
		impl->Draw(acquired.texture, acquired.initialState, Rhi::ResourceState::Present, viewProjection, boxes,
		           lighting, cameraPosition, swapchain);
		done(swapchain->Present());
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}

Result<Rhi::FencePtr> Renderer3D::RenderOffscreen(const Rhi::TexturePtr& color, const Camera& camera,
                                                  const Vector<BoxInstance>& boxes)
{
	URSA_PROFILE_SCOPE("Renderer3D.RenderOffscreen");
	try
	{
		impl->WaitPrevious();
		if (!color)
		{
			throw std::runtime_error("3D renderer requires a color target");
		}
		const Ursa::Float32 aspect =
		    static_cast<Ursa::Float32>(color->desc.width) / static_cast<Ursa::Float32>(color->desc.height);
		return Result<Rhi::FencePtr>::Success(
		    impl->Draw(color, Rhi::ResourceState::Undefined, Rhi::ResourceState::TransferSource,
		               camera.ViewProjection(aspect), ModelsFromBoxes(boxes), {}, camera.position));
	}
	catch (const std::exception& error)
	{
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<Rhi::FencePtr> Renderer3D::RenderOffscreenModels(const Rhi::TexturePtr& color, const Mat4& viewProjection,
                                                        const Vector<ModelBoxInstance>& boxes,
                                                        const SceneLighting& lighting, Vec3 cameraPosition)
{
	URSA_PROFILE_SCOPE("Renderer3D.RenderOffscreenModels");
	try
	{
		impl->WaitPrevious();
		return Result<Rhi::FencePtr>::Success(impl->Draw(color, Rhi::ResourceState::Undefined,
		                                                 Rhi::ResourceState::TransferSource, viewProjection, boxes,
		                                                 lighting, cameraPosition));
	}
	catch (const std::exception& error)
	{
		return Result<Rhi::FencePtr>::Failure(error.what());
	}
}

Result<void> Renderer3D::RecordOffscreenModels(const Rhi::CommandListPtr& command, const Rhi::TexturePtr& color,
                                               Rhi::ResourceState initialState, Rhi::ResourceState finalState,
                                               const Mat4& viewProjection, const Vector<ModelBoxInstance>& boxes,
                                               const SceneLighting& lighting, Vec3 cameraPosition)
{
	URSA_PROFILE_SCOPE("Renderer3D.RecordOffscreenModels");
	try
	{
		impl->WaitPrevious();
		impl->record(command, color, initialState, finalState, viewProjection, boxes, lighting, cameraPosition);
		return {};
	}
	catch (const std::exception& error)
	{
		return Result<void>::Failure(error.what());
	}
}
} // namespace Ursa
