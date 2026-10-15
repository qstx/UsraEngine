#include <Shader/ShaderRenderState.hpp>
#include <Core/StringInterop.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Ursa::Shader
{
namespace
{
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

} // namespace

void ApplyGraphicsRenderStates(Rhi::GraphicsPipelineDesc& description, const Vector<RenderStateDeclaration>& states)
{
	ApplyRenderStates(description, states);
}
} // namespace Ursa::Shader
