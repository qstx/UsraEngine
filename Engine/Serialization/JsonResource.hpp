#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/ResourceFile.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Ursa
{
Result<nlohmann::json> ReadJsonResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                        ResourceKind expectedKind);

Result<void> WriteJsonResource(VirtualFileSystem& filesystem, const VirtualPath& path, const nlohmann::json& document);

} // namespace Ursa
