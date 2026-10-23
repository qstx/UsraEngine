#pragma once

#include <Serialization/ResourceId.hpp>

namespace Ursa::Shader
{
ResourceId BuiltinUnlitShaderId();

ResourceId BuiltinPbrShaderId();

ResourceId BuiltinBlitShaderId();

ResourceId BuiltinFinalPostShaderId();
ResourceId BuiltinBloomShaderId();

const char* BuiltinShaderAssetPath(ResourceId id);
} // namespace Ursa::Shader
