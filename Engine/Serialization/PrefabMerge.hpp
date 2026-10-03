#pragma once
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace Ursa
{
Result<nlohmann::json> MergePrefabNodes(const nlohmann::json& source, const nlohmann::json& overrides,
                                        const String& location);
} // namespace Ursa
