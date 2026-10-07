#pragma once

#include <Shader/ShaderAsset.hpp>

namespace Ursa::Shader
{
ResourceId BuiltinUnlitShaderId();

ResourceId BuiltinPbrShaderId();

ShaderAsset CreateBuiltinUnlitShaderAsset();

ShaderAsset CreateBuiltinPbrShaderAsset();
} // namespace Ursa::Shader
