#pragma once

#include <Shader/ShaderAsset.hpp>
#include <Serialization/ResourceFile.hpp>

namespace Ursa::Shader
{
Result<void> SaveShaderAssetResource(VirtualFileSystem& filesystem, const VirtualPath& path, const ShaderAsset& shader);

Result<ShaderAsset> LoadShaderAssetResource(const VirtualFileSystem& filesystem, const VirtualPath& path);
} // namespace Ursa::Shader
