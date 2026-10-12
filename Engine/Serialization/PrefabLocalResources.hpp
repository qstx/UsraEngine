#pragma once

#include <Serialization/ResourceId.hpp>
#include <nlohmann/json.hpp>

namespace Ursa
{
void RemapLocalResourceBindings(nlohmann::json& value, const Map<String, String>& mapping);

Set<String> ReferencedLocalResources(const nlohmann::json& value);

Result<void> ExpandPrefabLocalResources(const nlohmann::json& prefab, const nlohmann::json& overrides,
                                        ResourceId instance, Map<String, String>& scoped, nlohmann::json& expanded);

Result<void> CapturePrefabLocalResources(const nlohmann::json& current, const nlohmann::json& baseline,
                                         const String& instance, Map<String, String>& scoped,
                                         nlohmann::json& overrides);

void CaptureRegularLocalResources(nlohmann::json& document, const nlohmann::json& encoded);

Result<nlohmann::json> MergePrefabLocalResources(const nlohmann::json& prefab, const nlohmann::json& overrides);
} // namespace Ursa
