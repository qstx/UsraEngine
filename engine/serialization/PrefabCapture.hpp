#pragma once
#include <core/Containers.hpp>
#include <serialization/PrefabResource.hpp>

namespace ursa {
Result<void> captureLinkedSceneChanges(const std::filesystem::path &path, LinkedSceneResource &linked,
                                       const ComponentRegistry &registry, const MaterialIdResolver &materialId);
} // namespace ursa
