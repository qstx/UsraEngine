#pragma once

#include <Shader/ShaderAsset.hpp>
#include <optional>

namespace Ursa::Rendering
{
struct ResolvedGraphicsPass
{
	std::shared_ptr<const Shader::ShaderAsset> programShader;
	UInt32 subShaderIndex = 0;
	UInt32 passIndex = 0;
	Vector<Shader::RenderStateDeclaration> renderStates;
};

Result<Vector<ResolvedGraphicsPass>> OrderedGraphicsPasses(const std::shared_ptr<const Shader::ShaderAsset>& shader,
                                                           const String& activePipeline);
Result<std::optional<ResolvedGraphicsPass>> SelectGraphicsPass(const std::shared_ptr<const Shader::ShaderAsset>& shader,
                                                               const String& activePipeline,
                                                               const Vector<String>& shaderTags,
                                                               Int32 explicitPass = -1);
} // namespace Ursa::Rendering
