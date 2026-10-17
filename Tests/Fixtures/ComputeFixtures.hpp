#pragma once

#include <Shader/ShaderCompiler.hpp>
#include <Shader/ShaderAsset.hpp>

namespace Ursa::Test
{
Result<std::shared_ptr<Shader::ShaderAsset>> CreateComputeFixture(const Shader::ShaderCompilerTools& tools,
                                                                  ResourceId identity = ResourceId::Generate(),
                                                                  bool matrixProbe = false);
} // namespace Ursa::Test
