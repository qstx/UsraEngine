#pragma once

// Host fixture bootstrap for tests only. Resource operations still use VFS.
#include <Vfs/VirtualFileSystem.hpp>
#include <Core/StringInterop.hpp>
#include <filesystem>

#include <Serialization/ResourceFile.hpp>
#include <Serialization/JsonResource.hpp>
#include <Serialization/TextureResource.hpp>
#include <Serialization/MaterialResource.hpp>
#include <Serialization/SceneResource.hpp>
#include <Serialization/PrefabResource.hpp>
#include <Serialization/PrefabAsset.hpp>
#include <Serialization/PrefabCapture.hpp>

namespace Ursa
{
struct HostResourceBridge
{
	VirtualFileSystem filesystem;
	VirtualPath path;

	static Result<HostResourceBridge> Open(const std::filesystem::path& filename, MountAccess access)
	{
		std::error_code error;
		auto absolute = std::filesystem::absolute(filename, error);
		if (error)
		{
			return Result<HostResourceBridge>::Failure("Invalid fixture path");
		}
		HostResourceBridge bridge;
		auto mounted = bridge.filesystem.Mount("fixture", absolute.parent_path(), access);
		if (!mounted)
		{
			return Result<HostResourceBridge>::Failure(mounted.error);
		}
		auto parsed = VirtualPath::Parse("fixture:" + PathToUrsaString(absolute.filename()));
		if (!parsed)
		{
			return Result<HostResourceBridge>::Failure(parsed.error);
		}
		bridge.path = std::move(parsed.value);
		return Result<HostResourceBridge>::Success(std::move(bridge));
	}
};

inline Result<ResourceHeader> ReadResourceHeader(const std::filesystem::path& path)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? ReadResourceHeader(bridge.value.filesystem, bridge.value.path)
	              : Result<ResourceHeader>::Failure(bridge.error);
}

inline Result<nlohmann::json> ReadJsonResource(const std::filesystem::path& path, ResourceKind expectedKind)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? ReadJsonResource(bridge.value.filesystem, bridge.value.path, expectedKind)
	              : Result<nlohmann::json>::Failure(bridge.error);
}

inline Result<void> WriteJsonResource(const std::filesystem::path& path, const nlohmann::json& document)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge ? WriteJsonResource(bridge.value.filesystem, bridge.value.path, document)
	              : Result<void>::Failure(bridge.error);
}

inline Result<void> SaveTextureResource(const std::filesystem::path& path, ResourceId id,
                                        const MaterialTexture& texture)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge ? SaveTextureResource(bridge.value.filesystem, bridge.value.path, id, texture)
	              : Result<void>::Failure(bridge.error);
}

inline Result<LoadedTextureResource> LoadTextureResource(const std::filesystem::path& path)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? LoadTextureResource(bridge.value.filesystem, bridge.value.path)
	              : Result<LoadedTextureResource>::Failure(bridge.error);
}

inline Result<void> SaveMaterialResource(const std::filesystem::path& path, ResourceId id,
                                         const MaterialInstance& material, const TextureIdResolver& resolver)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge ? SaveMaterialResource(bridge.value.filesystem, bridge.value.path, id, material, resolver)
	              : Result<void>::Failure(bridge.error);
}

inline Result<LoadedMaterialResource> LoadMaterialResource(const std::filesystem::path& path,
                                                           const TextureResourceLoader& loader)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? LoadMaterialResource(bridge.value.filesystem, bridge.value.path, loader)
	              : Result<LoadedMaterialResource>::Failure(bridge.error);
}

inline Result<void> SaveSceneResource(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                                      const ComponentRegistry& registry, const MaterialIdResolver& materialId = {})
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge ? SaveSceneResource(bridge.value.filesystem, bridge.value.path, id, scene, registry, materialId)
	              : Result<void>::Failure(bridge.error);
}

inline Result<LoadedSceneResource> LoadSceneResource(const std::filesystem::path& path,
                                                     const ComponentRegistry& registry, ComponentLoadContext& context)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? LoadSceneResource(bridge.value.filesystem, bridge.value.path, registry, context)
	              : Result<LoadedSceneResource>::Failure(bridge.error);
}

inline Result<void> ReloadSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                        ComponentLoadContext& context, std::unique_ptr<Scene>& activeScene)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? ReloadSceneResource(bridge.value.filesystem, bridge.value.path, registry, context, activeScene)
	              : Result<void>::Failure(bridge.error);
}

inline Result<void> SavePrefabResource(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                                       GameObject::Ptr root, const ComponentRegistry& registry,
                                       const MaterialIdResolver& materialId = {})
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge
	           ? SavePrefabResource(bridge.value.filesystem, bridge.value.path, id, scene, root, registry, materialId)
	           : Result<void>::Failure(bridge.error);
}

inline Result<LoadedSceneResource> LoadPrefabResource(const std::filesystem::path& path,
                                                      const ComponentRegistry& registry, ComponentLoadContext& context)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? LoadPrefabResource(bridge.value.filesystem, bridge.value.path, registry, context)
	              : Result<LoadedSceneResource>::Failure(bridge.error);
}

inline Result<LinkedSceneResource> LoadLinkedSceneResource(const std::filesystem::path& path,
                                                           const ComponentRegistry& registry,
                                                           ComponentLoadContext& context)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? LoadLinkedSceneResource(bridge.value.filesystem, bridge.value.path, registry, context)
	              : Result<LinkedSceneResource>::Failure(bridge.error);
}

inline Result<void> SaveLinkedSceneResource(const std::filesystem::path& path, LinkedSceneResource& linked,
                                            const ComponentRegistry& registry,
                                            const MaterialIdResolver& materialId = {})
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge ? SaveLinkedSceneResource(bridge.value.filesystem, bridge.value.path, linked, registry, materialId)
	              : Result<void>::Failure(bridge.error);
}

inline Result<void> ReloadLinkedSceneResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                              ComponentLoadContext& context, LinkedSceneResource& active)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? ReloadLinkedSceneResource(bridge.value.filesystem, bridge.value.path, registry, context, active)
	              : Result<void>::Failure(bridge.error);
}

inline Result<void> SavePrefabAssetResource(
    const std::filesystem::path& path, const PrefabAsset& asset, const ComponentRegistry& registry,
    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {})
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadWrite);
	return bridge ? SavePrefabAssetResource(bridge.value.filesystem, bridge.value.path, asset, registry, materialId)
	              : Result<void>::Failure(bridge.error);
}

inline Result<PrefabAsset> LoadPrefabAssetResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                                   ComponentLoadContext& context)
{
	auto bridge = HostResourceBridge::Open(path, MountAccess::ReadOnly);
	return bridge ? LoadPrefabAssetResource(bridge.value.filesystem, bridge.value.path, registry, context)
	              : Result<PrefabAsset>::Failure(bridge.error);
}

inline Result<nlohmann::json> EncodeSceneDocument(const std::filesystem::path& path, ResourceId id, const Scene& scene,
                                                  const ComponentRegistry& registry,
                                                  const MaterialIdResolver& materialId = {},
                                                  const GameObject::Ptr& subtree = {})
{
	return EncodeSceneDocument(PathToUrsaString(path), id, scene, registry, materialId, subtree);
}

inline Result<LoadedSceneResource> DecodeSceneDocument(const std::filesystem::path& path,
                                                       const nlohmann::json& document,
                                                       const ComponentRegistry& registry, ComponentLoadContext& context)
{
	return DecodeSceneDocument(PathToUrsaString(path), document, registry, context);
}

inline Result<LinkedSceneResource> DecodeLinkedSceneDocument(const std::filesystem::path& path, nlohmann::json document,
                                                             const ComponentRegistry& registry,
                                                             ComponentLoadContext& context)
{
	return DecodeLinkedSceneDocument(PathToUrsaString(path), std::move(document), registry, context);
}

inline Result<void> CaptureLinkedSceneChanges(const std::filesystem::path& path, LinkedSceneResource& linked,
                                              const ComponentRegistry& registry,
                                              const MaterialIdResolver& resolver = {})
{
	return CaptureLinkedSceneChanges(PathToUrsaString(path), linked, registry, resolver);
}

} // namespace Ursa
