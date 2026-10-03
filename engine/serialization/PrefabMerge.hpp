#pragma once
#include <core/Containers.hpp>
#include <core/Result.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace ursa {
Result<nlohmann::json> mergePrefabNodes(const nlohmann::json &source, const nlohmann::json &overrides,
                                        const String &location);
} // namespace ursa
