#pragma once
#include <Core/Containers.hpp>
#include <Serialization/PrefabResource.hpp>

namespace Ursa
{
Result<nlohmann::json> CapturePrefabDocumentChanges(const nlohmann::json& source, const nlohmann::json& current);

Result<void> CaptureLinkedSceneChanges(const String& path, LinkedSceneResource& linked,
                                       const ComponentRegistry& registry, const MaterialIdResolver& materialId);
} // namespace Ursa
