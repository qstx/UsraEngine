#pragma once

#include <Shader/ShaderAsset.hpp>
#include <Rhi/RHI.hpp>

namespace Ursa::Shader
{
void ApplyGraphicsRenderStates(Rhi::GraphicsPipelineDesc& description, const Vector<RenderStateDeclaration>& states);
} // namespace Ursa::Shader
