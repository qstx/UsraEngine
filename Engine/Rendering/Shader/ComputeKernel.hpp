#pragma once

#include <Shader/ShaderAsset.hpp>

namespace Ursa::Rendering
{
Result<UInt32> FindComputeKernel(const Shader::ShaderAsset& shader, const String& name);
Result<const Shader::ShaderPassAsset*> ComputeKernelAt(const Shader::ShaderAsset& shader, UInt32 index);
} // namespace Ursa::Rendering
