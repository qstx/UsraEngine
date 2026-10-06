#pragma once

#include <Scene/GameObject.hpp>
#include <Serialization/ResourceId.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace Ursa
{
class ComponentRegistry;
class ComponentLoadContext;
class Scene;
class MaterialInstance;

class PrefabAsset final
{
public:
	PrefabAsset() = default;

	PrefabAsset(ResourceId assetId, GameObject::Ptr rootObject) : id(assetId), root(std::move(rootObject))
	{
	}

	ResourceId Id() const
	{
		return id;
	}

	const GameObject::Ptr& Root() const
	{
		return root;
	}

	Result<GameObject::Ptr> Instantiate(
	    Scene& scene, const ComponentRegistry& registry, ComponentLoadContext& context,
	    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {}) const;

private:
	ResourceId id;
	GameObject::Ptr root;
};

Result<void> SavePrefabAssetResource(
    const std::filesystem::path& path, const PrefabAsset& asset, const ComponentRegistry& registry,
    const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {});

Result<PrefabAsset> LoadPrefabAssetResource(const std::filesystem::path& path, const ComponentRegistry& registry,
                                            ComponentLoadContext& context);

Result<PrefabAsset>
CapturePrefabAsset(ResourceId id, const Scene& scene, GameObject::Ptr root, const ComponentRegistry& registry,
                   ComponentLoadContext& context,
                   const std::function<Result<ResourceId>(const std::shared_ptr<MaterialInstance>&)>& materialId = {});
} // namespace Ursa
