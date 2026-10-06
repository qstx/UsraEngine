#pragma once
#include <Core/Types.hpp>
#include <Core/Containers.hpp>
#include <Core/Result.hpp>
#include <Serialization/ComponentRegistry.hpp>
#include <filesystem>
#include <memory>

namespace Ursa
{
constexpr Ursa::UInt32 sceneGraphFormatVersion = 2;

struct LoadedSceneResource
{
	ResourceId id;
	std::unique_ptr<Scene> scene;
};

Result<nlohmann::json> EncodeSceneDocument(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                                           const ComponentRegistry& registry, const MaterialIdResolver& materialId = {},
                                           const GameObject::Ptr& subtree = {});

Result<LoadedSceneResource> DecodeSceneDocument(const std::filesystem::path& path, const nlohmann::json& document,
                                                const ComponentRegistry& registry, ComponentLoadContext& context);

Result<void> SaveSceneResource(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                               const ComponentRegistry& registry, const MaterialIdResolver& materialId = {});

Result<LoadedSceneResource> LoadSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                              ComponentLoadContext& context);

Result<void> ReloadSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                 ComponentLoadContext& context, std::unique_ptr<Scene>& activeScene);
} // namespace Ursa
