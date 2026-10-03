#pragma once
#include <core/Containers.hpp>
#include <serialization/PrefabResource.hpp>

namespace Ursa
{
Result<void> CaptureLinkedSceneChanges(const std::filesystem::path& path, LinkedSceneResource& linked,
                                       const ComponentRegistry& registry, const MaterialIdResolver& materialId);
} // namespace Ursa
