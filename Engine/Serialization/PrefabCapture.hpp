#pragma once
#include <Core/Containers.hpp>
#include <Serialization/PrefabResource.hpp>

namespace Ursa
{
Result<void> CaptureLinkedSceneChanges(const std::filesystem::path& path, LinkedSceneResource& linked,
                                       const ComponentRegistry& registry, const MaterialIdResolver& materialId);
} // namespace Ursa
