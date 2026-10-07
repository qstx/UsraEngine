#pragma once
#include <Vfs/VirtualFileSystem.hpp>
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

Result<nlohmann::json> EncodeSceneDocument(const String& path, ResourceId id, const Scene& scene,
                                           const ComponentRegistry& registry, const MaterialIdResolver& materialId = {},
                                           const GameObject::Ptr& subtree = {});

Result<LoadedSceneResource> DecodeSceneDocument(const String& path, const nlohmann::json& document,
                                                const ComponentRegistry& registry, ComponentLoadContext& context);

Result<void> SaveSceneResource(VirtualFileSystem& filesystem, const VirtualPath& path, ResourceId id,
                               const Scene& scene, const ComponentRegistry& registry,
                               const MaterialIdResolver& materialId = {});

Result<LoadedSceneResource> LoadSceneResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                              const ComponentRegistry& registry, ComponentLoadContext& context);

Result<void> ReloadSceneResource(const VirtualFileSystem& filesystem, const VirtualPath& path,
                                 const ComponentRegistry& registry, ComponentLoadContext& context,
                                 std::unique_ptr<Scene>& activeScene);

} // namespace Ursa
